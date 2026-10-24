#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0061, issues #38 and #39. A surface's pixel origin must be the same
# number for the renderer that draws it and the hit test that maps a pointer
# into it. Two things were wrong: the hit test left out the four-pixel inset
# that both renderers draw with, and a surface with no pane was placed by
# three different rules.
#
# The pointer arrives through POST /v1/input/inject, and the session under
# test echoes what it receives with `cat -v`, so the mouse report the server
# wrote to the PTY comes back as ordinary cells and can be read through the
# API. That makes this gate independent of the capture paths in #45.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19209'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
INSET = 0
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)

abort("pointer-geometry gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "geom-#{rand(1 << 30)}" if %w[POST PUT PATCH].include?(method)
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("pointer-geometry gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

def pixel(bmp, x, y)
  gate_pixel_rgb24(bmp, x, y)
end

def lit_in_cell(bmp, origin_x, origin_y)
  count = 0
  (0...CELL_H).each do |dy|
    (0...CELL_W).each do |dx|
      count += 1 if pixel(bmp, origin_x + dx, origin_y + dy) != 0
    end
  end
  count
end

def row_text(surface_id, row)
  cells = json('GET', "/v1/surfaces/#{surface_id}/cells").fetch('cells')
  cells.select { |c| c.fetch('y', 0) == row }
       .sort_by { |c| c.fetch('x', 0) }
       .map { |c| c.fetch('text', ' ') }.join
end

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
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

failures = []

# Two panes side by side, so an omitted pane origin cannot hide behind zero.
topology = json('PUT', '/v1/topology', 'layout' => { 'columns' => 2, 'rows' => 1 })
pane = topology.fetch('panes').find { |p| p.dig('bounds', 'x').to_i > 0 }
abort('pointer-geometry gate: the second pane was not created') if pane.nil?
pane_x = pane.dig('bounds', 'x') * CELL_W
pane_y = pane.dig('bounds', 'y') * CELL_H

# The session reports SGR mouse events and echoes them back as text.
paned = json('POST', '/v1/terminal-sessions',
             'command' => '/bin/sh',
             'arguments' => ['-c', "printf '\\033[?1000h\\033[?1006h'; cat -v"],
             'display_id' => 'display-1')
json('PATCH', "/v1/terminal-sessions/#{paned.fetch('id')}",
     'pane_id' => pane.fetch('id'))
sleep 2.0

# Six pixels into the surface's first cell, and fourteen into its first row.
# These positions must remain in local cell (0,0), with no extra margin.
request('POST', '/v1/input/inject', 'kind' => 'pointer_motion',
        'x' => pane_x + INSET + 6, 'y' => pane_y + INSET + 14)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => true)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => false)

report = nil
Timeout.timeout(6) do
  loop do
    text = row_text(paned.fetch('surface_id'), 0)
    report = text.match(/<0;(\d+);(\d+)M/)
    break if report

    sleep 0.2
  end
rescue Timeout::Error
  nil
end

if report.nil?
  failures << 'the session received no SGR mouse report at all for a click ' \
              'inside its pane'
else
  column = report[1].to_i
  row = report[2].to_i
  unless column == 1 && row == 1
    failures << "a click six pixels into the pane's first cell reported " \
                "column #{column} row #{row}, not column 1 row 1: the hit " \
                'test is not using the origin the renderer draws at'
  end
end

# #39. A surface with no pane is placed from the display's own corner, not
# stacked below the panes. The renderer used to stack it below every surface
# on the output, including paned ones, while the hit test did not.
plain = json('POST', '/v1/terminal-sessions',
             'command' => '/bin/sh',
             'arguments' => ['-c', "printf 'Z'; sleep 60"],
             'display_id' => 'display-1')
sleep 2.0
capture = request('GET', '/v1/diagnostics/screenshots/display-1?cached=1')
abort("pointer-geometry gate: screenshot #{capture.code}") unless capture.code == '200'
at_inset = lit_in_cell(capture.body, INSET, INSET)
if at_inset < 5
  stacked_y = INSET + pane.dig('bounds', 'height') * CELL_H + INSET
  failures << 'a pane-less surface was not drawn at the display corner: ' \
              "#{at_inset} lit pixels at y=#{INSET}. Stacking it below the " \
              "panes would put it at y=#{stacked_y}" +
              (stacked_y >= FB_HEIGHT ? ', which is off the display entirely' : '')
end

request('DELETE', "/v1/terminal-sessions/#{paned.fetch('id')}")
request('DELETE', "/v1/terminal-sessions/#{plain.fetch('id')}")

unless failures.empty?
  failures.each { |f| warn "pointer-geometry gate: #{f}" }
  exit 1
end

puts 'POINTER_GEOMETRY_PASS inset_honoured=yes pane_less_at_corner=yes'
