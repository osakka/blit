#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #66 on the live host. This is the half the local gate cannot do: a
# fabricated node is a regular file and fails every ioctl, so the gate tests a
# stand-in for the kernel. Here the kernel is the thing under test.
#
# It also leaves the host as it found it: every keyboard's repeat is read
# first and restored at the end, because a test box whose keys repeat at 3 Hz
# afterwards is a test box somebody has to fix by hand.

require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')

def call(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  headers.each { |k, v| req[k] = v }
  req['Idempotency-Key'] = rand(1 << 30).to_s if method == 'POST'
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 40) { |h| h.request(req) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "live keyboard: FAIL #{message}"
end

def device(id)
  call('GET', "/v1/input/devices/#{id}").last
end

def configure(id, configuration)
  revision = device(id).fetch('revision')
  call('PATCH', "/v1/input/devices/#{id}",
       { 'configuration' => configuration }, { 'If-Match' => revision.to_s })
end

_, collection = call('GET', '/v1/input/devices')
keyboards = collection.fetch('devices').select do |d|
  d.fetch('kinds').include?('keyboard') &&
    d.fetch('capabilities').include?('repeat')
end
puts "keyboards reporting EV_REP: #{keyboards.length} of " \
     "#{collection.fetch('devices').length} devices"
abort('live keyboard: no keyboard reports EV_REP') if keyboards.empty?

original = {}
keyboards.each do |keyboard|
  id = keyboard.fetch('id')
  configuration = keyboard.fetch('configuration')
  original[id] = [configuration.fetch('repeat_delay_ms'),
                  configuration.fetch('repeat_rate_hz')]
  puts format('  %-50s delay=%4d ms period=%3d ms rate=%3d Hz', id,
              configuration.fetch('repeat_delay_ms'),
              configuration.fetch('repeat_period_ms'),
              configuration.fetch('repeat_rate_hz'))
  check(configuration.fetch('repeat_period_ms') > 0,
        "#{id} reports a period the kernel actually holds")
end

# One keyboard, set and read back through the kernel. 20 Hz is a 50 ms period
# exactly; 7 Hz is 142.857, which the kernel can only hold as 142 -- so the
# read-back is what gets reported and the two can differ.
subject = keyboards.first.fetch('id')
[[400, 20, 50], [250, 7, 142], [180, 30, 33]].each do |delay, rate, period|
  code, patched = configure(subject, { 'repeat_delay_ms' => delay,
                                       'repeat_rate_hz' => rate })
  check(code == 200, "PATCH #{delay}/#{rate} -> #{code} #{patched.inspect}")
  next unless code == 200

  configuration = patched.fetch('configuration')
  puts format('  set delay=%4d rate=%3d -> kernel delay=%4d period=%3d rate=%3d',
              delay, rate, configuration.fetch('repeat_delay_ms'),
              configuration.fetch('repeat_period_ms'),
              configuration.fetch('repeat_rate_hz'))
  check(configuration.fetch('repeat_delay_ms') == delay,
        "the kernel took the delay exactly, got #{configuration['repeat_delay_ms']}")
  check(configuration.fetch('repeat_period_ms') == period,
        "#{rate} Hz is a #{period} ms period, got #{configuration['repeat_period_ms']}")
end

# A refusal where the hardware has no repeat to set.
non_repeat = collection.fetch('devices').find do |d|
  !d.fetch('capabilities').include?('repeat')
end
if non_repeat
  code, problem = configure(non_repeat.fetch('id'), { 'repeat_rate_hz' => 20 })
  puts "  a device without EV_REP: #{code} #{problem.to_h['code']}"
  check(code == 422 && problem.to_h['code'] == 'device_property_not_applicable',
        "a device without EV_REP refuses the property, got #{code}")
end

# The lock states and the LED, which the gate cannot touch at all: it would be
# writing event structs into a regular file.
code, patched = configure(subject, { 'num_lock_boot' => true,
                                     'caps_lock_boot' => false })
check(code == 200, "PATCH lock states -> #{code} #{patched.inspect}")
if code == 200
  puts "  lock states: #{patched.fetch('configuration').slice('caps_lock_boot', 'num_lock_boot')}"
  check(patched.fetch('configuration').fetch('num_lock_boot') == true &&
        patched.fetch('configuration').fetch('caps_lock_boot') == false,
        'both lock states are reported back')
end

_, settings = call('GET', '/v1/settings')
stored = settings.fetch('document').fetch('input_devices').fetch(subject, {})
puts "  stored: #{stored.inspect}"
check(stored['repeat_delay_ms'] == 180 && stored['repeat_rate_hz'] == 30,
      "the repeat is in the settings document, got #{stored.inspect}")
check(stored['num_lock_boot'] == true,
      "and the lock states, got #{stored.inspect}")

# Leave the host as it was found.
configure(subject, { 'repeat_delay_ms' => original[subject][0],
                     'repeat_rate_hz' => original[subject][1] })
restored = device(subject).fetch('configuration')
puts format('  restored delay=%4d rate=%3d', restored.fetch('repeat_delay_ms'),
            restored.fetch('repeat_rate_hz'))
check(restored.fetch('repeat_delay_ms') == original[subject][0],
      'the host is left as it was found')

if $failures.empty?
  puts "LIVE_KEYBOARD_PASS keyboards=#{keyboards.length} " \
       'ioctl=EVIOCGREP_EVIOCSREP quantisation=measured ' \
       'capability_refusal=422 locks=stored'
else
  warn "live keyboard: #{$failures.length} failure(s)"
  exit 1
end
