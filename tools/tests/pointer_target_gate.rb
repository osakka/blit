#!/usr/bin/env ruby
# frozen_string_literal: true

# A fixed evdev-pointer route must not be required for ordinary pane input.
# The pointer target is selected from global coordinates and pane geometry;
# explicit routes remain a separate API feature for dedicated probe workloads.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

port = Integer(ENV.fetch('BLIT_POINTER_TARGET_PORT', '19256'))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-pointer-target-#{Process.pid}.log"
pid = Process.spawn({'SDL_VIDEODRIVER' => 'dummy',
                     'BLIT_RENDERER' => 'software',
                     'LD_LIBRARY_PATH' => 'build/sdl3-kms'},
                    'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

def request(base, method, path, body = nil)
  klass = {get: Net::HTTP::Get, post: Net::HTTP::Post,
           delete: Net::HTTP::Delete}.fetch(method)
  response = Net::HTTP.start(base.hostname, base.port, open_timeout: 2,
                              read_timeout: 4) do |http|
    req = klass.new(base + path)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.start_with?('2')
  response.body.to_s.empty? ? nil : JSON.parse(response.body)
end

begin
  fallback_id = nil
  at_exit do
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
  Timeout.timeout(8) do
    loop do
      begin
        request(base, :get, '/v1/version')
        break
      rescue StandardError
        sleep 0.05
      end
    end
  end

  session = request(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh',
                    'arguments' => ['-c', 'sleep 3'],
                    'pane_id' => 'pane-display-1-1',
                    'surface' => {'width' => 240, 'height' => 135})
  id = session.fetch('id')
  request(base, :post, '/v1/input/inject',
          'kind' => 'pointer_button', 'button' => 0, 'pressed' => true)
  request(base, :post, '/v1/input/inject',
          'kind' => 'pointer_button', 'button' => 0, 'pressed' => false)
  trace = request(base, :get,
                  '/v1/diagnostics/trace?kind=pointer_button&limit=16')
  details = trace.fetch('events').map { |event| event.fetch('detail') }
  expected = details.select { |detail| detail.include?('target_session=1') }
  raise "pane hit-test routing was not observed: #{details.inspect}" unless
    expected.any? { |detail| detail.include?('routing=pane-hit-test') }
  raise 'fixed pointer route unexpectedly selected the target' if
    expected.any? { |detail| detail.include?('routing=explicit') }
  puts 'POINTER_TARGET_PASS routing=pane-hit-test'

  # A released pane remains free layout, not an input sink. A display-attached
  # session without pane_id must still receive pointer input.
  request(base, :delete, "/v1/terminal-sessions/#{id}")
  id = nil
  fallback = request(base, :post, '/v1/terminal-sessions',
                     'command' => '/bin/sh',
                     'arguments' => ['-c', 'sleep 3'],
                     'display_id' => 'display-1',
                     'surface' => {'width' => 240, 'height' => 135})
  fallback_id = fallback.fetch('id')
  request(base, :post, '/v1/input/inject',
          'kind' => 'pointer_motion', 'x' => 100, 'y' => 100)
  fallback_trace = request(base, :get,
                           '/v1/diagnostics/trace?kind=pointer_motion&limit=16')
  fallback_details = fallback_trace.fetch('events').map { |event| event.fetch('detail') }
  expected_fallback = fallback_details.select do |detail|
    detail.include?("target_session=#{fallback_id}")
  end
raise "free pane swallowed display fallback: #{fallback_details.inspect}" if
    expected_fallback.empty?
raise "display fallback route was not identified: #{expected_fallback.inspect}" unless
  expected_fallback.any? { |detail| detail.include?('routing=display-fallback') }
puts "POINTER_FREE_PANE_FALLBACK_PASS target_session=#{fallback_id}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
  begin
    request(base, :delete, "/v1/terminal-sessions/#{fallback_id}") if fallback_id
  rescue StandardError
    nil
  end
end
