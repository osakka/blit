#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #67, epic #64 stage 3. `sensitivity_percent` and `natural_scroll` were
# the whole of a pointer's configuration, and `sensitivity_percent` is a linear
# multiplier -- so there was exactly one acceleration curve and it was the
# identity.
#
# Everything here is measured by its effect rather than its state. A button map
# that is stored but swaps nothing, or a scroll step that is accepted and emits
# one report, would pass a state assertion and fail this gate:
#
#  * buttons   -- injected by evdev *code*, which goes through the same map a
#                 real press does, and read back off the pointer_button trace
#  * scroll_step -- counted on the pointer_wheel trace, not on a surface: a
#                 terminal only sees the wheel if the session is in a mouse
#                 mode, and the trace is where the reports actually are
#  * acceleration -- measured as pointer travel for a known delta, which is
#                 why the curve is defined on the delta's magnitude rather
#                 than a speed: it makes the arithmetic exact
#
# What it cannot do: there is no gamepad injection kind, so the deadzone and
# axis inversion are asserted as an API contract -- accepted on a gamepad,
# refused on a pointer, persisted -- and not as an effect. The live host has no
# gamepad either (its one `axes` device reports as `other`), so that half is a
# recorded gap rather than a measurement.

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19406'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

POINTER = 'input-0003-046d-c52b-usb-0000-00-14.0-3-input0'
KEYBOARD = 'input-0003-05ac-0256-usb-0000-00-14.0-4-input0'
GAMEPAD = 'input-0003-045e-028e-usb-0000-00-14.0-5-input0'
BTN_LEFT = 272
BTN_RIGHT = 273
BTN_MIDDLE = 274
BTN_SIDE = 275

abort("pointer-settings gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "ptr-#{rand(1 << 30)}" if method == 'POST'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) do |http|
    http.request(req)
  end
  [response.code.to_i,
   (response.body.to_s.empty? ? nil : (JSON.parse(response.body) rescue response.body))]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "pointer-settings gate: FAIL #{message}"
end

def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

def device(id)
  request('GET', "/v1/input/devices/#{id}").last
end

def configure(id, configuration)
  revision = device(id).to_h.fetch('revision', 0)
  request('PATCH', "/v1/input/devices/#{id}",
          { 'configuration' => configuration },
          { 'If-Match' => revision.to_s })
end

def inject(event)
  request('POST', '/v1/input/inject', event)
end

def trace(kind, limit = 32)
  request('GET', "/v1/diagnostics/trace?kind=#{kind}&limit=#{limit}")
    .last.to_h.fetch('events', [])
end

INPUT_ROOT = Dir.mktmpdir('blit-ptr')
DOCUMENT = File.join(INPUT_ROOT, 'settings.json')
at_exit { FileUtils.remove_entry(INPUT_ROOT) rescue nil }
nodes = File.join(INPUT_ROOT, 'nodes')
Dir.mkdir(nodes)
File.write(File.join(nodes, 'event0'), '')
File.write(File.join(nodes, 'event0.identity'), <<~SIDECAR)
  bustype=3
  vendor=046d
  product=c52b
  version=0111
  phys=usb-0000:00:14.0-3/input0
  name=Fabricated Pointer
  kinds=pointer
  capabilities=rel-x,rel-y,buttons
SIDECAR
File.write(File.join(nodes, 'event1'), '')
File.write(File.join(nodes, 'event1.identity'), <<~SIDECAR)
  bustype=3
  vendor=05ac
  product=0256
  version=0100
  phys=usb-0000:00:14.0-4/input0
  name=Fabricated Keyboard
  kinds=keyboard
  capabilities=keys,repeat
  repeat=250,33
SIDECAR
File.write(File.join(nodes, 'event2'), '')
File.write(File.join(nodes, 'event2.identity'), <<~SIDECAR)
  bustype=3
  vendor=045e
  product=028e
  version=0110
  phys=usb-0000:00:14.0-5/input0
  name=Fabricated Gamepad
  kinds=gamepad
  capabilities=axes
SIDECAR

$server = nil
def start
  $server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                    'BLIT_INPUT_ROOT' => File.join(INPUT_ROOT, 'nodes'),
                    'BLIT_SETTINGS_PATH' => DOCUMENT,
                    'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
                  BINARY, PORT.to_s,
                  out: (ENV['BLIT_GATE_LOG'] ? [ENV['BLIT_GATE_LOG'], 'a'] : '/dev/null'),
                  err: (ENV['BLIT_GATE_LOG'] ? [ENV['BLIT_GATE_LOG'], 'a'] : '/dev/null'))
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').first == 200
      rescue StandardError
        nil
      end
      sleep 0.2
    end
  end
  Timeout.timeout(20) do
    sleep 0.1 until request('GET', '/v1/input/devices').last
                      .fetch('devices').length >= 3
  end
end

def stop
  return if $server.nil?

  Process.kill('TERM', $server) rescue nil
  Process.wait($server) rescue nil
  $server = nil
end
at_exit { stop }

start

verdict('a-pointer-starts-flat-with-one-report-a-notch') do
  configuration = device(POINTER).to_h.fetch('configuration', {})
  check(configuration['scroll_step'] == 1,
        "one report a notch by default, got #{configuration['scroll_step'].inspect}")
  check(configuration['acceleration'] == { 'profile' => 'flat' },
        "and the flat profile, got #{configuration['acceleration'].inspect}")
  check(!configuration.key?('buttons'),
        'and no button map at all, rather than an identity one to read past')
  check(configuration['sensitivity_percent'] == 100,
        "with the sensitivity this always had, got #{configuration['sensitivity_percent'].inspect}")
end

verdict('a-button-map-swaps-what-the-terminal-is-told') do
  # Injected by evdev code, so this goes through the map exactly as a real
  # press does. Without the swap, 272 is the left button and reports 0.
  inject('kind' => 'pointer_button', 'code' => BTN_LEFT, 'pressed' => true,
         'device_id' => POINTER)
  before = trace('pointer_button').last.to_h['detail'].to_s
  check(before.include?('button=0'),
        "BTN_LEFT is the left button to begin with, got #{before.inspect}")
  inject('kind' => 'pointer_button', 'code' => BTN_LEFT, 'pressed' => false,
         'device_id' => POINTER)

  code, = configure(POINTER, { 'buttons' => { BTN_LEFT.to_s => 'right',
                                              BTN_RIGHT.to_s => 'left' } })
  check(code == 200, "PATCH buttons -> #{code}")
  { BTN_LEFT => 'button=2', BTN_RIGHT => 'button=0',
    BTN_MIDDLE => 'button=1' }.each do |evdev, expected|
    inject('kind' => 'pointer_button', 'code' => evdev, 'pressed' => true,
           'device_id' => POINTER)
    detail = trace('pointer_button').last.to_h['detail'].to_s
    check(detail.include?(expected),
          "evdev #{evdev} now reports #{expected}, got #{detail.inspect}")
    inject('kind' => 'pointer_button', 'code' => evdev, 'pressed' => false,
           'device_id' => POINTER)
  end
end

verdict('a-button-mapped-to-none-delivers-nothing') do
  configure(POINTER, { 'buttons' => { BTN_LEFT.to_s => 'right',
                                      BTN_RIGHT.to_s => 'left',
                                      BTN_MIDDLE.to_s => 'none' } })
  before = trace('pointer_button').length
  code, = inject('kind' => 'pointer_button', 'code' => BTN_MIDDLE,
                 'pressed' => true, 'device_id' => POINTER)
  check(code == 202 || code == 200,
        "the injection is accepted rather than refused, got #{code}")
  check(trace('pointer_button').length == before,
        'and nothing was dispatched, which is what disabling a button means')
end

verdict('a-side-button-can-be-made-useful') do
  code, patched = configure(POINTER,
                            { 'buttons' => { BTN_SIDE.to_s => 'middle',
                                             BTN_MIDDLE.to_s => 'none' } })
  check(code == 200, "PATCH -> #{code} #{patched.to_h['detail'].inspect}")
  inject('kind' => 'pointer_button', 'code' => BTN_SIDE, 'pressed' => true,
         'device_id' => POINTER)
  detail = trace('pointer_button').last.to_h['detail'].to_s
  check(detail.include?('button=1'),
        "BTN_SIDE reports the middle button, got #{detail.inspect}")
  inject('kind' => 'pointer_button', 'code' => BTN_SIDE, 'pressed' => false,
         'device_id' => POINTER)
  # A code that is neither a default nor mapped is not a button, and the
  # filter must not widen to all of EV_KEY -- a pointer with keyboard keys
  # would otherwise dispatch letters as clicks.
  code, problem = inject('kind' => 'pointer_button', 'code' => 300,
                         'pressed' => true, 'device_id' => POINTER)
  check(code == 422 &&
        problem.to_h['code'] == 'device_property_not_applicable',
        "an unmapped, non-default code is not a button, got #{code} " \
        "#{problem.to_h['code'].inspect}")
end

verdict('two-codes-cannot-hold-one-button') do
  before = device(POINTER).to_h.dig('configuration', 'buttons')
  # The common mistake: map the left button to right and leave the right one
  # alone, so both produce right and one protocol state is held by two codes.
  code, problem = configure(POINTER, { 'buttons' => { BTN_LEFT.to_s => 'right' } })
  check(code == 400 && problem.to_h['code'] == 'invalid_device_buttons',
        "the aliasing map is refused, got #{code}")
  check(problem.to_h['detail'].to_s.include?('272') &&
        problem.to_h['detail'].to_s.include?('273'),
        "and names both codes, got #{problem.to_h['detail'].inspect}")
  check(device(POINTER).to_h.dig('configuration', 'buttons') == before,
        'and nothing was applied from a refused map')
end

verdict('a-scroll-step-is-how-many-reports-a-notch-produces') do
  configure(POINTER, { 'scroll_step' => 1 })
  before = trace('pointer_wheel', 128).length
  inject('kind' => 'pointer_wheel', 'delta' => 1, 'device_id' => POINTER)
  one = trace('pointer_wheel', 128).length - before
  check(one == 1, "one notch is one report by default, got #{one}")
  code, = configure(POINTER, { 'scroll_step' => 3 })
  check(code == 200, "PATCH scroll_step -> #{code}")
  before = trace('pointer_wheel', 128).length
  inject('kind' => 'pointer_wheel', 'delta' => 1, 'device_id' => POINTER)
  three = trace('pointer_wheel', 128).length - before
  check(three == 3, "and three at scroll_step 3, got #{three}")
  code, problem = configure(POINTER, { 'scroll_step' => 0 })
  check(code == 400 && problem.to_h['code'] == 'invalid_device_configuration',
        "a step of zero is refused with the bound named, got #{code}")
  code, = configure(POINTER, { 'scroll_step' => 11 })
  check(code == 400, "and so is one above the ceiling, got #{code}")
end

# The curve's arithmetic is tools/tests/pointer_curve_gate.c, which asserts it
# at its boundaries without a server, and its effect on a real pointer is
# tools/probes/host_keyboard_settings.rb's sibling on the live host. Neither
# can happen here: a fabricated node is a regular file, nothing opens it as a
# pointer, and update_evdev_pointer deliberately publishes nothing when no
# pointer is open -- issue #52's gate records the same limitation about
# sensitivity_percent. What is asserted here is the contract around the curve.
verdict('a-profile-is-flat-or-adaptive-and-nothing-else') do
  configure(POINTER, { 'scroll_step' => 1, 'sensitivity_percent' => 100,
                       'acceleration' => { 'profile' => 'flat' } })
  check(device(POINTER).to_h.dig('configuration', 'acceleration') ==
        { 'profile' => 'flat' }, 'flat round-trips')
  code, patched = configure(POINTER,
                            { 'acceleration' => { 'profile' => 'adaptive' } })
  check(code == 200 &&
        patched.to_h.dig('configuration', 'acceleration') ==
        { 'profile' => 'adaptive' },
        "and so does adaptive, got #{code} #{patched.to_h['configuration'].inspect}")
  code, problem = configure(POINTER,
                            { 'acceleration' => { 'profile' => 'quadratic' } })
  check(code == 400 && problem.to_h['code'] == 'invalid_device_acceleration',
        "an unknown profile is refused and lists the two, got #{code} " \
        "#{problem.to_h['detail'].inspect}")
  code, problem = configure(POINTER, { 'acceleration' => { 'factor' => 2 } })
  check(code == 400,
        "an acceleration object with no profile is refused, got #{code}")
  check(device(POINTER).to_h.dig('configuration', 'acceleration') ==
        { 'profile' => 'adaptive' },
        'and neither refusal changed the profile in force')
  configure(POINTER, { 'acceleration' => { 'profile' => 'flat' } })
  check(device(POINTER).to_h.dig('configuration', 'sensitivity_percent') == 100,
        'and sensitivity_percent is untouched by any of it, which is what ' \
        'issue #67 promised about the property it did not change')
end

verdict('the-kind-rule-holds-for-every-new-property') do
  [[KEYBOARD, { 'buttons' => { BTN_LEFT.to_s => 'right',
                               BTN_RIGHT.to_s => 'left' } }, 'buttons'],
   [KEYBOARD, { 'scroll_step' => 2 }, 'scroll_step'],
   [KEYBOARD, { 'acceleration' => { 'profile' => 'adaptive' } }, 'acceleration'],
   [POINTER, { 'deadzone_percent' => 10 }, 'deadzone_percent'],
   [POINTER, { 'invert_axes' => [0] }, 'invert_axes']].each do |id, body, label|
    code, problem = configure(id, body)
    check(code == 422 &&
          problem.to_h['code'] == 'device_property_not_applicable',
          "#{label} is refused on the wrong kind of device, got #{code} " \
          "#{problem.to_h['code'].inspect}")
  end
  code, patched = configure(GAMEPAD, { 'deadzone_percent' => 15,
                                       'invert_axes' => [0, 1] })
  check(code == 200, "and accepted on a gamepad, got #{code} #{patched.inspect}")
  configuration = patched.to_h.fetch('configuration', {})
  check(configuration['deadzone_percent'] == 15 &&
        configuration['invert_axes'] == [0, 1],
        "with both reported back, got #{configuration.inspect}")
  code, = configure(GAMEPAD, { 'deadzone_percent' => 60 })
  check(code == 400, "a deadzone above the ceiling is refused, got #{code}")
end

verdict('pointer-and-gamepad-settings-survive-a-restart') do
  configure(POINTER, { 'buttons' => { BTN_LEFT.to_s => 'right',
                                      BTN_RIGHT.to_s => 'left' },
                       'scroll_step' => 4,
                       'acceleration' => { 'profile' => 'adaptive' } })
  # Asserted through GET /v1/settings rather than by reading the file. ADR
  # 0067 makes the document a resource precisely so that where it is kept is
  # an implementation detail, and a gate that reads the path is asserting the
  # detail instead of the contract. The restart below is what proves the
  # bytes reached the disk: a server that had written nothing would come up at
  # the defaults.
  stored = request('GET', '/v1/settings').last.to_h.dig('document',
                                                        'input_devices')
  check(stored.to_h.dig(POINTER, 'buttons') == { BTN_LEFT.to_s => 'right',
                                                 BTN_RIGHT.to_s => 'left' },
        "the map is in the document, got #{stored.to_h[POINTER].inspect}")
  check(stored.to_h.dig(POINTER, 'scroll_step') == 4 &&
        stored.to_h.dig(POINTER, 'acceleration') == { 'profile' => 'adaptive' },
        "with the step and the profile, got #{stored.to_h[POINTER].inspect}")
  check(stored.to_h.dig(GAMEPAD, 'deadzone_percent') == 15 &&
        stored.to_h.dig(GAMEPAD, 'invert_axes') == [0, 1],
        "and the gamepad's, got #{stored.to_h[GAMEPAD].inspect}")
  # Nothing a default produces is in the document, which is what keeps it
  # readable on a machine with twenty-eight devices.
  check(!stored.to_h.fetch(POINTER, {}).key?('sensitivity_percent'),
        'and a property left at its default is not written at all, got ' \
        "#{stored.to_h[POINTER].inspect}")

  stop
  start
  configuration = device(POINTER).to_h.fetch('configuration', {})
  check(configuration['scroll_step'] == 4 &&
        configuration['acceleration'] == { 'profile' => 'adaptive' },
        "all of it is in force again after a restart, got #{configuration.inspect}")
  check(configuration['buttons'] == { BTN_LEFT.to_s => 'right',
                                      BTN_RIGHT.to_s => 'left' },
        "including the map, got #{configuration['buttons'].inspect}")
  inject('kind' => 'pointer_button', 'code' => BTN_LEFT, 'pressed' => true,
         'device_id' => POINTER)
  detail = trace('pointer_button').last.to_h['detail'].to_s
  check(detail.include?('button=2'),
        'and the swap still swaps, measured rather than read back, got ' \
        "#{detail.inspect}")
  inject('kind' => 'pointer_button', 'code' => BTN_LEFT, 'pressed' => false,
         'device_id' => POINTER)
  gamepad = device(GAMEPAD).to_h.fetch('configuration', {})
  check(gamepad['deadzone_percent'] == 15 && gamepad['invert_axes'] == [0, 1],
        "and the gamepad's, got #{gamepad.inspect}")
end

unless $failures.empty?
  warn "pointer-settings gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'POINTER_SETTINGS_PASS'
