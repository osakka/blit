#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'

port = Integer(ENV.fetch('BLIT_GATE_PORT', '19248'))
base = URI("http://127.0.0.1:#{port}")
log = File.join(Dir.tmpdir, "blit-frame-rate-#{Process.pid}.log")
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  if pid
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

def request(base, method, path, body = nil)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    req = method.new(path)
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

session = request(base, Net::HTTP::Post, '/v1/terminal-sessions',
                 'command' => '/bin/sh',
                 'arguments' => ['-c',
                   'i=0; while [ "$i" -lt 180 ]; do printf "BLIT_FRAME_%s\\n" "$i"; i=$((i + 1)); sleep 0.02; done'],
                 'surface_mode' => 'terminal',
                 'surface' => {'width' => 80, 'height' => 24})
session_id = session.fetch('id')
samples = []
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 6
while Process.clock_gettime(Process::CLOCK_MONOTONIC) < deadline
  samples << request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')
  performance = samples.last.fetch('performance')
  break if performance.fetch('frame_rate_window_ms') >= 500 &&
            performance.fetch('frame_rate_hz') > 0
  sleep 0.25
end

performance = samples.last.fetch('performance')
outputs = samples.last.fetch('snapshot').fetch('render').fetch('outputs')
raise 'no rendered output reported' if outputs.empty?
target_interval_ms = outputs.map { |output| output.fetch('target_interval_ms') }.max
raise 'frame-rate window never completed' unless performance.fetch('frame_rate_window_ms') >= 500
raise 'frame-rate measurement is zero' unless performance.fetch('frame_rate_hz') > 0
raise 'frame-rate measurement is stale' unless performance.fetch('frame_rate_age_ms') <= 2000
raise 'frame-rate frame count invalid' unless performance.fetch('frame_rate_frames') >= 1
raise 'refresh deadline was missed' unless performance.fetch('missed_deadlines').zero?
raise format('render exceeded refresh budget: %.3f > %.3f ms',
             performance.fetch('max_render_ms'), target_interval_ms) unless
  performance.fetch('max_render_ms') <= target_interval_ms

request(base, Net::HTTP::Delete, "/v1/terminal-sessions/#{session_id}")
puts format('FRAME_RATE_PASS hz=%.3f frames=%d window_ms=%d age_ms=%d max_render_ms=%.3f target_interval_ms=%.3f missed_deadlines=%d',
            performance.fetch('frame_rate_hz'),
            performance.fetch('frame_rate_frames'),
            performance.fetch('frame_rate_window_ms'),
            performance.fetch('frame_rate_age_ms'),
            performance.fetch('max_render_ms'), target_interval_ms,
            performance.fetch('missed_deadlines'))
