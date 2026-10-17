#!/usr/bin/env ruby
# Real-host reproduction of #89. One request writes the bottom-right cell
# first and top-left cell last; both must reach scanout. Creates a temporary
# non-Hibr session in a free pane and deletes only that session afterward.
require 'json'
require 'net/http'
require 'uri'

base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN', 'hellomonkeyboy')
def request(base, token, method, path, body = nil)
  uri = base + path
  req = {'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
         'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete,
         'PATCH' => Net::HTTP::Patch}.fetch(method).new(uri)
  req['Authorization'] = "Bearer #{token}"
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  response = Net::HTTP.start(uri.host, uri.port, open_timeout: 4, read_timeout: 30) { |http| http.request(req) }
  raise "#{method} #{path}: #{response.code} #{response.body[0,200]}" unless response.code.start_with?('2')
  response
end
def data(base, token, path)
  JSON.parse(request(base, token, 'GET', path).body)
end
def rgb(bmp, x, y)
  w = bmp[18, 4].unpack1('l<')
  h = bmp[22, 4].unpack1('l<')
  bpp = bmp[28, 2].unpack1('S<') / 8
  raise 'invalid BMP geometry' unless [3,4].include?(bpp) && x >= 0 && y >= 0 && x < w && y < h
  stride = (w * bpp + 3) & ~3
  o = 54 + (h - 1 - y) * stride + x * bpp
  [bmp.getbyte(o + 2), bmp.getbyte(o + 1), bmp.getbyte(o)]
end
topology = data(base, token, '/v1/topology')
focused = data(base, token, '/v1/terminal-sessions').fetch('sessions').find { |s| s['focused'] }
pane = topology.fetch('panes').find { |p| p['surface_id'].nil? }
raise 'requires a free pane' unless pane
grid = pane.fetch('cell_grid')
raise 'requires at least 24x14 cells' unless grid['columns'] >= 24 && grid['rows'] >= 14
display = pane.fetch('display_id')
inset = data(base, token, '/v1/diagnostics').fetch('render').fetch('surface_inset_px')
session = nil
begin
  created = JSON.parse(request(base, token, 'POST', '/v1/terminal-sessions',
    {'command'=>'/bin/sh', 'arguments'=>['-c','sleep 120'], 'pane_id'=>pane.fetch('id')}).body)
  session = created.fetch('id')
  surface = created.fetch('surface_id')
  summary = data(base, token, "/v1/surfaces/#{surface}/summary")
  request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells", {
    'base_revision'=>summary.fetch('revision'), 'cells'=>[
      {'x'=>2,'y'=>1,'text'=>' ', 'style'=>{'background'=>'#102030'}},
      {'x'=>20,'y'=>10,'text'=>' ', 'style'=>{'background'=>'#102030'}}]})
  request(base, token, 'GET', "/v1/diagnostics/screenshots/#{display}")
  summary = data(base, token, "/v1/surfaces/#{surface}/summary")
  update = JSON.parse(request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells", {
    'base_revision'=>summary.fetch('revision'), 'cells'=>[
      {'x'=>20,'y'=>10,'text'=>' ', 'style'=>{'background'=>'#ff0000'}},
      {'x'=>2,'y'=>1,'text'=>' ', 'style'=>{'background'=>'#ff0000'}}]}).body)
  shot = request(base, token, 'GET', "/v1/diagnostics/screenshots/#{display}")
  captured = (shot['X-Blit-Capture-Revisions'] || '').split(',').to_h { |pair| pair.split('=') }
  raise 'capture revision mismatch' unless captured[surface].to_i == update.fetch('revision')
  samples = [[2,1],[20,10]].map do |x,y|
    rgb(shot.body, grid['origin_x'] + inset + x * grid['cell_width'] + 2,
        grid['origin_y'] + inset + y * grid['cell_height'] + 2)
  end
  raise "LIVE_DAMAGE_UNION_FAIL pixels=#{samples.inspect} expected=red,red" unless samples.all? { |p| p == [255,0,0] }
  puts "LIVE_DAMAGE_UNION_PASS revision=#{update['revision']} display=#{display} both-corners=exact"
  summary = data(base, token, "/v1/surfaces/#{surface}/summary")
  painted = JSON.parse(request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells", {
    'base_revision'=>summary.fetch('revision'), 'cells'=>[
      {'x'=>5,'y'=>5,'text'=>"\u2580", 'style'=>{'foreground'=>'#ff0000','background'=>'#102030'}},
      {'x'=>6,'y'=>5,'text'=>"\u2584", 'style'=>{'foreground'=>'#ff0000','background'=>'#102030'}}]}).body)
  shot = request(base, token, 'GET', "/v1/diagnostics/screenshots/#{display}")
  captured = (shot['X-Blit-Capture-Revisions'] || '').split(',').to_h { |pair| pair.split('=') }
  raise 'half-block capture revision mismatch' unless captured[surface].to_i == painted['revision']
  [[5, [255,0,0], [16,32,48]], [6, [16,32,48], [255,0,0]]].each do |column, top, bottom|
    x = grid['origin_x'] + inset + column * grid['cell_width'] + grid['cell_width'] / 2
    y = grid['origin_y'] + inset + 5 * grid['cell_height']
    upper = rgb(shot.body, x, y + grid['cell_height'] / 4)
    lower = rgb(shot.body, x, y + 3 * grid['cell_height'] / 4)
    raise "LIVE_HALF_BLOCK_FAIL column=#{column} top=#{upper} bottom=#{lower}" unless upper == top && lower == bottom
  end
  puts "LIVE_HALF_BLOCK_PASS upper=exact lower=exact foreground=ff0000 background=102030"
ensure
  request(base, token, 'DELETE', "/v1/terminal-sessions/#{session}") if session
  request(base, token, 'PATCH', "/v1/terminal-sessions/#{focused['id']}", {'focused'=>true}) if focused
end
