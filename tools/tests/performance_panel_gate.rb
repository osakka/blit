#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0203: the performance panel, on whichever compositor BLIT_GATE_BACKEND
# picks (the suite runs it on both). One display, one pane in a flat colour.
#
#   * BLIT_HUD=1 turns the panel on when the document says nothing
#   * off: no panel pixel in the screenshot, no updates
#   * on: the panel is in the chosen corner, 16 px in, its border and
#     background exact; its heading and compositor lines match, pixel for
#     pixel, the boot PSF drawn by this gate, and the compositor line says
#     what /v1/diagnostics says -- the same reference on both compositors
#   * idle for 5 s: the output presents at most hz x 5 + 2 frames, all of
#     them counted as hud_frames, and the frame rate leaves them out
#   * an update costs under 0.5 ms on average (hud.mean_update_ms)
#   * off again: the region it covered is the pixels it covered before
#   * strict: a bad hz, section, corner or member is refused
# BLIT_HUD_GATE_SHOT=<path> keeps the screenshot with the panel on.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19541'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 1024
H = 768
FLAT = [40, 90, 160].freeze
BACKGROUND = [0x10, 0x10, 0x14].freeze
BORDER = [0x50, 0x50, 0x5a].freeze
TEXT = [0xd8, 0xd8, 0xdc].freeze
HEADING = [0x80, 0xb4, 0xff].freeze
MARGIN = 16
PAD = 6
HZ = 2

abort("performance panel gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s if revision
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
  raise "PERFORMANCE_PANEL_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 5)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def screenshot_bytes
  Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1')
end

# The boot PSF (PSF2 with a Unicode table), as Blit reads it.
class Psf
  attr_reader :width, :height

  def initialize(path)
    data = File.binread(path)
    raise 'not PSF2' unless data.byteslice(0, 4).bytes == [0x72, 0xb5, 0x4a, 0x86]
    _version, header, flags, count, bytes, @height, @width = data.byteslice(4, 28).unpack('V7')
    @row = (@width + 7) / 8
    @glyphs = (0...count).map { |i| data.byteslice(header + i * bytes, bytes) }
    @map = {}
    return unless flags & 1 == 1
    table = data.byteslice(header + count * bytes, data.bytesize).bytes
    glyph = 0
    sequence = false
    run = []
    table.each do |byte|
      if byte == 0xff
        @map[run.pack('C*').force_encoding('UTF-8').ord] ||= glyph if !sequence && !run.empty?
        glyph += 1
        run = []
        sequence = false
      elsif byte == 0xfe
        sequence = true
      elsif !sequence
        run << byte
        text = run.pack('C*').force_encoding('UTF-8')
        if text.valid_encoding?
          @map[text.ord] ||= glyph
          run = []
        end
      end
    end
  end

  def ink?(char, row, column)
    glyph = @glyphs[@map.fetch(char.ord) { @map.fetch('?'.ord) }]
    glyph.getbyte(row * @row + column / 8) & (0x80 >> (column % 8)) != 0
  end
end

# Whether `text` is drawn at (x, y) in `ink` over the panel background,
# pixel for pixel.
def text_at?(shot, font, x, y, text, ink)
  text.each_char.with_index.all? do |char, index|
    (0...font.height).all? do |row|
      (0...font.width).all? do |column|
        want = font.ink?(char, row, column) ? ink : BACKGROUND
        shot.pixel(x + index * font.width + column, y + row) == want
      end
    end
  end
end

def region(shot, x, y, width, height)
  (y...(y + height)).flat_map { |py| (x...(x + width)).map { |px| shot.pixel(px, py) } }
end

def hud_settings(document_hud)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  if document_hud.nil? then document.delete('hud') else document['hud'] = document_hud end
  request('PUT', '/v1/settings', { document: document }, settings['revision'])
end

def output
  get('/v1/diagnostics')['render']['outputs'].first
end

font = Psf.new(FONT)
env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x1', 'BLIT_AUTH_TOKEN' => '', 'BLIT_HUD' => '1',
  'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'))
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-performance-panel') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  code, = request('POST', '/v1/terminal-sessions',
                  { command: '/bin/sh', pane_id: 'pane-display-1-1',
                    arguments: ['-c', "printf '\\033[?25l\\033[48;2;#{FLAT.join(';')}m\\033[2J'; sleep 600"] })
  check(code == 201, "session: #{code}")
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 0, y: H - 1 })
  backend = GATE_BACKEND == 'gles' ? 'gles' : 'cpu'
  wait_until("the #{backend} compositor is not presenting") do
    output.dig('compositor', 'active') == backend
  end

  # BLIT_HUD=1 with nothing in the document: on.
  hud = get('/v1/settings').dig('effective', 'hud')
  check(hud == { 'enabled' => true, 'display_id' => 'all', 'corner' => 'top-right', 'hz' => 2,
                 'sections' => %w[blit hibr graph] }, "BLIT_HUD=1 default: #{hud}")
  wait_until('BLIT_HUD=1 drew no panel') { !get('/v1/diagnostics')['hud']['panels'].empty? }

  # Off: absent, and no timer.
  code, reply = hud_settings({ 'enabled' => false })
  check(code == 200, "hud off: #{code} #{reply}")
  wait_until('the panel stayed in diagnostics') { get('/v1/diagnostics')['hud']['panels'].empty? }
  sleep 0.6
  off = GateBmp.new(screenshot_bytes)
  check(off.count(BACKGROUND).zero? && off.count(BORDER).zero?, 'panel pixels with the panel off')
  check(off.pixel(W - 40, 40) == FLAT, "the pane is not under the corner: #{off.pixel(W - 40, 40)}")
  updates = get('/v1/diagnostics')['hud']['updates']
  sleep 1.0
  check(get('/v1/diagnostics')['hud']['updates'] == updates, 'the panel updated while off')

  # Strict.
  [{ 'hz' => 11 }, { 'hz' => 0 }, { 'sections' => ['blit', 'blit'] }, { 'sections' => ['cpu'] },
   { 'corner' => 'middle' }, { 'display_id' => 'screen-1' }, { 'enabled' => 'yes' },
   { 'opacity' => 50 }].each do |bad|
    code, reply = hud_settings({ 'enabled' => false }.merge(bad))
    check(code == 400 && reply['code'] == 'invalid_settings_hud', "#{bad} was not refused: #{code} #{reply}")
  end

  # On, top right, every section.
  code, reply = hud_settings({ 'enabled' => true, 'display_id' => 'display-1', 'corner' => 'top-right',
                               'hz' => HZ, 'sections' => %w[blit hibr graph] })
  check(code == 200, "hud on: #{code} #{reply}")
  check(get('/v1/settings').dig('document', 'hud', 'corner') == 'top-right', 'hud was not kept in the document')
  wait_until('no panel with hud on') { !get('/v1/diagnostics')['hud']['panels'].empty? }
  sleep 0.6
  bytes = screenshot_bytes
  shot = GateBmp.new(bytes)
  diagnostics = get('/v1/diagnostics')
  panel = diagnostics['hud']['panels'].first
  x, y, width, height = panel.values_at('x', 'y', 'width', 'height')
  check(panel['display_id'] == 'display-1' && x == W - MARGIN - width && y == MARGIN,
        "panel placed at #{x},#{y} #{width}x#{height}")
  check(shot.pixel(x, y) == BORDER && shot.pixel(x + width - 1, y + height - 1) == BORDER &&
        shot.pixel(x + 1, y + 1) == BACKGROUND && shot.pixel(x - 1, y) == FLAT &&
        shot.pixel(x + width, y + height) == FLAT,
        "panel edges: #{shot.pixel(x, y)} #{shot.pixel(x + 1, y + 1)} #{shot.pixel(x - 1, y)}")
  lines = panel['lines']
  compositor = diagnostics['render']['outputs'].first['compositor']
  wanted = if backend == 'cpu'
             'compositor cpu (requested)'
           else
             "compositor gles  #{compositor['renderer'].split(' | ').first}"
           end
  check(lines[0] == 'blit  display-1' && lines[3] == wanted,
        "panel lines #{lines[0..3]} against the compositor #{compositor['active']} #{compositor['renderer']}")
  check(text_at?(shot, font, x + 1 + PAD, y + 1 + PAD, lines[0], HEADING),
        'the heading is not drawn as the boot PSF draws it')
  check(text_at?(shot, font, x + 1 + PAD, y + 1 + PAD + 3 * font.height, wanted, TEXT),
        'the compositor line is not drawn as the boot PSF draws it')
  File.binwrite(ENV['BLIT_HUD_GATE_SHOT'], bytes) if ENV['BLIT_HUD_GATE_SHOT']

  # Idle: only the panel's own frames, at most hz a second.
  before = output
  sleep 5
  after = output
  presents = after['present_calls'] - before['present_calls']
  hud_frames = after['hud_frames'] - before['hud_frames']
  check(presents <= HZ * 5 + 2, "#{presents} presents in an idle 5 s at #{HZ} Hz")
  check(hud_frames == presents && hud_frames >= HZ * 5 - 3,
        "#{presents} idle presents, #{hud_frames} of them hud_frames")
  fps = get('/v1/diagnostics/runtime')['performance']['frame_rate_hz']
  check(fps < 0.5, "the frame rate counts the panel's frames: #{fps}")
  cost = get('/v1/diagnostics')['hud']
  check(cost['mean_update_ms'] < 0.5, "an update costs #{cost['mean_update_ms']} ms on average")

  # Off again: what it covered, exactly as before.
  code, reply = hud_settings({ 'enabled' => false, 'corner' => 'top-right' })
  check(code == 200, "hud off again: #{code} #{reply}")
  wait_until('the panel stayed') { get('/v1/diagnostics')['hud']['panels'].empty? }
  sleep 0.6
  again = GateBmp.new(screenshot_bytes)
  check(region(again, x, y, width, height) == region(off, x, y, width, height),
        'the region the panel covered was not repainted')
  check(again.count(BACKGROUND).zero? && again.count(BORDER).zero?, 'panel pixels left behind')

  puts "PERFORMANCE_PANEL_PASS backend=#{backend} default=BLIT_HUD off=absent strict=refused " \
       "on=top-right@#{x},#{y} size=#{width}x#{height} text=psf_exact compositor=\"#{lines[3]}\" " \
       "idle_presents=#{presents} idle_hud_frames=#{hud_frames} fps=#{fps.round(2)} " \
       "update_ms=last:#{cost['last_update_ms']},mean:#{cost['mean_update_ms']},max:#{cost['max_update_ms']} " \
       'off_again=repainted'
end
