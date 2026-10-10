#!/usr/bin/env ruby
# frozen_string_literal: true
# Live oracle for the one rule that decides where a device property applies
# (ADR 0064). The local gate can only produce devices of no known kind, which
# accept everything; the refusal needs a device Blit reads as a keyboard or a
# pointer, which needs real evdev ioctls.
require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')

def call(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) { |h| h.request(req) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end

_, inventory = call('GET', '/v1/input/devices')
devices = inventory.fetch('devices')
keyboard = devices.find { |d| d['kinds'] == ['keyboard'] }
pointer = devices.find { |d| d['kinds'] == ['pointer'] }
other = devices.find { |d| d['kinds'] == ['other'] }
abort 'need a keyboard-only and a pointer-only device' if keyboard.nil? || pointer.nil?
puts "keyboard-only #{keyboard['id']}"
puts "pointer-only  #{pointer['id']}"
puts "no-kind       #{other ? other['id'] : '(none on this host)'}"

failures = []

def expect(failures, what, got, want)
  puts "  #{what} -> #{got}"
  return if got == want

  failures << "#{what}: got #{got}, wanted #{want}"
end

# A property the device's kinds contradict is refused.
code, = call('PATCH', "/v1/input/devices/#{keyboard.fetch('id')}",
             { 'configuration' => { 'sensitivity_percent' => 200 } })
expect(failures, 'pointer property on a keyboard', code, 422)
code, = call('PATCH', "/v1/input/devices/#{pointer.fetch('id')}",
             { 'configuration' => { 'keymap' => { 'base' => 'us' } } })
expect(failures, 'keymap on a pointer', code, 422)

# And an injection may deliver only what that device could have delivered.
code, = call('POST', '/v1/input/inject',
             { 'kind' => 'pointer_relative', 'dx' => 5,
               'device_id' => keyboard.fetch('id') })
expect(failures, 'pointer_relative through a keyboard', code, 422)
code, = call('POST', '/v1/input/inject',
             { 'kind' => 'key', 'code' => 30, 'value' => 0,
               'device_id' => pointer.fetch('id') })
expect(failures, 'key through a pointer', code, 422)

# What a device cannot be written is also not reported.
_, item = call('GET', "/v1/input/devices/#{keyboard.fetch('id')}")
if item.is_a?(Hash) && item.fetch('configuration').key?('sensitivity_percent')
  failures << 'a keyboard reports sensitivity_percent, which it refuses to ' \
              "accept: #{item['configuration'].inspect}"
end
_, item = call('GET', "/v1/input/devices/#{pointer.fetch('id')}")
unless item.is_a?(Hash) && item.fetch('configuration').key?('sensitivity_percent')
  failures << 'a pointer does not report sensitivity_percent: ' \
              "#{item['configuration'].inspect}"
end

# A device of no known kind accepts what Blit has made no claim about.
if other
  code, accepted = call('PATCH', "/v1/input/devices/#{other.fetch('id')}",
                        { 'configuration' => { 'sensitivity_percent' => 150 } })
  expect(failures, 'pointer property on a device of no known kind', code, 200)
  if code == 200
    _, back = call('GET', "/v1/input/devices/#{other.fetch('id')}")
    revision = back.is_a?(Hash) ? back.fetch('revision') : 0
    call('PATCH', "/v1/input/devices/#{other.fetch('id')}",
         { 'revision' => revision,
           'configuration' => { 'sensitivity_percent' => 100 } })
  end
  (accepted || {}).clear if accepted.is_a?(Hash)
end

if failures.empty?
  puts 'LIVE_DEVICE_KINDS_PASS kind_mismatch=422 injection_mismatch=422 ' \
       'reported_equals_writable=yes no_kind_accepts=200'
else
  failures.each { |f| warn "live device kinds: #{f}" }
  exit 1
end
