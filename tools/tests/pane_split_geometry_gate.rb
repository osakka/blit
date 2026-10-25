#!/usr/bin/env ruby
# frozen_string_literal: true

# API regression gate for display cell geometry and pane-region quantization.
# It deliberately uses a 1.6 font scale against the dummy display so each
# half-region has a cell remainder. The pre-ADR-0075 implementation quantized
# the whole display first, which put a terminal row across the physical split.

require 'json'
require 'net/http'
require 'uri'
require_relative 'gate_server'

port = Integer(ENV.fetch('BLIT_PANE_SPLIT_PORT', '19291'), 10)
env = {
  **gate_display_env,
  
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
inset = request(base, 'GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')
original.fetch('outputs').each do |output|
  grid = output.fetch('cell_grid')
  raise "presentation inset clips complete cells: #{grid.inspect} inset=#{inset}" if
    grid.fetch('origin_x') + inset + grid.fetch('pixel_width') > output.dig('bounds', 'width') ||
    grid.fetch('origin_y') + inset + grid.fetch('pixel_height') > output.dig('bounds', 'height')
end
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
  panes = split.fetch('panes').sort_by { |pane| pane.fetch('pixel_region').fetch('y') }
  raise "expected two panes, got #{panes.length}" unless panes.length == 2
  top = panes.fetch(0).fetch('bounds')
  bottom = panes.fetch(1).fetch('bounds')
  top_region = panes.fetch(0).fetch('pixel_region')
  bottom_region = panes.fetch(1).fetch('pixel_region')
  raise "top region starts at #{top_region.inspect}" unless
    top_region.fetch('x').zero? && top_region.fetch('y').zero?
  raise "regions do not meet: #{top_region.inspect} #{bottom_region.inspect}" unless
    bottom_region.fetch('y') == top_region.fetch('y') + top_region.fetch('height')
  raise "regions do not cover output: #{top_region.inspect} #{bottom_region.inspect}" unless
    bottom_region.fetch('y') + bottom_region.fetch('height') == display_height
  [panes.fetch(0), panes.fetch(1)].each do |pane|
    bounds = pane.fetch('bounds')
    region = pane.fetch('pixel_region')
    pane_grid = pane.fetch('cell_grid')
    raise "pane grid columns mismatch: #{pane.inspect}" unless
      pane_grid.fetch('columns') == bounds.fetch('width')
    raise "pane grid rows mismatch: #{pane.inspect}" unless
      pane_grid.fetch('rows') == bounds.fetch('height')
    raise "pane grid pixel width mismatch: #{pane.inspect}" unless
      pane_grid.fetch('pixel_width') == pane_grid.fetch('columns') * cell_width
    raise "pane grid pixel height mismatch: #{pane.inspect}" unless
      pane_grid.fetch('pixel_height') == pane_grid.fetch('rows') * cell_height
    raise "pane grid origin x mismatch: #{pane.inspect}" unless
      pane_grid.fetch('origin_x') == region.fetch('x') +
        (region.fetch('width') - pane_grid.fetch('pixel_width')) / 2
    raise "pane grid origin y mismatch: #{pane.inspect}" unless
      pane_grid.fetch('origin_y') == region.fetch('y') +
        (region.fetch('height') - pane_grid.fetch('pixel_height')) / 2
    raise "pane grid escapes physical region: #{pane.inspect}" unless
      pane_grid.fetch('origin_x') >= region.fetch('x') &&
      pane_grid.fetch('origin_y') >= region.fetch('y') &&
      pane_grid.fetch('origin_x') + pane_grid.fetch('pixel_width') <=
        region.fetch('x') + region.fetch('width') &&
      pane_grid.fetch('origin_y') + pane_grid.fetch('pixel_height') <=
        region.fetch('y') + region.fetch('height')
  end
  raise "logical pane rows are not equal: #{top.inspect} #{bottom.inspect}" unless
    top.fetch('height') == bottom.fetch('height')

  puts "PANE_SPLIT_GEOMETRY_PASS output_grid=#{grid.fetch('columns')}x#{grid_height} " \
       "pane_grid=#{top.fetch('width')}x#{top.fetch('height')} " \
       "top_origin=#{panes.fetch(0).fetch('cell_grid').fetch('origin_x')}," \
       "#{panes.fetch(0).fetch('cell_grid').fetch('origin_y')} " \
       "bottom_origin=#{panes.fetch(1).fetch('cell_grid').fetch('origin_x')}," \
       "#{panes.fetch(1).fetch('cell_grid').fetch('origin_y')}"
ensure
  current = request(base, 'GET', '/v1/topology')
  request(base, 'PUT', '/v1/topology',
          {'revision' => current.fetch('revision'), 'layout' => original_layout},
          'If-Match' => current.fetch('revision').to_s) if
    current.fetch('layout') != original_layout
end
