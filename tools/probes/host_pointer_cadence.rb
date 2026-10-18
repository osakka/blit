#!/usr/bin/env ruby
# Bounded normal-path pointer workload. No clicks, keys, sessions or settings.
require 'json'
require 'net/http'
require 'uri'
rate = Float(ENV.fetch('BLIT_POINTER_HZ','60'))
abort 'BLIT_POINTER_HZ must be 1..240' unless rate.between?(1,240)
async = ENV['BLIT_POINTER_ASYNC'] == '1'
base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
request = lambda do |path, payload = nil|
  q = (payload ? Net::HTTP::Post : Net::HTTP::Get).new(base + path)
  q['Authorization'] = "Bearer #{token}"
  # Every operation has a fresh connection. A requested rate is not an
  # achieved rate: path RTT and gateway work remain in measured request times.
  q['Connection'] = 'close'
  if payload
    q['Content-Type'] = 'application/json'; q.body = JSON.generate(payload)
  end
  r = Net::HTTP.start(base.host, base.port, open_timeout:3,read_timeout:8){|h|h.request(q)}
  raise "#{path}: #{r.code}" unless r.code.start_with?('2')
  JSON.parse(r.body)
end
before = request.call('/v1/diagnostics/observation')
pointer = before.dig('diagnostics','render','pointer')
abort 'no valid pointer; no workload applied' unless pointer['valid']
x = pointer['x'].to_i; y = pointer['y'].to_i
samples = []; count = 0; request_ms = []; late_slots = 0
queue = SizedQueue.new(16); workers = []; errors = Queue.new; skipped = 0
if async
  workers = 8.times.map do
    Thread.new do
      while (i = queue.pop)
        t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
        begin
          request.call('/v1/input/inject',{kind:'pointer_motion',x:x+i%2,y:y})
          request_ms << 1000*(Process.clock_gettime(Process::CLOCK_MONOTONIC)-t)
        rescue => e
          errors << e
        end
      end
    end
  end
end
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
begin
  360.times do |i|
    deadline = started + i / rate
    delay = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    sleep delay if delay > 0
    late_slots += 1 if -delay > 1.0 / rate
    if async
      begin; queue.push(i,true); count += 1; rescue ThreadError; skipped += 1; end
    else
      request_started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      request.call('/v1/input/inject', {kind:'pointer_motion',x:x+i%2,y:y})
      request_ms << 1000.0*(Process.clock_gettime(Process::CLOCK_MONOTONIC)-request_started)
      count += 1
      samples << request.call('/v1/diagnostics/runtime') if i % 60 == 59
    end
  end
ensure
  workers.length.times{queue << nil}; workers.each(&:join)
  request.call('/v1/input/inject', {kind:'pointer_motion',x:x,y:y})
end
raise errors.pop unless errors.empty?
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
after = request.call('/v1/diagnostics/observation')
first = before.dig('runtime','performance'); last = after.dig('runtime','performance')
verdict = {version:before['version'], displays:before.dig('displays','count'),
  requested_motion_hz:rate, http_connection:'close', late_slots:late_slots,
  asynchronous:async, workers:workers.length, skipped_injections:skipped,
  request_p50_ms:request_ms.sort[request_ms.length/2],
  request_p95_ms:request_ms.sort[(request_ms.length*0.95).floor],
  duration_s:elapsed, motions:count, present_delta:last['present_calls']-first['present_calls'],
  loop_delta:last['loop_iterations']-first['loop_iterations'],
  render_samples:samples.map{|s|s['performance'].slice('last_render_ms','frame_rate_hz','loop_gap_window_max_ms','last_kms_present_ms')},
  pointer_output_id:before.dig('diagnostics','render','outputs',pointer['output'],'display_id'),
  output_submissions:after.dig('diagnostics','render','outputs').map do |output|
    old=before.dig('diagnostics','render','outputs').find{|v|v['display_id']==output['display_id']}
    {display_id:output['display_id'], delta:old ? output['present_calls']-old['present_calls'] : nil,
     last_phases:output['kms_phases']}
  end,
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
