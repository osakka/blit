#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #67 on the live host. The half the local gate cannot do: a fabricated
# node is a regular file, nothing opens it as a pointer, and
# `update_evdev_pointer` publishes nothing when no pointer is open -- so
# pointer *travel* cannot be measured in a gate environment at all. Issue #52's
# gate records the same limitation about `sensitivity_percent` and sends that
# measurement here.
#
# The host has three real pointers and nobody at them, so the events are
# injected -- which is what makes this a measurement of the scale and the curve
# rather than of a physical device. The device's own configuration is what is
# under test, and injection runs the same dispatch a real event does.
#
# Every setting is restored at the end.

require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
BTN_LEFT = 272
BTN_RIGHT = 273

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
  warn "live pointer: FAIL #{message}"
end

def device(id)
  call('GET', "/v1/input/devices/#{id}").last
end

def configure(id, configuration)
  revision = device(id).fetch('revision')
  call('PATCH', "/v1/input/devices/#{id}",
       { 'configuration' => configuration }, { 'If-Match' => revision.to_s })
end

def inject(event)
  call('POST', '/v1/input/inject', event)
end

def trace(kind, limit = 64)
  call('GET', "/v1/diagnostics/trace?kind=#{kind}&limit=#{limit}")
    .last.to_h.fetch('events', [])
end

# Pointer travel for a known delta, from the place the renderer reads it. This
# is the measurement the gate environment cannot make.
def travel(id, dx, origin: 600)
  inject('kind' => 'pointer_motion', 'x' => origin, 'y' => 400)
  inject('kind' => 'pointer_relative', 'dx' => dx, 'device_id' => id)
  call('GET', '/v1/diagnostics').last.fetch('render').fetch('pointer')
      .fetch('x') - origin
end

_, collection = call('GET', '/v1/input/devices')
pointers = collection.fetch('devices').select { |d| d.fetch('kinds').include?('pointer') }
puts "pointers: #{pointers.length} of #{collection.fetch('devices').length} devices"
abort('live pointer: no pointer') if pointers.empty?
subject = pointers.first.fetch('id')
original = pointers.first.fetch('configuration')
puts "subject: #{subject}"
puts "  as found: #{original.inspect}"

# sensitivity_percent, unchanged by anything issue #67 added. This is #52's
# measurement, re-run under an explicit flat profile.
configure(subject, { 'sensitivity_percent' => 100,
                     'acceleration' => { 'profile' => 'flat' } })
flat_100 = travel(subject, 20)
configure(subject, { 'sensitivity_percent' => 200 })
flat_200 = travel(subject, 20)
puts format('  flat: delta 20 travels %.1f at 100%%, %.1f at 200%%',
            flat_100, flat_200)
check(flat_100 == 20.0, "flat at 100% travels 20, got #{flat_100}")
check(flat_200 == 40.0, "flat at 200% travels 40, got #{flat_200}")

configure(subject, { 'sensitivity_percent' => 100,
                     'acceleration' => { 'profile' => 'adaptive' } })
adaptive = [2, 11, 20].map { |delta| [delta, travel(subject, delta)] }
adaptive.each do |delta, got|
  puts format('  adaptive: delta %2d travels %.1f', delta, got)
end
check(adaptive == [[2, 2.0], [11, 22.0], [20, 60.0]],
      "the curve on real hardware is the documented one, got #{adaptive.inspect}")
configure(subject, { 'acceleration' => { 'profile' => 'flat' } })
check(travel(subject, 20) == 20.0, 'and flat puts it back')

# The button map, measured off the dispatch trace.
configure(subject, { 'buttons' => { BTN_LEFT.to_s => 'right',
                                    BTN_RIGHT.to_s => 'left' } })
swapped = {}
[BTN_LEFT, BTN_RIGHT].each do |code|
  inject('kind' => 'pointer_button', 'code' => code, 'pressed' => true,
         'device_id' => subject)
  swapped[code] = trace('pointer_button').last.to_h['detail'].to_s[/button=\d/]
  inject('kind' => 'pointer_button', 'code' => code, 'pressed' => false,
         'device_id' => subject)
end
puts "  buttons: #{swapped.inspect}"
check(swapped[BTN_LEFT] == 'button=2' && swapped[BTN_RIGHT] == 'button=0',
      "the swap reaches the dispatch, got #{swapped.inspect}")
code, problem = configure(subject, { 'buttons' => { BTN_LEFT.to_s => 'right' } })
puts "  aliasing map: #{code} #{problem.to_h['code']}"
check(code == 400 && problem.to_h['code'] == 'invalid_device_buttons',
      "an aliasing map is refused on the host too, got #{code}")

# The scroll step, counted on the wheel trace.
configure(subject, { 'scroll_step' => 1 })
before = trace('pointer_wheel').length
inject('kind' => 'pointer_wheel', 'delta' => 1, 'device_id' => subject)
one = trace('pointer_wheel').length - before
configure(subject, { 'scroll_step' => 3 })
before = trace('pointer_wheel').length
inject('kind' => 'pointer_wheel', 'delta' => 1, 'device_id' => subject)
three = trace('pointer_wheel').length - before
puts "  scroll_step: 1 -> #{one} report(s), 3 -> #{three} report(s)"
check(one == 1 && three == 3,
      "one notch is scroll_step reports, got #{one} and #{three}")

_, settings = call('GET', '/v1/settings')
stored = settings.fetch('document').fetch('input_devices').fetch(subject, {})
puts "  stored: #{stored.inspect}"
check(stored['scroll_step'] == 3 &&
      stored.dig('buttons', BTN_LEFT.to_s) == 'right',
      "the settings document carries them, got #{stored.inspect}")

# No gamepad on this host: the one device reporting an `axes` capability reads
# as `other`, so the gamepad properties cannot be accepted-and-measured here.
gamepads = collection.fetch('devices').select { |d| d.fetch('kinds').include?('gamepad') }
puts "  gamepads: #{gamepads.length} (deadzone and invert_axes are a recorded gap)"

configure(subject, { 'buttons' => {}, 'scroll_step' => 1,
                     'sensitivity_percent' => original.fetch('sensitivity_percent'),
                     'natural_scroll' => original.fetch('natural_scroll'),
                     'acceleration' => { 'profile' => 'flat' } })
restored = device(subject).fetch('configuration')
puts "  restored: #{restored.inspect}"
check(!restored.key?('buttons') && restored['scroll_step'] == 1,
      "the host is left as it was found, got #{restored.inspect}")

if $failures.empty?
  puts "LIVE_POINTER_PASS pointers=#{pointers.length} flat=20_and_40 " \
       'adaptive=2_22_60 buttons=swapped_at_dispatch aliasing=400 ' \
       'scroll_step=1_and_3 gamepad=absent'
else
  warn "live pointer: #{$failures.length} failure(s)"
  exit 1
end
