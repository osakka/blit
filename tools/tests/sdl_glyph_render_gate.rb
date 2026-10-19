#!/usr/bin/env ruby
# frozen_string_literal: true

# Pixel-level regression gate for the bundled Hibr font and SDL atlas path.
# Cell snapshots prove terminal state; this gate proves representative
# geometric glyphs survive the renderer boundary with their intended shape.

require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'
require_relative 'sdl_test_profile'

port = gate_port(Integer(ENV.fetch('BLIT_GLYPH_RENDER_PORT', '19264')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-glyph-render-#{Process.pid}.log"
pid = Process.spawn(SdlTestProfile.environment, SdlTestProfile.binary, port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def request(base, method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  uri = base + path
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                              read_timeout: 3) do |http|
    req = klass.new(uri)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.start_with?('2')
  response.body.nil? || response.body.empty? ? nil : JSON.parse(response.body)
end

Timeout.timeout(8) do
  loop do
    begin
      request(base, 'GET', '/v1/server')
      break
    rescue StandardError
      sleep 0.05
    end
  end
end

# U+2500, U+2502, U+2501, U+2550, U+2588, U+2581, A+combining acute,
# followed by a wide CJK glyph. The first six have deterministic geometry.
glyphs = "─│━═█▁Á界"
SdlTestProfile.verify!(request(base, 'GET', '/v1/diagnostics'))
display_id = SdlTestProfile.display_id!(request(base, 'GET', '/v1/displays'))
session = request(base, 'POST', '/v1/terminal-sessions',
                  'display_id' => display_id,
                  'command' => '/bin/sh',
                  'arguments' => ['-c', "printf '#{glyphs}'; sleep 1"],
                  'surface' => { 'width' => 16, 'height' => 2 })
id = session.fetch('id')
sleep 0.6
bmp = Net::HTTP.get(base + "/v1/diagnostics/screenshots/#{display_id}")

raise 'not a BMP screenshot' unless bmp.byteslice(0, 2) == 'BM'
offset = bmp.byteslice(10, 4).unpack1('V')
width = bmp.byteslice(18, 4).unpack1('l<')
height = bmp.byteslice(22, 4).unpack1('l<')
bits = bmp.byteslice(28, 2).unpack1('v')
raise 'unexpected screenshot geometry' unless width > 0 && height > 0
raise 'unexpected screenshot depth' unless bits == 24 || bits == 32
bpp = bits / 8
stride = ((width * bpp + 3) / 4) * 4

inset = request(base, 'GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')
foreground = lambda do |cell, row, column|
  x = inset + cell * 8 + column
  y = inset + row
  pixel = offset + (height - 1 - y) * stride + x * bpp
  bmp.byteslice(pixel, 3).bytes == [0xbe, 0xd2, 0xbe]
end

row_count = lambda do |cell, row|
  (0...8).count { |column| foreground.call(cell, row, column) }
end
column_count = lambda do |cell, column|
  (0...16).count { |row| foreground.call(cell, row, column) }
end

raise 'light horizontal topology mismatch' unless row_count.call(0, 7) == 8
raise 'light vertical topology mismatch' unless column_count.call(1, 3) == 16
raise 'heavy horizontal topology mismatch' unless
  row_count.call(2, 7) == 8 && row_count.call(2, 8) == 8
raise 'double horizontal topology mismatch' unless
  row_count.call(3, 5) == 8 && row_count.call(3, 10) == 8
raise 'full block topology mismatch' unless (0...16).all? { |row| row_count.call(4, row) == 8 }
raise "lower block topology mismatch rows=#{(0...16).map { |row| row_count.call(5,row) }}" unless
  row_count.call(5, 14) == 8 && row_count.call(5, 15) == 8 &&
  row_count.call(5, 13) == 0
raise 'wide glyph did not paint both cells' unless
  (0...16).any? { |row| row_count.call(7, row) > 0 } &&
  (0...16).any? { |row| row_count.call(8, row) > 0 }

request(base, 'GET', "/v1/terminal-sessions/#{id}")
puts 'SDL_GLYPH_RENDER_PASS light=1 heavy=1 double=1 blocks=1 wide=1'
