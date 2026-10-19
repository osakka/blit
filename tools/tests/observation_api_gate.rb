#!/usr/bin/env ruby
# frozen_string_literal: true

# The observation endpoint is intentionally tested as a contract and as an
# aggregate: it must be valid JSON, contain every declared state plane, and
# agree with the focused identity/session resources it composes.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19251'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("observation API gate: missing #{BINARY}") unless File.executable?(BINARY)

def get(path)
  uri = BASE + path
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "GET #{path} -> #{response.code}: #{response.body}" unless
    response.code == '200'
  JSON.parse(response.body)
end

def post(path, payload)
  uri = BASE + path
  request = Net::HTTP::Post.new(uri)
  request['Content-Type'] = 'application/json'
  request.body = JSON.generate(payload)
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(request)
  end
  raise "POST #{path} -> #{response.code}: #{response.body}" unless
    response.code == '201'
  JSON.parse(response.body)
end

def delete(path)
  uri = BASE + path
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Delete.new(uri))
  end
  raise "DELETE #{path} -> #{response.code}: #{response.body}" unless
    response.code == '204'
end

server = Process.spawn({
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'BLIT_LOGIN_AUTOSTART' => '0',
  'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PSF_FONT' => FONT,
  'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms')
}, BINARY, PORT.to_s, out: '/tmp/blit-observation-api-gate.log', err: [:child, :out])

at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if get('/v1/version')['api_version'] == '0.1'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

version = get('/v1/version')
server_details = get('/v1/server')
sessions = get('/v1/terminal-sessions')
created = post('/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'printf observation; sleep 10'],
  'display_id' => 'display-1',
  'surface' => { 'width' => 80, 'height' => 24 }
})
begin
  observation = get('/v1/diagnostics/observation')
ensure
  delete("/v1/terminal-sessions/#{created.fetch('id')}")
end

required = %w[observation_version captured_monotonic_us consistency version
              server diagnostics runtime graphics displays audio audio_cards
              input_devices input_routes topology fonts settings login sessions
              effects overlays]
missing = required.reject { |key| observation.key?(key) }
raise "observation missing #{missing.inspect}" unless missing.empty?
raise 'unexpected observation version' unless observation['observation_version'] == 1
raise 'observation timestamp is not monotonic' unless
  observation['captured_monotonic_us'].is_a?(Integer) &&
  observation['captured_monotonic_us'] >= 0
raise 'observation consistency marker changed' unless
  observation['consistency'] == 'single_event_loop_turn'
raise 'observation version disagrees with focused endpoint' unless
  observation['version'] == version
raise 'observation server identity disagrees with focused endpoint' unless
  observation['server']['server_version'] == server_details['server_version'] &&
  observation['server']['api_version'] == server_details['api_version']
raise 'observation link is missing from server details' unless
  observation['server']['resources']['observation'] ==
  '/v1/diagnostics/observation'
raise 'observation session count disagrees with focused endpoint' unless
  observation['sessions']['count'] == sessions['count'] + 1
raise 'observation sessions are not an array' unless
  observation['sessions']['sessions'].is_a?(Array)
raise 'observation did not include the created session' unless
  observation['sessions']['sessions'].any? { |session|
    session['id'] == created['id']
  }
raise 'observation displays are not an inventory' unless
  observation['displays']['displays'].is_a?(Array) &&
  observation['displays']['count'] == observation['displays']['displays'].length
raise 'observation diagnostics is missing health' unless
  observation['diagnostics']['health'].is_a?(Hash)
raise 'observation runtime is missing performance' unless
  observation['runtime']['performance'].is_a?(Hash)
raise 'observation render is missing the exact video driver' unless
  observation['diagnostics']['render']['video_driver'].is_a?(String) &&
  !observation['diagnostics']['render']['video_driver'].empty?
render_outputs = observation.dig('diagnostics', 'render', 'outputs')
raise 'observation render outputs are missing' unless
  render_outputs.is_a?(Array) && !render_outputs.empty?
render_outputs.each do |output|
  scanout = output['kms_scanout']
  raise 'per-output KMS scanout telemetry is missing' unless
    scanout.is_a?(Hash) && scanout.keys.sort == %w[available
      flip_completions last_vblank_timestamp_us observed_refresh_hz
      vblank_sequence vblank_span_count vblank_span_us].sort
  raise 'SDL dummy output falsely claims DRM/KMS telemetry' unless
    scanout['available'] == false && scanout['flip_completions'].zero? &&
    scanout['observed_refresh_hz'].zero?
end

puts "OBSERVATION_API_PASS fields=#{required.length} sessions=#{observation['sessions']['count']} displays=#{observation['displays']['count']}"
