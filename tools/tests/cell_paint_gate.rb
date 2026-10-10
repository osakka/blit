#!/usr/bin/env ruby
# frozen_string_literal: true

# The cell-painting contract: what the canonical cell state says a cell is
# must be what the renderer puts in that cell's pixels. Each case here is a
# place where the two renderers were each deciding for themselves.
#
#   #41 a block cursor is an inverse cell, not an eraser
#   #42 italic is a slant, and is not bold
#   #40 an unstyled cell uses the session's DT_INK/DT_FACE pair
#   #49 a bold glyph stays inside its own cell
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
            'PUT' => Net::HTTP::Put,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "paint-#{rand(1 << 30)}" if %w[POST PUT].include?(method)
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

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("cell-paint gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
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

# #42. Bold, italic and plain are three different things on screen. Italic
# used to be drawn as a second strike one pixel over, which is what bold is,
# so the two were pixel-identical.
id = session("printf '\\033[1mX\\033[0m \\033[3mX\\033[0m X\\n'; sleep 60")
sleep 2.0
attributes = screenshot
request('DELETE', "/v1/terminal-sessions/#{id}")
shapes = { 'bold' => 0, 'italic' => 2, 'plain' => 4 }.transform_values do |column|
  cell(attributes, inset, column, 0).select { |_, colour| colour != 0 }.keys.sort
end
shapes.each do |name, lit|
  failures << "the #{name} cell has only #{lit.length} lit pixels" if lit.length < 10
end
[%w[bold italic], %w[bold plain], %w[italic plain]].each do |a, b|
  next unless shapes[a] == shapes[b]

  failures << "#{a} and #{b} render pixel-identically " \
              "(#{shapes[a].length} lit pixels each)"
end

# #40. The session's default pair is what an unstyled cell is painted with.
# It used to reach only the OSC 10/11 answer, so an application was told one
# colour and shown another.
id = session("printf 'X'; sleep 60", 'DT_INK' => '#ff0000')
sleep 2.0
ink = cell(screenshot, inset, 0, 0).values.tally
request('DELETE', "/v1/terminal-sessions/#{id}")
unless ink.fetch(0xff0000, 0) >= 10
  failures << "DT_INK=#ff0000 painted no red glyph pixels: #{ink.inspect}"
end

id = session('sleep 60', 'DT_FACE' => '#0000ff')
sleep 2.0
face = cell(screenshot, inset, 5, 5).values.tally
request('DELETE', "/v1/terminal-sessions/#{id}")
unless face.fetch(0x0000ff, 0) == CELL_W * CELL_H
  failures << "DT_FACE=#0000ff did not paint a blank cell blue: #{face.inspect}"
end

# OSC 10 is also a set, not only a query: a client that changes the default
# repaints every unstyled cell.
id = session("printf '\\033]10;rgb:0000/ff00/0000\\033\\\\X'; sleep 60")
sleep 2.0
osc = cell(screenshot, inset, 0, 0).values.tally
request('DELETE', "/v1/terminal-sessions/#{id}")
unless osc.fetch(0x00ff00, 0) >= 10
  failures << "OSC 10 set to green painted no green glyph pixels: #{osc.inspect}"
end

# #49. Bold is an overstrike one pixel to the right, so a glyph that reaches
# its own right edge used to put a pixel in the next cell. A box-drawing
# glyph spans the full cell width, which is what makes this visible at all.
holder = json('POST', '/v1/terminal-sessions',
              'command' => '/bin/cat', 'display_id' => 'display-1',
              'surface' => { 'width' => 20, 'height' => 4 })
bold_surface = holder.fetch('surface_id')
revision = json('GET', "/v1/surfaces/#{bold_surface}/summary").fetch('revision')
written = request('PUT', "/v1/surfaces/#{bold_surface}/cells",
                  'base_revision' => revision,
                  'cells' => [
                    { 'x' => 0, 'y' => 2, 'text' => "\u2500",
                      'style' => { 'foreground' => '#ffffff',
                                   'background' => '#204060', 'bold' => true } },
                    { 'x' => 1, 'y' => 2, 'text' => ' ',
                      'style' => { 'foreground' => '#ffffff',
                                   'background' => '#204060' } }
                  ])
unless %w[200 204].include?(written.code)
  abort("cell-paint gate: bold fixture write #{written.code} #{written.body}")
end
sleep 2.0
bold_shot = screenshot
drawn = cell(bold_shot, inset, 0, 2).values.count { |c| c != 0x204060 }
spill = cell(bold_shot, inset, 1, 2).values.tally.reject { |c, _| c == 0x204060 }
request('DELETE', "/v1/terminal-sessions/#{holder.fetch('id')}")
if drawn < 4
  failures << "the bold fixture drew only #{drawn} glyph pixels; the font has " \
              'no full-width glyph at U+2500 and this case proves nothing'
elsif !spill.empty?
  failures << "a bold glyph put #{spill.values.sum} pixels into the blank " \
              "cell beside it: #{spill.inspect}"
end

unless failures.empty?
  failures.each { |f| warn "cell-paint gate: #{f}" }
  exit 1
end

puts "CELL_PAINT_PASS block_cursor_keeps_glyph=#{glyph.length}px " \
     "bold=#{shapes['bold'].length}px italic=#{shapes['italic'].length}px " \
     "plain=#{shapes['plain'].length}px session_defaults=ink,face,osc10 " \
     "bold_stays_in_cell=#{drawn}px_drawn_0_spilled"
