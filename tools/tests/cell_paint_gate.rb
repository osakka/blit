#!/usr/bin/env ruby
# frozen_string_literal: true

# The cell-painting contract: what the canonical cell state says a cell is
# must be what the renderer puts in that cell's pixels. Each case here is a
# place where the two renderers were each deciding for themselves.
#
#   #41 a block cursor is an inverse cell, not an eraser
#
# Captures use ?cached=1 deliberately: a fresh capture invalidates the output
# it inspects and can answer from a frame that was never presented (#45).

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

ROOT = File.expand_path('../..', __dir__)
PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '19245'), 10)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)
DEFAULT_FOREGROUND = 0xbed2be

abort("cell-paint gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "paint-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def screenshot
  response = request('GET', '/v1/diagnostics/screenshots/display-1?cached=1')
  raise "screenshot failed: #{response.code}" unless response.code == '200'

  response.body
end

def pixel(bmp, x, y)
  stride = (FB_WIDTH * 3 + 3) & ~3
  offset = 54 + (FB_HEIGHT - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

# Every pixel of one cell, as {[dx, dy] => colour}.
def cell(bmp, inset, column, row)
  map = {}
  (0...CELL_H).each do |dy|
    (0...CELL_W).each do |dx|
      map[[dx, dy]] = pixel(bmp, inset + column * CELL_W + dx,
                            inset + row * CELL_H + dy)
    end
  end
  map
end

def session(script, environment = {})
  body = { 'command' => '/bin/sh', 'arguments' => ['-c', script],
           'display_id' => 'display-1',
           'surface' => { 'width' => 40, 'height' => 10 } }
  body['environment'] = environment unless environment.empty?
  response = request('POST', '/v1/terminal-sessions', body)
  unless response.code.start_with?('2')
    abort("cell-paint gate: session create failed #{response.code} #{response.body}")
  end
  JSON.parse(response.body).fetch('id')
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
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

inset = JSON.parse(request('GET', '/v1/diagnostics').body)
             .fetch('render').fetch('surface_inset_px')
failures = []

# #41. Draw an X with the cursor parked on the next row, then move the cursor
# onto the X. The glyph must survive, in a second colour, in exactly the same
# pixels it occupied before.
id = session("printf 'X\\n'; sleep 2.5; printf '\\033[1;1H'; sleep 60")
sleep 1.5
without_cursor = cell(screenshot, inset, 0, 0)
sleep 2.5
with_cursor = cell(screenshot, inset, 0, 0)
request('DELETE', "/v1/terminal-sessions/#{id}")

glyph = without_cursor.select { |_, colour| colour != 0 }.keys.sort
block = with_cursor.select { |_, colour| colour == DEFAULT_FOREGROUND }.keys
inverted = with_cursor.reject { |_, colour| colour == DEFAULT_FOREGROUND }.keys.sort

if glyph.length < 10
  failures << "the reference glyph has only #{glyph.length} lit pixels; " \
              'the gate is reading the wrong cell'
elsif block.empty?
  failures << 'the cursor cell has no cursor-coloured pixels at all'
elsif inverted.empty?
  failures << "the block cursor covered the whole cell: #{block.length} of " \
              "#{CELL_W * CELL_H} pixels are the cursor colour and the " \
              "#{glyph.length}-pixel glyph under it is gone"
elsif inverted != glyph
  failures << 'the glyph under the block cursor is not the glyph that was ' \
              "there: #{glyph.length} pixels before, #{inverted.length} " \
              'inverted under the cursor'
end

unless failures.empty?
  failures.each { |f| warn "cell-paint gate: #{f}" }
  exit 1
end

puts "CELL_PAINT_PASS block_cursor_keeps_glyph=#{glyph.length}px"
