#!/usr/bin/env ruby
# frozen_string_literal: true

# External Hibr surfaces must retain atomic commit boundaries without forcing a
# complete scene rebuild for every pointer motion. This gate uses the public
# API only and checks the renderer path trace rather than assuming a frame rate.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

port = Integer(ENV.fetch('BLIT_EXTERNAL_POINTER_PORT', '19252'))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-external-pointer-#{Process.pid}.log"
pid = Process.spawn({'SDL_VIDEODRIVER' => 'dummy',
                     'BLIT_RENDERER' => 'software',
                     'LD_LIBRARY_PATH' => 'build/sdl3-kms'},
                    'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

def request(base, method, path, body = nil)
  klass = {get: Net::HTTP::Get, post: Net::HTTP::Post,
           put: Net::HTTP::Put, delete: Net::HTTP::Delete}.fetch(method)
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
  response.body.empty? ? nil : JSON.parse(response.body)
end

begin
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
                    'surface_mode' => 'external',
                    'surface' => {'width' => 80, 'height' => 24})
  id = session.fetch('id')
  path = "/v1/surfaces/surface-#{id}/cells"
  request(base, :put, path,
          'base_revision' => 1,
          'presentation' => 'defer',
          'cells' => [{'x' => 0, 'y' => 0, 'text' => 'A',
                       'style' => {'background' => '#102030'}}])
  request(base, :put, path,
          'base_revision' => 2,
          'presentation' => 'commit',
          'cells' => [{'x' => 1, 'y' => 0, 'text' => 'B',
                       'style' => {'background' => '#405060'}}])
  sleep 0.2
  5.times do |index|
    request(base, :post, '/v1/input/inject',
            'kind' => 'pointer_motion',
            'x' => 200 + index * 40,
            'y' => 200 + index * 30)
    sleep 0.05
  end
  runtime = request(base, :get, '/v1/diagnostics/runtime')
  performance = runtime.fetch('performance')
  trace = request(base, :get, '/v1/diagnostics/trace?kind=render_path')
  details = trace.fetch('events').map { |event| event.fetch('detail') }
  pointer_hits = performance.fetch('pointer_cache_hits')
  raise "external pointer cache was not used: #{performance.inspect}" unless
    pointer_hits >= 5
  raise 'external cell damage did not render incrementally' unless
    details.any? { |detail| detail.include?('external_changed=1') &&
      detail.include?('graphics_changed=0') }
  raise 'pointer-cache render path was not observed' unless
    details.any? { |detail| detail.include?('path=pointer-cache') }
  puts "EXTERNAL_POINTER_COMPOSITION_PASS pointer_cache_hits=#{pointer_hits} " \
       "pointer_cache_misses=#{performance.fetch('pointer_cache_misses')}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
