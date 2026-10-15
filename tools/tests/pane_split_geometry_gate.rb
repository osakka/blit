#!/usr/bin/env ruby
# frozen_string_literal: true

# API regression gate for odd terminal-grid partitions. It deliberately uses
# a 1.6 font scale against the dummy display so its quantized height is odd;
# the pre-ADR-0075 implementation left the last row unowned.

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
  cell_height = fonts.fetch('active').fetch('cell_pixels').fetch('height')
  display_height = request(base, 'GET', '/v1/topology').fetch('outputs').first
                       .fetch('bounds').fetch('height')
  grid_height = display_height / cell_height
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

  puts "PANE_SPLIT_GEOMETRY_PASS grid_rows=#{grid_height} " \
       "top_rows=#{top.fetch('height')} bottom_rows=#{bottom.fetch('height')}"
ensure
  current = request(base, 'GET', '/v1/topology')
  request(base, 'PUT', '/v1/topology',
          {'revision' => current.fetch('revision'), 'layout' => original_layout},
          'If-Match' => current.fetch('revision').to_s) if
    current.fetch('layout') != original_layout
end
