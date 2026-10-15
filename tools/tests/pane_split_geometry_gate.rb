#!/usr/bin/env ruby
# frozen_string_literal: true

# API regression gate for display cell geometry and odd terminal-grid
# partitions. It deliberately uses a 1.6 font scale against the dummy display
# so the display has quantized remainders and an odd cell height; the
# pre-ADR-0076 implementation left the grid one-sided, while the pre-ADR-0075
# implementation left the last row unowned.

require 'json'
require 'net/http'
require 'uri'

port = Integer(ENV.fetch('BLIT_PANE_SPLIT_PORT', '19291'), 10)
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms',
  'BLIT_PSF_FONT' => 'assets/fonts/Blit-Hibr-Terminus16.psf'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: '/tmp/blit-pane-split-gate.log', err: [:child, :out])
at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

base = URI("http://127.0.0.1:#{port}")

def request(base, method, path, body = nil, headers = {})
  klass = {
    'GET' => Net::HTTP::Get,
    'PUT' => Net::HTTP::Put
  }.fetch(method)
  uri = base + path
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port) { |http| http.request(req) }
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.to_i.between?(200, 299)
  response.body.empty? ? nil : JSON.parse(response.body)
end

40.times do
  begin
    request(base, 'GET', '/v1/version')
    break
  rescue StandardError
    sleep 0.1
  end
end

original = request(base, 'GET', '/v1/topology')
original_layout = original.fetch('layout')
begin
  fonts = request(base, 'PUT', '/v1/fonts', {'scale' => 1.6})
  cell_width = fonts.fetch('active').fetch('cell_pixels').fetch('width')
  cell_height = fonts.fetch('active').fetch('cell_pixels').fetch('height')
  output = request(base, 'GET', '/v1/topology').fetch('outputs').first
  display_width = output.fetch('bounds').fetch('width')
  display_height = output.fetch('bounds').fetch('height')
  grid = output.fetch('cell_grid')
  expected_columns = display_width / cell_width
  grid_height = display_height / cell_height
  raise "grid columns mismatch: #{grid.inspect}" unless
    grid.fetch('columns') == expected_columns
  raise "grid rows mismatch: #{grid.inspect}" unless
    grid.fetch('rows') == grid_height
  expected_pixel_width = expected_columns * cell_width
  expected_pixel_height = grid_height * cell_height
  expected_remainder_x = display_width - expected_pixel_width
  expected_remainder_y = display_height - expected_pixel_height
  raise "grid pixel width mismatch: #{grid.inspect}" unless
    grid.fetch('pixel_width') == expected_pixel_width
  raise "grid pixel height mismatch: #{grid.inspect}" unless
    grid.fetch('pixel_height') == expected_pixel_height
  raise "grid origin x is not centred: #{grid.inspect}" unless
    grid.fetch('origin_x') == expected_remainder_x / 2
  raise "grid origin y is not centred: #{grid.inspect}" unless
    grid.fetch('origin_y') == expected_remainder_y / 2
  raise "grid remainder x mismatch: #{grid.inspect}" unless
    grid.fetch('remainder_x') == expected_remainder_x
  raise "grid remainder y mismatch: #{grid.inspect}" unless
    grid.fetch('remainder_y') == expected_remainder_y
  raise "test grid is not odd: #{display_height}/#{cell_height}=#{grid_height}" if
    grid_height.even?

  current = request(base, 'GET', '/v1/topology')
  split = request(base, 'PUT', '/v1/topology',
                  {'revision' => current.fetch('revision'),
                   'layout' => {'columns' => 1, 'rows' => 2}},
                  'If-Match' => current.fetch('revision').to_s)
  panes = split.fetch('panes').sort_by { |pane| pane.fetch('bounds').fetch('y') }
  raise "expected two panes, got #{panes.length}" unless panes.length == 2
  top = panes.fetch(0).fetch('bounds')
  bottom = panes.fetch(1).fetch('bounds')
  raise "top starts at #{top.fetch('y')}" unless top.fetch('y').zero?
  raise "bottom does not meet top: #{top.inspect} #{bottom.inspect}" unless
    bottom.fetch('y') == top.fetch('y') + top.fetch('height')
  raise "partition lost rows: #{top.inspect} #{bottom.inspect}" unless
    bottom.fetch('y') + bottom.fetch('height') == grid_height
  raise "remainder was not assigned to bottom: #{top.inspect} #{bottom.inspect}" unless
    bottom.fetch('height') == top.fetch('height') + 1

  puts "PANE_SPLIT_GEOMETRY_PASS grid=#{grid.fetch('columns')}x#{grid_height} " \
       "origin=#{grid.fetch('origin_x')},#{grid.fetch('origin_y')} " \
       "top_rows=#{top.fetch('height')} bottom_rows=#{bottom.fetch('height')}"
ensure
  current = request(base, 'GET', '/v1/topology')
  request(base, 'PUT', '/v1/topology',
          {'revision' => current.fetch('revision'), 'layout' => original_layout},
          'If-Match' => current.fetch('revision').to_s) if
    current.fetch('layout') != original_layout
end
