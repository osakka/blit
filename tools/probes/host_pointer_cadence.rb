#!/usr/bin/env ruby
# Bounded API pointer workload. No clicks, keys, sessions or settings.
# Sweep pixels/event and delivered output frames are measured separately.
require 'json'
require 'net/http'
require 'uri'
rate = Float(ENV.fetch('BLIT_POINTER_HZ','60'))
abort 'BLIT_POINTER_HZ must be 1..240' unless rate.between?(1,240)
step_px = Integer(ENV.fetch('BLIT_POINTER_STEP_PX','1'), 10)
abort 'BLIT_POINTER_STEP_PX must be 1..40' unless step_px.between?(1,40)
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
pattern = ENV.fetch('BLIT_POINTER_PATTERN','alternate')
abort 'BLIT_POINTER_PATTERN must be alternate or sweep' unless %w[alternate sweep].include?(pattern)
display = before.dig('displays','displays').find do |d|
  d['id']==before.dig('diagnostics','render','outputs',pointer['output'],'display_id')
end
abort 'pointer output is absent' unless display
left=display.dig('bounds','x');right=left+display.dig('bounds','width')-1
margin = [step_px * 2, 32].max
span = [right - left + 1 - margin * 2, 1].max
abort 'sweep requires at least three pointer margins of output width' if
  pattern == 'sweep' && span <= step_px
minimum_x = left + margin
start_x = [[x - 180, minimum_x].max, right - margin].min
target_x=lambda do |i|
  if pattern == 'sweep'
    distance = (start_x - minimum_x + i * step_px) % (span * 2)
    offset = distance <= span ? distance : span * 2 - distance
    minimum_x + offset
  else
    x + i % 2
  end
end
samples = []; count = 0; request_ms = []; late_slots = 0
queue = SizedQueue.new(16); workers = []; errors = Queue.new; skipped = 0
if async
  workers = 8.times.map do
    Thread.new do
      while (i = queue.pop)
        t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
        begin
          request.call('/v1/input/inject',{kind:'pointer_motion',x:target_x.call(i),y:y})
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
      request.call('/v1/input/inject', {kind:'pointer_motion',x:target_x.call(i),y:y})
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
  pattern:pattern, original_pointer:{x:x,y:y},
  requested_motion_hz:rate, step_px:step_px,
  http_connection:'close', late_slots:late_slots,
  asynchronous:async, workers:workers.length, skipped_injections:skipped,
  request_p50_ms:request_ms.sort[request_ms.length/2],
  request_p95_ms:request_ms.sort[(request_ms.length*0.95).floor],
  duration_s:elapsed, motions:count, present_delta:last['present_calls']-first['present_calls'],
  loop_delta:last['loop_iterations']-first['loop_iterations'],
  input_delta:last['input_events']-first['input_events'],
  physical_mouse_events_delta:last['evdev_mouse_events']-first['evdev_mouse_events'],
  terminal_bytes_delta:last['terminal_bytes']-first['terminal_bytes'],
  render_samples:samples.map{|s|s['performance'].slice('last_render_ms','frame_rate_hz','loop_gap_window_max_ms','last_kms_present_ms')},
  pointer_output_id:before.dig('diagnostics','render','outputs',pointer['output'],'display_id'),
  output_submissions:after.dig('diagnostics','render','outputs').map do |output|
    old=before.dig('diagnostics','render','outputs').find{|v|v['display_id']==output['display_id']}
    delta = old ? output['present_calls']-old['present_calls'] : nil
    {display_id:output['display_id'], refresh_hz:output['refresh_hz'],
     target_interval_us:output['target_interval_us'],
     delta:delta, presentation_hz:elapsed > 0 ? delta / elapsed : nil,
     missed_deadlines_delta:old ? output['missed_deadlines']-old['missed_deadlines'] : nil,
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
