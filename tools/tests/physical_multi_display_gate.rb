#!/usr/bin/env ruby
# frozen_string_literal: true

# Hardware acceptance gate. Unlike the VKMS fixtures, this gate must be run
# against a real Blit instance owning a physical DRM device.

require 'json'
require 'net/http'
require 'uri'

base = URI(ENV.fetch('BLIT_GATE_SERVER', 'http://127.0.0.1:8080'))

def request(base, method, path, body = nil, headers = {})
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'PUT' => Net::HTTP::Put,
    'DELETE' => Net::HTTP::Delete
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port,
                             open_timeout: 3, read_timeout: 8) do |http|
    http.request(req)
  end
  unless response.code.start_with?('2')
    raise "#{method} #{path}: HTTP #{response.code}: #{response.body}"
  end
  response.body.empty? ? nil : JSON.parse(response.body)
end

def wait_for_running(base, id)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 10
  loop do
    detail = request(base, 'GET', "/v1/terminal-sessions/#{id}")
    return detail if detail.fetch('state') == 'running'
    return detail if detail.fetch('state') == 'failed'
    raise "session #{id} did not become ready" if
      Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline

    sleep 0.2
  end
end

displays = request(base, 'GET', '/v1/displays').fetch('displays')
if displays.length < 2
  warn "PHYSICAL_MULTI_DISPLAY_UNAVAILABLE displays=#{displays.length}"
  exit 77
end

before_runtime = request(base, 'GET', '/v1/diagnostics/runtime')
before_resources = before_runtime.fetch('snapshot').fetch('resources')
raise 'physical gate requires no active sessions' unless
  before_resources.fetch('sessions').zero?

original = request(base, 'GET', '/v1/topology')
original_layout = original.fetch('layout')
original_placements = original.fetch('outputs').map do |output|
  bounds = output.fetch('bounds')
  {
    'display_id' => output.fetch('display_id'),
    'x' => bounds.fetch('x'),
    'y' => bounds.fetch('y')
  }
end
session_ids = []
topology_changed = false

begin
  layout = {'columns' => 1, 'rows' => 1}
  if original_layout != layout || original.fetch('panes').length != displays.length
    current = request(base, 'GET', '/v1/topology')
    request(base, 'PUT', '/v1/topology', {
      'revision' => current.fetch('revision'),
      'layout' => layout
    }, 'If-Match' => current.fetch('revision').to_s)
    topology_changed = true
  end

  topology = request(base, 'GET', '/v1/topology')
  panes = topology.fetch('panes').select { |pane| pane['surface_id'].nil? }
  raise "expected #{displays.length} free physical panes, got #{panes.length}" if
    panes.length < displays.length

  displays.each_with_index do |display, index|
    pane = panes.fetch(index)
    bounds = pane.fetch('bounds')
    created = request(base, 'POST', '/v1/terminal-sessions', {
      'command' => '/bin/sh',
      'arguments' => ['-c', "printf PHYSICAL_DISPLAY_#{index}\\n; sleep 3"],
      'surface_mode' => 'terminal',
      'pane_id' => pane.fetch('id'),
      'surface' => {
        'width' => bounds.fetch('width'),
        'height' => bounds.fetch('height')
      }
    })
    id = created.fetch('id')
    session_ids << id
    detail = wait_for_running(base, id)
    raise "display #{display.fetch('id')} session #{id} failed" unless
      detail.fetch('state') == 'running'
  end

  runtime = request(base, 'GET', '/v1/diagnostics/runtime')
  snapshot = runtime.fetch('snapshot')
  resources = snapshot.fetch('resources')
  raise 'physical display count changed during gate' unless
    resources.fetch('displays') >= displays.length
  raise 'not all physical sessions are running' unless
    resources.fetch('sessions') == displays.length
  raise "runtime not ready: #{snapshot.fetch('health')}" unless
    snapshot.fetch('health').fetch('ready')
  raise "runtime errors: #{snapshot.fetch('errors')}" unless
    snapshot.fetch('errors').empty?

  pointer = snapshot.fetch('render').fetch('pointer')
  raise 'pointer diagnostics are invalid' unless pointer.fetch('valid')
  raise 'pointer output is outside physical outputs' unless
    pointer.fetch('output') < displays.length

  puts "PHYSICAL_MULTI_DISPLAY_PASS displays=#{displays.length} " \
       "sessions=#{resources.fetch('sessions')} " \
       "pointer_output=#{pointer.fetch('output')}"
ensure
  session_ids.each do |id|
    request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  rescue StandardError => e
    warn "cleanup session #{id}: #{e.message}"
  end

  if topology_changed
    begin
      current = request(base, 'GET', '/v1/topology')
      request(base, 'PUT', '/v1/topology', {
        'revision' => current.fetch('revision'),
        'layout' => original_layout,
        'placements' => original_placements
      }, 'If-Match' => current.fetch('revision').to_s)
    rescue StandardError => e
      warn "cleanup topology: #{e.message}"
    end
  end
end

after = request(base, 'GET', '/v1/diagnostics/runtime').fetch('snapshot').fetch('resources')
%w[sessions surfaces streams input_routes].each do |resource|
  expected = before_resources.fetch(resource)
  actual = after.fetch(resource)
  raise "resource leak #{resource}: expected #{expected}, got #{actual}" unless
    actual == expected
end
