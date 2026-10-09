#!/usr/bin/env ruby
# Schema/API regression gate for runtime font selection and logical scaling.
require 'json'
require 'net/http'

port = 18_995
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms',
  'BLIT_PSF_FONT' => 'assets/fonts/Blit-Hibr-Terminus16.psf'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
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

30.times do
  begin
    request(URI("#{base}/v1/version"))
    break
  rescue StandardError
    sleep 0.1
  end
end

topology = request(URI("#{base}/v1/topology"))
pane = topology.fetch('panes').first
raise 'no test pane discovered' unless pane

profiles = request(URI("#{base}/v1/fonts"))
active = profiles.fetch('active')
raise 'unexpected initial scale' unless active.fetch('scale') == 1
available = profiles.fetch('profiles').select { |profile| profile['available'] }
raise 'no available font profile' if available.empty?

session_request = {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'printf ready; sleep 10'],
  'pane_id' => pane.fetch('id')
}
session = request(URI("#{base}/v1/terminal-sessions"), Net::HTTP::Post,
                 session_request)
session_id = session.fetch('id')
begin
  surface_id = session.fetch('surface_id')
  before = request(URI("#{base}/v1/surfaces/#{surface_id}/summary"))
  scaled = request(URI("#{base}/v1/fonts"), Net::HTTP::Put, {'scale' => 2})
  raise 'scale was not applied' unless scaled.fetch('active').fetch('scale') == 2
  raise 'cell width did not double' unless scaled.fetch('active').fetch('cell_pixels').fetch('width') == active.fetch('cell_pixels').fetch('width') * 2
  after = request(URI("#{base}/v1/surfaces/#{surface_id}/summary"))
  raise 'surface was not reflowed' unless after['width'] < before['width'] && after['height'] < before['height']

  selected = available.first.fetch('id')
  selected_state = request(URI("#{base}/v1/fonts"), Net::HTTP::Put,
                           {'font_id' => selected, 'scale' => 1})
  raise 'font profile was not selected' unless selected_state.fetch('active').fetch('id') == selected
  raise 'scale was not restored' unless selected_state.fetch('active').fetch('scale') == 1
ensure
  delete = Net::HTTP::Delete.new(URI("#{base}/v1/terminal-sessions/#{session_id}"))
  Net::HTTP.start(base.hostname, base.port) { |http| http.request(delete) }
end

puts 'font API gate: PASS (profile listing, 2x scale, PTY/pane reflow, profile restore)'
