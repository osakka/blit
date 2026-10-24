#!/usr/bin/env ruby
# frozen_string_literal: true

# External Hibr surfaces must retain atomic commit boundaries without forcing a
# complete scene rebuild for every pointer motion. This gate uses the public
# API only and checks the renderer path trace rather than assuming a frame rate.
#
# BLIT_TEST_SCENE_CACHE=0 exercises the physical host's conservative content
# policy: every content change is rebuilt in full because incremental render-
# target preservation is not trusted. The complete result must still become an
# immutable scene snapshot that pointer-only frames can copy. Conflating those
# two policies was the live 0.1.465 failure: 60 idle pointer motions produced
# 60 full redraws, 60 cache misses and zero cache hits.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'sdl_test_profile'
# SDL path only (asserts the SDL pointer cache); retired at the GLES cutover.
SdlTestProfile.sdl_only!

port = gate_port(Integer(ENV.fetch('BLIT_EXTERNAL_POINTER_PORT', '19252')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-external-pointer-#{Process.pid}.log"
incremental_content = ENV.fetch('BLIT_TEST_SCENE_CACHE', '1') != '0'
pid = Process.spawn(SdlTestProfile.environment.merge(
                     'BLIT_SDL_SCENE_CACHE' => incremental_content ? '1' : '0'),
                    SdlTestProfile.binary, port.to_s,
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
                    'arguments' => ['-c', 'sleep 30'],
                    'surface_mode' => 'external',
                    'surface' => {'width' => 80, 'height' => 24})
  id = session.fetch('id')
  path = "/v1/surfaces/surface-#{id}/cells"
  seed = request(base, :put, path, 'base_revision' => 1,
                 'presentation' => 'defer',
                 'cells' => [{'x' => 0, 'y' => 1, 'text' => ' ',
                              'style' => {'background' => '#010203'}}])
  seed = request(base, :put, path, 'base_revision' => seed.fetch('revision'),
                 'presentation' => 'commit',
                 'cells' => [{'x' => 1, 'y' => 1, 'text' => ' ',
                              'style' => {'background' => '#010203'}}])
  # A first scene necessarily redraws in full. If create/defer/commit are
  # serviced together before its first presentation, that is correct, not a
  # failed incremental update. Establish the presented baseline before asking
  # the existing scene to take the incremental path. No timing-based sleep.
  Timeout.timeout(8) do
    loop do
      capture = Net::HTTP.start(base.hostname, base.port, read_timeout: 5) do |http|
        http.get('/v1/diagnostics/screenshots/display-1')
      end
      raise "initial scene capture failed: HTTP #{capture.code}" unless capture.code == '200'
      captured = capture['X-Blit-Capture-Revisions'].to_s.split(',').to_h { |pair| pair.split('=', 2) }
      break if captured[session.fetch('surface_id')].to_i == seed.fetch('revision')
      sleep 0.01
    end
  end
  deferred = request(base, :put, path,
          'base_revision' => seed.fetch('revision'),
          'presentation' => 'defer',
          'cells' => [{'x' => 0, 'y' => 0, 'text' => 'A',
                       'style' => {'background' => '#102030'}}])
  request(base, :put, path,
          'base_revision' => deferred.fetch('revision'),
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
  surface_trace = request(base, :get, '/v1/diagnostics/trace?kind=surface_update')
  surface_details = surface_trace.fetch('events').map { |event| event.fetch('detail') }
  pointer_hits = performance.fetch('pointer_cache_hits')
  raise "external pointer cache was not used: #{performance.inspect}" unless
    pointer_hits >= 5
  expected_content_path = incremental_content ? 'path=incremental-damage' :
                                                'path=full-redraw'
  raise "external cell damage did not use #{expected_content_path}: #{details.inspect}" unless
    details.any? { |detail| detail.include?('external_changed=1') &&
      detail.include?('graphics_changed=0') &&
      detail.include?(expected_content_path) }
  raise 'pointer-cache render path was not observed' unless
    details.any? { |detail| detail.include?('path=pointer-cache') }
  raise 'surface transport/mode was not classified' unless
    surface_details.any? { |detail| detail.include?('transport=api-cells') &&
      detail.include?('surface_mode=external') &&
      detail.include?('presentation=commit') }
  puts "EXTERNAL_POINTER_COMPOSITION_PASS content_policy=" \
       "#{incremental_content ? 'incremental' : 'full-rebuild'} " \
       "pointer_cache_hits=#{pointer_hits} " \
       "pointer_cache_misses=#{performance.fetch('pointer_cache_misses')}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
