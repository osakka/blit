#!/usr/bin/env ruby
# frozen_string_literal: true

# Gate 6 bounded soak runner. The server is started by the caller. This test
# deliberately exercises the same API surface used by Hibr: concurrent PTYs,
# terminal output, snapshots, resize/revision handling, and deterministic
# teardown. It does not require Hibr and never modifies Hibr state.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

base = URI(ENV.fetch('BLIT_TEST_SERVER', 'http://127.0.0.1:19195'))
duration = Integer(ENV.fetch('BLIT_SOAK_SECONDS', '120'), 10)
session_count = Integer(ENV.fetch('BLIT_SOAK_SESSIONS', '4'), 10)
raise 'BLIT_SOAK_SECONDS must be positive' unless duration.positive?
raise 'BLIT_SOAK_SESSIONS must be positive' unless session_count.positive?

def request(base, method, path, body = nil, headers = {})
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'PATCH' => Net::HTTP::Patch,
    'DELETE' => Net::HTTP::Delete
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 5) do |http|
    http.request(req)
  end
  code = response.code.to_i
  raise "#{method} #{path}: HTTP #{code}: #{response.body}" unless code.between?(200, 299)
  return nil if code == 204 || response.body.empty?

  JSON.parse(response.body)
end

def rss_kib(pid)
  return nil unless pid && !pid.empty?

  status = File.read("/proc/#{pid}/status")
  match = status.match(/^VmRSS:\s+(\d+)\s+kB$/)
  match && match[1].to_i
rescue Errno::ENOENT, Errno::ESRCH
  nil
end

def assert(condition, message)
  raise message unless condition
end

sessions = []
max_rss = 0
snapshots = 0
resizes = 0
last_frames = nil
stagnant_frame_checks = 0
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
deadline = started + duration

begin
  session_count.times do |index|
    created = request(base, 'POST', '/v1/terminal-sessions', {
      'command' => '/bin/sh',
      'arguments' => ['-c', <<~'CHILD'],
        i=0
        while :; do
          printf '\033[1;3%dmBLIT_SOAK_%04d\033[0m %s\\r\\n' "$((i % 7 + 1))" "$i" "UTF8-€-界"
          printf '\033[2J\033[H'
          i=$((i + 1))
          sleep 0.01
        done
      CHILD
      'surface_mode' => 'terminal',
      'surface' => { 'width' => 96 + index, 'height' => 32 }
    })
    sessions << { 'id' => created.fetch('id'), 'revision' => created.fetch('revision'),
                  'width' => 96 + index }
  end

  while Process.clock_gettime(Process::CLOCK_MONOTONIC) < deadline
    sessions.each_with_index do |session, index|
      id = session.fetch('id')
      state = request(base, 'GET', "/v1/terminal-sessions/#{id}")
      assert(%w[starting running].include?(state.fetch('state')), "session #{id} left running state")
      snapshot = request(base, 'GET', "/v1/surfaces/surface-#{id}/cells")
      assert(snapshot.fetch('cells').is_a?(Array), "session #{id} returned invalid cells")
      snapshots += 1

      if (snapshots + index) % 11 == 0
        next_width = session.fetch('width') == 96 + index ? 80 + index : 96 + index
        resized = request(base, 'PATCH', "/v1/terminal-sessions/#{id}", {
          'revision' => state.fetch('revision'),
          'surface' => { 'width' => next_width, 'height' => 32 }
        }, { 'If-Match' => state.fetch('revision').to_s })
        session['width'] = next_width
        session['revision'] = resized.fetch('revision')
        resizes += 1
      end
    end

    runtime_response = request(base, 'GET', '/v1/diagnostics/runtime')
    diagnostics = runtime_response.fetch('snapshot')
    health = diagnostics.fetch('health')
    assert(health.fetch('ready') == true, 'runtime stopped being ready')
    assert(diagnostics.fetch('errors') == [], "runtime errors: #{diagnostics.fetch('errors').inspect}")
    render = diagnostics.fetch('render')
    frames = runtime_response.fetch('performance').fetch('frames')
    if last_frames.nil? || frames > last_frames
      stagnant_frame_checks = 0
    else
      stagnant_frame_checks += 1
    end
    assert(stagnant_frame_checks < 50, 'render frame counter stopped')
    last_frames = frames
    current_rss = rss_kib(ENV['BLIT_SOAK_PID'])
    max_rss = [max_rss, current_rss || 0].max
    sleep 0.02
  end
ensure
  sessions.reverse_each do |session|
    begin
      request(base, 'DELETE', "/v1/terminal-sessions/#{session.fetch('id')}")
    rescue StandardError => error
      warn "soak cleanup #{session.fetch('id')}: #{error.message}"
    end
  end
end

runtime = request(base, 'GET', '/v1/diagnostics/runtime').fetch('snapshot')
assert(runtime.fetch('health').fetch('ready') == true, 'runtime not ready after cleanup')
assert(runtime.fetch('resources').fetch('sessions') == 0, 'sessions leaked after cleanup')
assert(runtime.fetch('render').fetch('pending') == false, 'render queue did not drain after cleanup')
puts "TERMINAL_SOAK_PASS seconds=#{duration} sessions=#{session_count} " \
     "snapshots=#{snapshots} resizes=#{resizes} max_rss_kib=#{max_rss}"
