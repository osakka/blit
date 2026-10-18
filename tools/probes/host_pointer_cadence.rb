#!/usr/bin/env ruby
# Bounded normal-path pointer workload. No clicks, keys, sessions or settings.
require 'json'
require 'net/http'
require 'uri'
base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
http = Net::HTTP.start(base.host, base.port, open_timeout: 3, read_timeout: 8)
request = lambda do |path, payload = nil|
  q = (payload ? Net::HTTP::Post : Net::HTTP::Get).new(base + path)
  q['Authorization'] = "Bearer #{token}"
  if payload
    q['Content-Type'] = 'application/json'; q.body = JSON.generate(payload)
  end
  r = http.request(q)
  raise "#{path}: #{r.code}" unless r.code.start_with?('2')
  JSON.parse(r.body)
end
before = request.call('/v1/diagnostics/observation')
pointer = before.dig('diagnostics','render','pointer')
abort 'no valid pointer; no workload applied' unless pointer['valid']
x = pointer['x'].to_i; y = pointer['y'].to_i
samples = []; count = 0
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
begin
  360.times do |i|
    request.call('/v1/input/inject', {kind:'pointer_motion',x:x+i%2,y:y})
    count += 1
    samples << request.call('/v1/diagnostics/runtime') if i % 60 == 59
    sleep 0.016
  end
ensure
  request.call('/v1/input/inject', {kind:'pointer_motion',x:x,y:y})
end
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
after = request.call('/v1/diagnostics/observation')
first = before.dig('runtime','performance'); last = after.dig('runtime','performance')
verdict = {version:before['version'], displays:before.dig('displays','count'),
  duration_s:elapsed, motions:count, present_delta:last['present_calls']-first['present_calls'],
  loop_delta:last['loop_iterations']-first['loop_iterations'],
  render_samples:samples.map{|s|s['performance'].slice('last_render_ms','frame_rate_hz','loop_gap_window_max_ms','last_kms_present_ms')},
  hotplug:after.dig('diagnostics','render','hotplug')}
wall_ms = last['uptime_ms'] - first['uptime_ms']
if wall_ms > 0 && last['process_cpu_ms'] && first['process_cpu_ms']
  verdict[:blit_cpu_percent] = 100.0*(last['process_cpu_ms']-first['process_cpu_ms'])/wall_ms
end
if wall_ms > 0 && last['desktop_cpu_ms'] && first['desktop_cpu_ms'] &&
   before.dig('login','desktop','owner') == after.dig('login','desktop','owner')
  verdict[:native_owner_cpu_percent] = 100.0*(last['desktop_cpu_ms']-first['desktop_cpu_ms'])/wall_ms
end
verdict[:system_load_1m] = last['system_load_1m'] if last['system_load_1m']
if (directory = ENV['BLIT_EVIDENCE_DIR'])
  require 'fileutils'; FileUtils.mkdir_p(directory)
  File.write(File.join(directory,'pointer-cadence.json'),JSON.pretty_generate(verdict))
end
puts JSON.pretty_generate(verdict)
http.finish
