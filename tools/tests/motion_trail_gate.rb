#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #29, the question behind it: when content moves, does the renderer
# leave pixels behind that the cell state does not account for?
#
# The live report ("the screensaver leaves part of the shape drawn when it
# animates") could not be settled against the screensaver itself, because a
# full-screen desktop mutating hundreds of times a second is never the same
# frame as a 12 MB capture -- which is what #29 is about. So the motion is
# reproduced here and then *stopped*: with nothing writing to the surface, a
# capture and the cell state describe the same frame by construction, and the
# capture's own headers are checked to confirm it rather than assumed.
#
# A blank cell must be a solid block of its background colour. Anything else
# in that block is a trail.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19255'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)
COLUMNS = 40
ROWS = 20
STEPS = 12

abort("motion-trail gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "trail-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 60) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("motion-trail gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

def capture
  response = request('GET', '/v1/diagnostics/screenshots/display-1')
  raise "capture failed: #{response.code}" unless response.code == '200'

  revisions = {}
  (response['X-Blit-Capture-Revisions'] || '').split(',').each do |pair|
    id, revision = pair.split('=')
    revisions[id] = Integer(revision, 10) if id && revision
  end
  { body: response.body, revisions: revisions,
    presented: response['X-Blit-Capture-Presented'] }
end

def pixel(bmp, x, y)
  stride = (FB_WIDTH * 3 + 3) & ~3
  offset = 54 + (FB_HEIGHT - 1 - y) * stride + x * 3
  [bmp.getbyte(offset + 2), bmp.getbyte(offset + 1), bmp.getbyte(offset)]
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null',
               err: ENV.fetch('BLIT_GATE_SERVER_LOG', '/dev/null'))
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

inset = json('GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')

# An owner-authored surface, so the motion is driven one committed frame at a
# time rather than through a shell's buffering.
created = json('POST', '/v1/terminal-sessions',
               'command' => '/bin/cat', 'display_id' => 'display-1',
               'surface' => { 'width' => COLUMNS, 'height' => ROWS })
surface = created.fetch('surface_id')

# Paint a background everywhere so a blank cell has a colour to be checked
# against, then walk a bright block down the surface, clearing where it was.
# The request buffer is bounded at 8 KiB, which is about eighty styled cells,
# so a surface-sized write goes in chunks -- the same shape Hibr uses when it
# paints a desktop.
CHUNK = 50
def write_cells(surface, cells)
  cells.each_slice(CHUNK) do |slice|
    revision = json('GET', "/v1/surfaces/#{surface}/summary").fetch('revision')
    response = request('PUT', "/v1/surfaces/#{surface}/cells",
                       'base_revision' => revision, 'cells' => slice)
    unless %w[200 204].include?(response.code)
      abort("motion-trail gate: cell write #{response.code} #{response.body}")
    end
  end
end

BACKGROUND = '#102030'
SHAPE_BG = '#d0a040'
blanket = []
(0...ROWS).each do |y|
  (0...COLUMNS).each do |x|
    blanket << { 'x' => x, 'y' => y, 'text' => ' ',
                 'style' => { 'foreground' => '#ffffff',
                              'background' => BACKGROUND } }
  end
end
write_cells(surface, blanket)
sleep 0.6

# Each step erases the previous block back to the background and draws the
# next one, which is exactly the shape of an animation that could leave a
# trail if the renderer repaints less than it clears.
(0...STEPS).each do |step|
  cells = []
  if step.positive?
    (0...6).each do |x|
      cells << { 'x' => x + step - 1, 'y' => step - 1, 'text' => ' ',
                 'style' => { 'foreground' => '#ffffff',
                              'background' => BACKGROUND } }
    end
  end
  (0...6).each do |x|
    cells << { 'x' => x + step, 'y' => step, 'text' => 'O',
               'style' => { 'foreground' => '#000000',
                            'background' => SHAPE_BG } }
  end
  write_cells(surface, cells)
  sleep 0.08
end

# Erase the last block so every cell on the surface is blank again. Any
# non-background pixel from here on is a pixel the state does not account for.
final = (0...6).map do |x|
  { 'x' => x + STEPS - 1, 'y' => STEPS - 1, 'text' => ' ',
    'style' => { 'foreground' => '#ffffff', 'background' => BACKGROUND } }
end
write_cells(surface, final)

# Let it go quiet, then take a sample the server itself agrees is coherent.
sleep 1.5
sample = nil
discarded = 0
10.times do
  shot = capture
  state = json('GET', "/v1/surfaces/#{surface}/cells")
  reported = shot.fetch(:revisions)[surface]
  abort("motion-trail gate: no revision reported for #{surface}") if reported.nil?
  if shot.fetch(:presented) != 'true'
    discarded += 1
    sleep 0.3
    next
  end
  if reported != state.fetch('revision')
    discarded += 1
    sleep 0.3
    next
  end
  sample = [shot, state]
  break
end

if sample.nil?
  warn 'motion-trail gate: no capture agreed with the surface state after ten ' \
       "tries (#{discarded} discarded); nothing is writing to this surface, " \
       'so this is a capture-identity fault rather than skew'
  exit 1
end

shot, state = sample
cursor = state.fetch('cursor', {})
cursor_cell = [cursor.fetch('x', -1), cursor.fetch('y', -1)]
want = BACKGROUND.delete('#').scan(/../).map { |c| c.to_i(16) }
checked = 0
trails = []
grid = {}
state.fetch('cells').each { |c| grid[[c.fetch('x', 0), c.fetch('y', 0)]] = c }
(0...ROWS).each do |y|
  (0...COLUMNS).each do |x|
    cell = grid[[x, y]]
    next if cell.nil?
    next unless (cell['text'] || ' ') == ' '
    # The text cursor legitimately paints the cell it is in.
    next if [x, y] == cursor_cell

    checked += 1
    bad = 0
    (0...CELL_H).each do |dy|
      (0...CELL_W).each do |dx|
        px = inset + x * CELL_W + dx
        py = inset + y * CELL_H + dy
        next if px >= FB_WIDTH || py >= FB_HEIGHT

        bad += 1 if pixel(shot.fetch(:body), px, py) != want
      end
    end
    trails << [x, y, bad] if bad.positive?
  end
end

request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")

if checked < ROWS * COLUMNS / 2
  warn "motion-trail gate: only #{checked} blank cells were checked of " \
       "#{ROWS * COLUMNS}; the fixture did not blank the surface"
  exit 1
end

unless trails.empty?
  warn "motion-trail gate: #{trails.length} blank cells of #{checked} keep " \
       'pixels the state does not account for after the motion stopped: ' \
       "#{trails.first(8).inspect} (worst #{trails.max_by { |t| t[2] }.inspect})"
  exit 1
end

puts "MOTION_TRAIL_PASS blank_cells=#{checked} steps=#{STEPS} " \
     "discarded_samples=#{discarded}"
