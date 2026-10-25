#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

remote_server = ENV['BLIT_GATE_SERVER']
if remote_server
  base = URI(remote_server)
  pid = nil
else
  port = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19248')))
  base = URI("http://127.0.0.1:#{port}")
  log = File.join(Dir.tmpdir, "blit-frame-rate-#{Process.pid}.log")
  env = {
    **gate_display_env,
  }
  pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                      out: log, err: [:child, :out])
end

at_exit do
  if pid
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

def request(base, method, path, body = nil)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    req = method.new(path)
    req['Authorization'] = "Bearer #{ENV['BLIT_GATE_AUTH_TOKEN']}" if
      ENV['BLIT_GATE_AUTH_TOKEN'] && !ENV['BLIT_GATE_AUTH_TOKEN'].empty?
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    response = http.request(req)
    raise "HTTP #{response.code} for #{path}: #{response.body}" unless response.is_a?(Net::HTTPSuccess)

    body = response.body.to_s
    body.empty? ? nil : JSON.parse(body)
  end
end

surface_width = Integer(ENV.fetch('BLIT_GATE_SURFACE_WIDTH', '80'), 10)
surface_height = Integer(ENV.fetch('BLIT_GATE_SURFACE_HEIGHT', '24'), 10)
raise 'surface width must be 1..512' unless surface_width.between?(1, 512)
raise 'surface height must be 1..256' unless surface_height.between?(1, 256)

Timeout.timeout(8) do
  loop do
    begin
      request(base, Net::HTTP::Get, '/v1/server')
      break
    rescue StandardError
      sleep 0.1
    end
  end
end

session_id = nil
begin
  baseline = request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')
  baseline_performance = baseline.fetch('performance')
  baseline_terminal_bytes = baseline_performance.fetch('terminal_bytes')
  baseline_window_sequence = baseline_performance.fetch('frame_rate_window_sequence')
  # The first measurement window can include SDL renderer/font-atlas startup.
  # It is useful as a diagnostic, but it is not steady-state presentation
  # evidence. Require the next completed window so a cold start cannot make a
  # healthy 60 Hz cadence look like a renderer regression.
  required_window_sequence = baseline_window_sequence + 2
  minimum_hz = Float(ENV.fetch('BLIT_GATE_MIN_HZ', '30'))
  session = request(base, Net::HTTP::Post, '/v1/terminal-sessions',
                   'command' => '/bin/sh',
                   'arguments' => ['-c',
                     "i=0; while [ \"$i\" -lt 1200 ]; do printf '%*s\\n' #{surface_width} '' | tr ' ' X; i=$((i + 1)); sleep 0.005; done"],
                   'surface_mode' => 'terminal',
                   'surface' => {'width' => surface_width,
                                 'height' => surface_height})
  session_id = session.fetch('id')
  samples = []
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 12
  while Process.clock_gettime(Process::CLOCK_MONOTONIC) < deadline
    samples << request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')
    performance = samples.last.fetch('performance')
    break if performance.fetch('frame_rate_window_sequence') >= required_window_sequence &&
              performance.fetch('frame_rate_window_ms').between?(500, 2000) &&
              performance.fetch('frame_rate_hz') > 0 &&
              performance.fetch('terminal_bytes') > baseline_terminal_bytes
    sleep 0.25
  end

  performance = samples.last.fetch('performance')
  %w[loop_gap_window_ms loop_gap_window_sequence loop_gap_window_max_ms].each do |name|
    raise "runtime diagnostics missing #{name}" unless performance.key?(name)
  end
  raise 'loop-gap measurement window never completed' unless
    performance.fetch('loop_gap_window_sequence') >=
      baseline.fetch('performance').fetch('loop_gap_window_sequence') + 1
  raise 'loop-gap window duration invalid' unless
    performance.fetch('loop_gap_window_ms') >= 500
  outputs = samples.last.fetch('snapshot').fetch('render').fetch('outputs')
  raise 'no rendered output reported' if outputs.empty?
  terminal_bytes = performance.fetch('terminal_bytes')
  raise format('frame-rate workload produced no terminal output: baseline=%d current=%d',
               baseline_terminal_bytes, terminal_bytes) unless
    terminal_bytes > baseline_terminal_bytes
  target_interval_ms = outputs.map { |output| output.fetch('target_interval_ms') }.max
  target_refresh_hz = outputs.map { |output| output.fetch('refresh_hz') }.max
  max_render_budget_ms = Float(ENV.fetch(
    'BLIT_GATE_MAX_RENDER_MS', '20.000'))
  raise 'frame-rate window never completed' unless performance.fetch('frame_rate_window_ms') >= 500
  raise 'frame-rate window did not advance during workload' unless
    performance.fetch('frame_rate_window_sequence') >= required_window_sequence
  raise 'frame-rate measurement is zero' unless performance.fetch('frame_rate_hz') > 0
  raise format('frame-rate below required minimum: %.3f < %.3f Hz',
               performance.fetch('frame_rate_hz'), minimum_hz) unless
    performance.fetch('frame_rate_hz') >= minimum_hz
  maximum_hz = target_refresh_hz * 1.25
  raise format('producer burst escaped refresh coalescing: %.3f > %.3f Hz',
               performance.fetch('frame_rate_hz'), maximum_hz) unless
    performance.fetch('frame_rate_hz') <= maximum_hz
  raise 'frame-rate measurement is stale' unless performance.fetch('frame_rate_age_ms') <= 2000
  raise 'frame-rate frame count invalid' unless performance.fetch('frame_rate_frames') >= 1
  raise 'refresh deadline was missed' unless performance.fetch('frame_rate_missed_deadlines').zero?
  raise format('full-scene render exceeded budget: %.3f > %.3f ms',
               performance.fetch('frame_rate_max_render_ms'),
               max_render_budget_ms) unless
    performance.fetch('frame_rate_max_render_ms') <= max_render_budget_ms
  puts format('FRAME_RATE_PASS hz=%.3f min_hz=%.3f max_hz=%.3f frames=%d window_ms=%d age_ms=%d window_max_render_ms=%.3f render_budget_ms=%.3f target_interval_ms=%.3f window_missed_deadlines=%d terminal_bytes=%d',
              performance.fetch('frame_rate_hz'),
              minimum_hz,
              maximum_hz,
              performance.fetch('frame_rate_frames'),
              performance.fetch('frame_rate_window_ms'),
              performance.fetch('frame_rate_age_ms'),
              performance.fetch('frame_rate_max_render_ms'), max_render_budget_ms,
              target_interval_ms,
              performance.fetch('frame_rate_missed_deadlines'), terminal_bytes)
ensure
  if session_id
    begin
      request(base, Net::HTTP::Delete, "/v1/terminal-sessions/#{session_id}")
    rescue StandardError => e
      warn "frame-rate cleanup: #{e.message}"
    end
  end
end
