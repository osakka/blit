#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #29. Assert that the framebuffer agrees with the canonical cell
# state, and prove the two describe the same frame before believing the
# comparison.
#
# A blank cell must render as a solid block of its background colour.
# Anything else in that block is a pixel the cell state does not account
# for, which is what a rendering trail looks like. The reason this gate
# needs capture identity is that the comparison is otherwise unfalsifiable:
# against a surface that is mutating, pixels read at one instant and state
# read at another disagree for reasons that have nothing to do with the
# renderer, and three such false positives were chased on a live host
# before the headers existed.
#
# The capture reports the revision of every surface composited into it.
# When that revision no longer matches the state that was read, the sample
# is discarded rather than failed. On this fixture nothing writes to the
# surface after the pattern is drawn, so a discard means something
# unexpected is mutating it and the gate says so rather than passing
# quietly.
#
# The cell-to-pixel mapping must apply the surface inset, which this reads
# from diagnostics rather than assuming. Assuming zero made the text cursor
# look like it was rendering four pixels outside its cell, which was filed
# as a defect and was not one.

require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19240'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16

abort("render-state gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "render-state-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

# Capture pixels together with the state identity they correspond to.
def capture
  uri = BASE + '/v1/diagnostics/screenshots/display-1'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 60) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "screenshot failed: #{response.code}" unless response.code == '200'

  revisions = {}
  (response['X-Blit-Capture-Revisions'] || '').split(',').each do |pair|
    id, rev = pair.split('=')
    revisions[id] = Integer(rev, 10) if id && rev
  end
  [response.body, revisions, response['X-Blit-Capture-Frame']]
end

# 24-bit BMP, bottom-up, rows padded to four bytes.
def bmp_pixel(bmp, width, height, x, y)
  gate_pixel(bmp, x, y)
end

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                 },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
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

created = JSON.parse(request('POST', '/v1/terminal-sessions',
                             'command' => '/bin/cat',
                             'display_id' => 'display-1').body)
session = created.fetch('id')
surface = created.fetch('surface_id')

# Draw a known pattern through the API so the surface is authored, static,
# and has blank cells with a non-default background to check against.
cells = []
(0...6).each do |row|
  (0...10).each do |column|
    text = row.even? ? 'X' : ' '
    # Colours belong under `style`; the server ignores a cell that carries
    # them at the top level, which left this gate asserting that blank cells
    # are black on a screen that was already black.
    cells << { 'x' => column, 'y' => row, 'text' => text,
               'style' => { 'foreground' => '#ffffff',
                            'background' => '#0000ff' } }
  end
end
snapshot = JSON.parse(request('GET', "/v1/surfaces/#{surface}/summary").body)
put = request('PUT', "/v1/surfaces/#{surface}/cells",
              'base_revision' => snapshot.fetch('revision'), 'cells' => cells)
abort("render-state gate: cell write failed #{put.code} #{put.body}") unless
  %w[200 204].include?(put.code)

sleep 1.5

attempts = 0
result = nil
while attempts < 5 && result.nil?
  attempts += 1
  bmp, revisions, frame = capture
  state = JSON.parse(request('GET', "/v1/surfaces/#{surface}/cells").body)
  captured = revisions[surface]
  if captured.nil?
    abort('render-state gate: the capture reported no revision for the surface')
  end
  # The comparison is only meaningful if the state did not move under it.
  next unless captured == state.fetch('revision')

  width = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
  height = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)
  # A surface is drawn inset from its pane origin, so a cell's pixels are
  # NOT at (x*cell_width, y*cell_height). Read the inset rather than
  # assuming zero: assuming zero is what made the renderer look wrong when
  # this gate was first written.
  inset = JSON.parse(request('GET', '/v1/diagnostics').body)
              .fetch('render').fetch('surface_inset_px')
  grid = {}
  state.fetch('cells').each { |c| grid[[c.fetch('x', 0), c.fetch('y', 0)]] = c }
  mismatched = []
  checked = 0
  (0...6).each do |y|
    (0...10).each do |x|
      cell = grid[[x, y]]
      next if cell.nil?
      next unless (cell['text'] || ' ') == ' '

      style = cell['style'] || {}
      if style['background'].nil?
        # A blank cell with no background of its own proves nothing here, and
        # silently defaulting it to black is how this gate came to pass
        # vacuously.
        next
      end
      hex = style['background'].delete('#')
      want = [hex[0, 2].to_i(16), hex[2, 2].to_i(16), hex[4, 2].to_i(16)]
      checked += 1
      bad = 0
      ((inset + y * CELL_H)...(inset + (y + 1) * CELL_H)).each do |py|
        ((inset + x * CELL_W)...(inset + (x + 1) * CELL_W)).each do |px|
          next if py >= height || px >= width

          bad += 1 if bmp_pixel(bmp, width, height, px, py) != want
        end
      end
      mismatched << [x, y, bad] if bad.positive?
    end
  end
  result = [checked, mismatched, frame, captured]
end

if result.nil?
  abort('render-state gate: the surface moved during every capture; nothing ' \
        'writes to it in this fixture, so this is a fault rather than skew')
end

checked, mismatched, frame, revision = result
request('DELETE', "/v1/terminal-sessions/#{session}")

unless mismatched.empty?
  warn "render-state gate: #{mismatched.length} blank cells of #{checked} " \
       "render content the state does not have: #{mismatched.first(8).inspect}"
  exit 1
end

puts "RENDER_STATE_AGREEMENT_PASS blank_cells=#{checked} " \
     "frame=#{frame} revision=#{revision} attempts=#{attempts}"
