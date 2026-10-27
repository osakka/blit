#!/usr/bin/env ruby
# Schema/API regression gate for runtime font selection and logical scaling.
require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'
require_relative 'test_profile'
require_relative 'gate_bmp'

port = gate_port(Integer(ENV.fetch('BLIT_FONT_API_PORT','18995')))
# The profiles are Blit's own fonts, from the repository (ADR 0194): the
# console fonts this used to find installed on the host are retired.
env = TestProfile.environment.merge('BLIT_FONT_ROOT' => File.expand_path('../../assets/fonts', __dir__))
pid = Process.spawn(env, TestProfile.binary, port.to_s,
                    out: '/tmp/blit-font-api-gate.log', err: [:child, :out])
at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

base = URI("http://127.0.0.1:#{port}")
def request(uri, method = Net::HTTP::Get, payload = nil)
  request = method.new(uri)
  request['Content-Type'] = 'application/json' if payload
  request.body = JSON.generate(payload) if payload
  response = Net::HTTP.start(uri.hostname, uri.port) { |http| http.request(request) }
  raise "HTTP #{response.code}: #{response.body}" unless response.code.to_i.between?(200, 299)
  JSON.parse(response.body)
end

def screenshot(base)
  response = Net::HTTP.start(base.hostname, base.port) do |http|
    http.request(Net::HTTP::Get.new(base + '/v1/diagnostics/screenshots/display-1'))
  end
  raise "screenshot failed: #{response.code}" unless response.code == '200'
  response.body
end

def bmp_pixel(bmp, x, y)
  gate_pixel(bmp, x, y)
end

def solid_rect?(bmp, x, y, width, height, colour)
  (y...(y + height)).all? do |py|
    (x...(x + width)).all? { |px| bmp_pixel(bmp, px, py) == colour }
  end
end

def wait_solid(base, x, y, width, height, colour)
  20.times do
    return true if solid_rect?(screenshot(base), x, y, width, height, colour)
    sleep 0.05
  end
  false
end

30.times do
  begin
    request(URI("#{base}/v1/version"))
    break
  rescue StandardError
    sleep 0.1
  end
end

topology = request(URI("#{base}/v1/topology"))
TestProfile.verify!(request(base + '/v1/diagnostics'))
pane = topology.fetch('panes').first
raise 'no test pane discovered' unless pane

profiles = request(URI("#{base}/v1/fonts"))
active = profiles.fetch('active')
raise 'unexpected initial scale' unless active.fetch('scale') == 1
available = profiles.fetch('profiles').select { |profile| profile['available'] }
raise 'no available font profile' if available.empty?

session_request = {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'printf ready; sleep 60'],
  'display_id' => 'display-1',
  'surface' => { 'width' => 128, 'height' => 48 }
}
session = request(URI("#{base}/v1/terminal-sessions"), Net::HTTP::Post,
                 session_request)
session_id = session.fetch('id')
begin
  surface_id = session.fetch('surface_id')
  # Wait for the child's actual output, not a timing delay. This both parks
  # its cursor away from the glyph oracle and ends the competing PTY write
  # before the revisioned API update below.
  Timeout.timeout(8) do
    loop do
      snapshot = request(URI("#{base}/v1/surfaces/#{surface_id}/cells"))
      marker = snapshot.fetch('cells').select { |cell| cell['y'] == 0 && cell['x'] < 5 }
                       .sort_by { |cell| cell['x'] }.map { |cell| cell['text'] }.join
      break if marker == 'ready'
      sleep 0.01
    end
  end
  before = request(URI("#{base}/v1/surfaces/#{surface_id}/summary"))
  cells = {
    'base_revision' => before.fetch('revision'),
    'cells' => [{ 'x' => 0, 'y' => 0, 'text' => '█',
                  'style' => { 'foreground' => '#ffffff',
                               'background' => '#000000' } }]
  }
  request(URI("#{base}/v1/surfaces/#{surface_id}/cells"), Net::HTTP::Put,
          cells)
  inset = request(URI("#{base}/v1/diagnostics")).fetch('render').fetch('surface_inset_px')
  raise 'scale-1 glyph geometry did not render' unless
    wait_solid(base, inset, inset, 8, 16, [255, 255, 255])
  scales = (10..30).map { |tenths| tenths / 10.0 }
  representative_scales = [1.0, 1.1, 2.0, 2.3, 3.0]
  scales.each do |scale|
    scaled = request(URI("#{base}/v1/fonts"), Net::HTTP::Put,
                     {'scale' => scale})
    actual = scaled.fetch('active')
    expected_width = (8 * (scale * 10).round + 5) / 10
    expected_height = (16 * (scale * 10).round + 5) / 10
    raise "scale #{scale} was not applied" unless
      (actual.fetch('scale') - scale).abs < 0.0001
    raise "scale #{scale} width mismatch" unless
      actual.fetch('cell_pixels').fetch('width') == expected_width
    raise "scale #{scale} height mismatch" unless
      actual.fetch('cell_pixels').fetch('height') == expected_height
    after = request(URI("#{base}/v1/surfaces/#{surface_id}/summary"))
    raise "scale #{scale} did not reflow surface" unless
      after['width'] <= before['width'] && after['height'] <= before['height']
    if representative_scales.include?(scale)
      grid = request(URI("#{base}/v1/topology")).fetch('outputs').first
        .fetch('cell_grid')
      expected_x = grid.fetch('origin_x') + inset
      expected_y = grid.fetch('origin_y') + inset
      current = request(URI("#{base}/v1/surfaces/#{surface_id}/summary"))
      request(URI("#{base}/v1/surfaces/#{surface_id}/cells"), Net::HTTP::Put,
              {'base_revision' => current.fetch('revision'),
               'cells' => [{ 'x' => 0, 'y' => 0, 'text' => '█',
                             'style' => { 'foreground' => '#ffffff',
                                          'background' => '#000000' } }]})
      unless wait_solid(base, expected_x, expected_y,
                        expected_width, expected_height,
                        [255, 255, 255])
        raise "scale #{scale} glyph geometry did not render at " \
          "#{expected_x},#{expected_y}"
      end
    end
  end

  [0.9, 1.05, 3.1].each do |invalid_scale|
    invalid = Net::HTTP::Put.new(URI("#{base}/v1/fonts"))
    invalid['Content-Type'] = 'application/json'
    invalid.body = JSON.generate({'scale' => invalid_scale})
    response = Net::HTTP.start(base.hostname, base.port) { |http| http.request(invalid) }
    raise "invalid scale #{invalid_scale} was accepted" unless response.code == '400'
    problem = JSON.parse(response.body)
    raise "invalid scale #{invalid_scale} has wrong code" unless
      problem.fetch('code') == 'invalid_font_scale'
  end

  selected = available.first.fetch('id')

  [8, 16, 17, 19, 23, 32, 48, 64].each do |size|
    actual = request(base + '/v1/fonts', Net::HTTP::Put, {'size_px'=>size}).fetch('active')
    width = (8 * size + 8) / 16
    raise "pixel height #{size} not applied" unless actual['size_px'] == size &&
      actual['sizing'] == 'pixels' && actual['cell_pixels'] == {'width'=>width,'height'=>size}
    grid = request(base + '/v1/topology').fetch('outputs').first.fetch('cell_grid')
    current = request(base + "/v1/surfaces/#{surface_id}/summary")
    request(base + "/v1/surfaces/#{surface_id}/cells", Net::HTTP::Put,
      {'base_revision'=>current.fetch('revision'),'cells'=>[{'x'=>0,'y'=>0,'text'=>'█',
      'style'=>{'foreground'=>'#ffffff','background'=>'#000000'}}]})
    raise "pixel size #{size} glyph not rendered" unless wait_solid(base,
      grid.fetch('origin_x')+inset,grid.fetch('origin_y')+inset,width,size,[255,255,255])
  end
  unchanged = request(base + '/v1/fonts').fetch('active')
  same_profile = request(base + '/v1/fonts',Net::HTTP::Put,{'font_id'=>selected}).fetch('active')
  raise 'profile-only selection reset pixel size policy' unless same_profile['id']==selected &&
    same_profile['sizing']=='pixels' && same_profile['size_px']==unchanged['size_px']
  unchanged = same_profile
  [{'size_px'=>7},{'size_px'=>65},{'size_px'=>19.5},{'size_px'=>'20'},
   {'size_px'=>nil},{'size_px'=>16,'scale'=>1.0}].each do |payload|
    q = Net::HTTP::Put.new(base + '/v1/fonts');q['Content-Type']='application/json';q.body=JSON.generate(payload)
    result = Net::HTTP.start(base.hostname,base.port) { |http| http.request(q) }
    raise "bad pixel size accepted: #{payload}" unless result.code == '400'
    raise 'rejection changed font state' unless request(base + '/v1/fonts').fetch('active') == unchanged
  end
  ['{"size_px":19,"size_px":20}', '{"scale":1.0,"scale":2.0}'].each do |body|
    q=Net::HTTP::Put.new(base+'/v1/fonts');q['Content-Type']='application/json';q.body=body
    response=Net::HTTP.start(base.hostname,base.port){|http|http.request(q)}
    raise 'duplicate size control accepted' unless response.code=='400'
    raise 'duplicate changed font state' unless request(base+'/v1/fonts').fetch('active')==unchanged
  end
  restored = request(base + '/v1/fonts',Net::HTTP::Put,{'scale'=>1.0}).fetch('active')
  raise 'legacy scale did not replace pixel policy' unless restored['sizing']=='scale' && restored['size_px']==16
ensure
  delete = Net::HTTP::Delete.new(URI("#{base}/v1/terminal-sessions/#{session_id}"))
  Net::HTTP.start(base.hostname, base.port) { |http| http.request(delete) }
end

puts 'font API gate: PASS (profiles, legacy scales, pixel heights 8..64, rendered pixels, reflow, rejection, restore)'
