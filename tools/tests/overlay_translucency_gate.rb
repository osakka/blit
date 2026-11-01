#!/usr/bin/env ruby
# frozen_string_literal: true

# A translucent /v1/overlays rectangle is one blend over the scene, on
# whichever compositor BLIT_GATE_BACKEND picks (the suite runs it on both).
# One display, one pane in a flat colour, an overlay at opacity 50 over it.
#
#   * first frame: every overlay pixel is the integer blend of its colour
#     and the pane's, (src * a + dst * (255 - a) + 127) / 255 with
#     a = 50 * 255 / 100, and the pixels around it are the pane
#   * no drift: after partial repaints (a cell written outside the
#     overlay, eight times) and the pointer crossing it, every overlay
#     pixel is still that blend. The CPU compositor blended the overlay
#     into its scene again on every frame, so each partial frame darkened
#     it towards the overlay's colour until it was opaque
#   * deleted: the region it covered is the pane again
# BLIT_OVERLAY_GATE_SHOT=<path> keeps the screenshot after the repaints.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19591'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 1024
H = 768
FLAT = [40, 90, 160].freeze
COLOUR = [255, 128, 0].freeze
# Cell-aligned, clear of the written cell (row 40) and of the pointer's
# resting place (0, H - 1).
X = 200
Y = 96
WIDTH = 240
HEIGHT = 160

abort("overlay translucency gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "OVERLAY_TRANSLUCENCY_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 5)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def screenshot
  GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
end

def region(shot, x, y, width, height)
  (y...(y + height)).flat_map { |py| (x...(x + width)).map { |px| shot.pixel(px, py) } }
end

def present_calls
  get('/v1/diagnostics')['render']['outputs'].first['present_calls']
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x1', 'BLIT_AUTH_TOKEN' => '')
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-overlay-translucency') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  # Each line writes one cell on row 40, far below the overlay: a partial
  # repaint that does not touch it.
  code, = request('POST', '/v1/terminal-sessions',
                  { command: '/bin/sh', pane_id: 'pane-display-1-1',
                    arguments: ['-c', "stty -echo; bg='\\033[48;2;#{FLAT.join(';')}m'; " \
                                      "printf \"\\033[?25l${bg}\\033[2J\"; " \
                                      "while read -r line; do printf \"${bg}\\033[40;1H%s\" \"$line\"; done"] })
  check(code == 201, "session: #{code}")
  session = get('/v1/terminal-sessions')['sessions'].first['id']
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 0, y: H - 1 })
  backend = GATE_BACKEND == 'gles' ? 'gles' : 'cpu'
  wait_until("the #{backend} compositor is not presenting") do
    get('/v1/diagnostics')['render']['outputs'].first.dig('compositor', 'active') == backend
  end
  wait_until('the pane is not flat') { screenshot.pixel(X + 4, Y + 4) == FLAT }

  alpha = 50 * 255 / 100
  blended = COLOUR.zip(FLAT).map { |s, d| (s * alpha + d * (255 - alpha) + 127) / 255 }
  code, overlay = request('POST', '/v1/overlays',
                          { display_id: 'display-1', x: X, y: Y, width: WIDTH, height: HEIGHT,
                            color: format('#%02x%02x%02x', *COLOUR), opacity: 50 })
  check(code == 201, "overlay: #{code} #{overlay}")
  sleep 0.6
  first = screenshot
  check(region(first, X, Y, WIDTH, HEIGHT).uniq == [blended],
        "opacity 50 drew #{region(first, X, Y, WIDTH, HEIGHT).uniq.first(4)} over #{FLAT}, not #{blended}")
  check([first.pixel(X - 1, Y), first.pixel(X + WIDTH, Y + HEIGHT - 1), first.pixel(X, Y - 1),
         first.pixel(X + WIDTH - 1, Y + HEIGHT)].uniq == [FLAT], 'the overlay spilled past its bounds')

  # Partial frames: a cell elsewhere, then the pointer across the overlay
  # and away again. Each one is a frame presented over the last.
  before = present_calls
  8.times do |i|
    request('POST', "/v1/terminal-sessions/#{session}/input", { data: "#{i}\n" })
    sleep 0.15
  end
  [[X + 20, Y + 20], [X + WIDTH / 2, Y + HEIGHT / 2], [X + WIDTH - 30, Y + 40], [0, H - 1]].each do |px, py|
    request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: px, y: py })
    sleep 0.15
  end
  sleep 0.6
  frames = present_calls - before
  check(frames >= 8, "only #{frames} frames presented for the repaints")
  later = screenshot
  File.binwrite(ENV['BLIT_OVERLAY_GATE_SHOT'], later.bytes) if ENV['BLIT_OVERLAY_GATE_SHOT']
  drifted = region(later, X, Y, WIDTH, HEIGHT).uniq
  check(drifted == [blended],
        "the translucent overlay drifted over #{frames} frames: #{drifted.first(4)}, not #{blended}")

  code, = request('DELETE', "/v1/overlays/#{overlay['id']}")
  check(code == 204, "delete: #{code}")
  sleep 0.6
  gone = screenshot
  check(region(gone, X, Y, WIDTH, HEIGHT).uniq == [FLAT],
        "the overlay's region is not the pane again: #{region(gone, X, Y, WIDTH, HEIGHT).uniq.first(4)}")

  puts "OVERLAY_TRANSLUCENCY_PASS backend=#{backend} opacity50=#{blended.join(',')}_exact " \
       "no_drift=#{frames}_frames(repaints+pointer) deleted=pane"
end
