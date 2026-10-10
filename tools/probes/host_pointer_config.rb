#!/usr/bin/env ruby
# frozen_string_literal: true
# Live oracle for ADR 0064 stage 2 on a real pointer device. The gate
# environment has no /dev/input, so these two effects -- sensitivity scaling
# relative motion, and disabling releasing the device -- can only be measured
# where real pointers exist.
require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')

def call(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch,
            'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  req['Content-Type'] = 'application/json' if body
  headers.each { |k, v| req[k] = v }
  req.body = JSON.generate(body) if body
  r = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 15) { |h| h.request(req) }
  parsed = begin
    r.body.to_s.empty? ? nil : JSON.parse(r.body)
  rescue JSON::ParserError
    r.body
  end
  [r.code.to_i, parsed]
end

def pointer
  _, d = call('GET', '/v1/diagnostics')
  d.fetch('render').fetch('pointer')
end

_, devices = call('GET', '/v1/input/devices')
target = devices.fetch('devices').find { |x| x['kinds'].include?('pointer') && x['state']['open'] }
abort 'no open pointer device on the host' if target.nil?
id = target.fetch('id')
puts "device  #{id}"
puts "        #{target.fetch('name')}  node=#{target.fetch('node')}"
puts "        configuration #{target.fetch('configuration').inspect}"

failures = []

# Park the pointer somewhere with room to move, then measure one relative
# motion at the default sensitivity and the same motion at 200%.
def measure(id, dx)
  call('POST', '/v1/input/inject', { 'kind' => 'pointer_motion', 'x' => 400, 'y' => 400 })
  before = pointer
  call('POST', '/v1/input/inject',
       { 'kind' => 'pointer_relative', 'dx' => dx, 'device_id' => id })
  after = pointer
  [before.fetch('x'), after.fetch('x'), after.fetch('x') - before.fetch('x')]
end

revision = target.fetch('revision')
b1, a1, moved_default = measure(id, 40)
puts "default  100%%: x #{b1} -> #{a1}  moved #{moved_default}"

code, patched = call('PATCH', "/v1/input/devices/#{id}",
                     { 'configuration' => { 'sensitivity_percent' => 200 } },
                     { 'If-Match' => revision.to_s })
failures << "PATCH sensitivity -> #{code} #{patched.inspect}" unless code == 200
if code == 200
  failures << "sensitivity not applied: #{patched.fetch('configuration').inspect}" unless
    patched.fetch('configuration').fetch('sensitivity_percent') == 200
  revision = patched.fetch('revision')
end
b2, a2, moved_200 = measure(id, 40)
puts "scaled   200%%: x #{b2} -> #{a2}  moved #{moved_200}"
unless (moved_200 - moved_default * 2).abs <= 1
  failures << "200% moved #{moved_200} for a 40-count delta; 100% moved " \
              "#{moved_default}, so twice is #{moved_default * 2}"
end

# natural_scroll inverts the reported button.
code, patched = call('PATCH', "/v1/input/devices/#{id}",
                     { 'configuration' => { 'natural_scroll' => true } },
                     { 'If-Match' => revision.to_s })
failures << "PATCH natural_scroll -> #{code} #{patched.inspect}" unless code == 200
revision = patched.fetch('revision') if code == 200
puts "natural_scroll #{code == 200 ? patched.fetch('configuration').fetch('natural_scroll') : 'n/a'}"

# Disabling releases the device: the pointer count drops and the device
# reports itself closed.
_, before_resources = call('GET', '/v1/diagnostics')
pointers_before = before_resources.fetch('resources').fetch('pointers')
code, patched = call('PATCH', "/v1/input/devices/#{id}",
                     { 'configuration' => { 'enabled' => false } },
                     { 'If-Match' => revision.to_s })
failures << "PATCH enabled:false -> #{code} #{patched.inspect}" unless code == 200
revision = patched.fetch('revision') if code == 200
sleep 3
_, after_resources = call('GET', '/v1/diagnostics')
pointers_after = after_resources.fetch('resources').fetch('pointers')
_, item = call('GET', "/v1/input/devices/#{id}")
puts "pointers #{pointers_before} -> #{pointers_after}   open=#{item.dig('state', 'open')}"
failures << "pointers #{pointers_before} -> #{pointers_after}: disabling a " \
            'device must release it, not merely stop reading it' unless
  pointers_after == pointers_before - 1
failures << "the disabled device still reports open=#{item.dig('state', 'open')}" unless
  item.dig('state', 'open') == false
code, = call('POST', '/v1/input/inject',
             { 'kind' => 'pointer_relative', 'dx' => 10, 'device_id' => id })
failures << "inject through the disabled device -> #{code}, wanted 409" unless code == 409

# Put it back exactly as it was.
code, patched = call('PATCH', "/v1/input/devices/#{id}",
                     { 'configuration' => { 'enabled' => true } },
                     { 'If-Match' => revision.to_s })
revision = patched.fetch('revision') if code == 200
code, patched = call('PATCH', "/v1/input/devices/#{id}",
                     { 'configuration' => { 'sensitivity_percent' => 100,
                                            'natural_scroll' => false } },
                     { 'If-Match' => revision.to_s })
sleep 4
_, restored = call('GET', "/v1/input/devices/#{id}")
_, resources = call('GET', '/v1/diagnostics')
puts "restored #{restored.fetch('configuration').inspect} open=#{restored.dig('state', 'open')} " \
     "pointers=#{resources.fetch('resources').fetch('pointers')}"
failures << 'the device was not restored to enabled and claimed' unless
  restored.dig('configuration', 'enabled') == true &&
  restored.dig('state', 'open') == true &&
  restored.dig('configuration', 'sensitivity_percent') == 100

if failures.empty?
  puts "LIVE_POINTER_CONFIG_PASS default=#{moved_default} scaled=#{moved_200} " \
       "pointers=#{pointers_before}->#{pointers_after}->" \
       "#{resources.fetch('resources').fetch('pointers')}"
else
  failures.each { |f| warn "live pointer config: #{f}" }
  exit 1
end
