#!/usr/bin/env ruby
# Read-only, bounded observations. No input, PTYs, settings or lifecycle writes.
require 'json'
require 'net/http'
require 'uri'
seconds = Float(ARGV.fetch(0, '10'))
abort 'duration must be finite and 2..30 seconds' unless seconds.finite? && seconds.between?(2, 30)
base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
abort 'expected HTTP(S) base URL' unless %w[http https].include?(base.scheme)
token = ENV.fetch('BLIT_AUTH_TOKEN')
get = lambda do |path|
  request = Net::HTTP::Get.new(base + path)
  request['Authorization'] = "Bearer #{token}"
  response = Net::HTTP.start(base.host, base.port, use_ssl: base.scheme == 'https',
                            open_timeout: 2, read_timeout: 3) { |http| http.request(request) }
  abort "GET #{path}: HTTP #{response.code}" unless response.is_a?(Net::HTTPSuccess)
  JSON.parse(response.body)
end
clock = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) }
version = get.call('/v1/version')
initial = get.call('/v1/diagnostics')
first = get.call('/v1/diagnostics/runtime').fetch('performance')
started = clock.call
samples = [initial.fetch('render')]
while clock.call - started < seconds
  sleep [0.5, seconds - (clock.call - started)].min.clamp(0, 0.5)
  samples << get.call('/v1/diagnostics').fetch('render')
end
last = get.call('/v1/diagnostics/runtime').fetch('performance')
elapsed = clock.call - started
final_version = get.call('/v1/version')
identity = lambda do |render|
  [render.values_at('direct_kms', 'video_driver', 'renderer', 'font_scale'),
   render.fetch('outputs').map { |o| o.values_at('display_id', 'render_width', 'render_height', 'refresh_hz') }]
end
abort 'backend/output geometry/version changed; discard comparison' unless
  version == final_version && samples.all? { |s| identity.call(s) == identity.call(samples.first) }
delta = lambda do |key|
  a = first[key]; b = last[key]
  abort "missing/reset counter #{key}; discard comparison" unless a.is_a?(Numeric) && b.is_a?(Numeric) && b >= a
  b - a
end
server_ms = delta.call('uptime_ms')
abort 'no server observation interval' unless server_ms > 0
outputs = samples.last.fetch('outputs').map do |o|
  phases = samples.filter_map do |s|
    s.fetch('outputs').find { |v| v['display_id'] == o['display_id'] }&.fetch('kms_phases', nil)
  end
  { 'display_id' => o['display_id'], 'refresh_hz' => o['refresh_hz'],
    'geometry' => o.values_at('render_width', 'render_height'),
    'sampled_last_frame_phases' => phases }
end
puts JSON.pretty_generate({
  'scope' => 'uncontrolled read-only window; not active-workload FPS or Kitty comparison',
  'version' => version, 'client_elapsed_seconds' => elapsed,
  'server_interval_ms' => server_ms,
  'server_cpu_percent_one_core' => 100.0 * delta.call('process_cpu_ms') / server_ms,
  'terminal_bytes_per_second' => 1000.0 * delta.call('terminal_bytes') / server_ms,
  'present_calls_delta' => delta.call('present_calls'),
  'missed_deadlines_delta' => delta.call('missed_deadlines'),
  'backend' => samples.last.values_at('direct_kms', 'video_driver', 'renderer'),
  'outputs' => outputs,
  'limitations' => ['CPU excludes Hibr/hold, compositor and GPU',
    'Last-frame phase samples may repeat a stale frame; not per-frame percentiles',
    'Presentation calls are not confirmed flips or distinct displayed frames',
    'Does not certify unchanged application state or font identity']
})
