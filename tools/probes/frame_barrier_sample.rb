#!/usr/bin/env ruby
# Read-only: what holds frames on a live host. Samples the trace for a
# bounded time and reports, per output, presents and missed refresh
# deadlines, and the Hibr frame barriers armed, released and timed out
# (ADR 0082). Run the same duration before and after a change.
#
#   BLIT_AUTH_TOKEN=... ruby tools/probes/frame_barrier_sample.rb [seconds]
require 'json'
require 'net/http'
require 'uri'
seconds = Float(ARGV.fetch(0, '30'))
abort 'duration must be 5..120 seconds' unless seconds.between?(5, 120)
base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
get = lambda do |path|
  request = Net::HTTP::Get.new(base + path)
  request['Authorization'] = "Bearer #{token}"
  response = Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 5) { |h| h.request(request) }
  abort "GET #{path}: HTTP #{response.code}" unless response.is_a?(Net::HTTPSuccess)
  JSON.parse(response.body)
end
outputs = ->(runtime) { runtime.fetch('snapshot').fetch('render').fetch('outputs') }
# The kind filter matches by prefix: one query returns the barrier's three kinds.
kinds = %w[hibr_frame_barrier kms_frame_retry]
seen = {}
before = outputs.call(get.call('/v1/diagnostics/runtime'))
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
while Process.clock_gettime(Process::CLOCK_MONOTONIC) - started < seconds
  kinds.each do |kind|
    get.call("/v1/diagnostics/trace?kind=#{kind}&limit=128").fetch('events').each do |event|
      seen[[kind, event.fetch('sequence')]] = event
    end
  end
  sleep 0.4
end
after = outputs.call(get.call('/v1/diagnostics/runtime'))
version = get.call('/v1/version').fetch('server_version')
counts = Hash.new(0)
seen.each_value { |event| counts[event.fetch('kind')] += 1 }
puts "FRAME_BARRIER_SAMPLE version=#{version} seconds=#{seconds.round}"
before.zip(after).each do |a, b|
  puts format('  %-14s presents=%d missed=%d', b.fetch('display_id'),
              b.fetch('present_calls') - a.fetch('present_calls'),
              b.fetch('missed_deadlines') - a.fetch('missed_deadlines'))
end
%w[hibr_frame_barrier hibr_frame_barrier_release hibr_frame_barrier_timeout kms_frame_retry].each do |kind|
  puts format('  %-28s %d', kind, counts[kind])
end
