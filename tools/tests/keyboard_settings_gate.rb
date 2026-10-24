#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #66, epic #64 stage 2. The settings a person actually changes about a
# keyboard: how fast a held key repeats, which locks are on when it is plugged
# in, and what a key *is* as against what it types.
#
# What this gate proves and what it cannot:
#
#  * The API contract, fully. The `repeat` capability gating -- a device the
#    kernel does not autorepeat for has no rate to set, so asking is a 422 --
#    the range refusals, the rate-to-period quantisation being reported from
#    the read-back rather than the request, and all of it surviving a restart.
#  * The remap and the boot lock states by their *effect*: keys are injected
#    through POST /v1/input/inject, which runs the same dispatch a real device
#    does, and the session echoes what reached its PTY. A remap that is stored
#    but does not change a byte would pass a state assertion and fail here.
#  * It cannot exercise EVIOCGREP/EVIOCSREP. A fabricated node is a regular
#    file and fails every ioctl, so the sidecar's `repeat=` line stands in for
#    the kernel and this gate is testing that stand-in. The real ioctl
#    round-trip is measured on 192.168.10.54 by tools/probes/host_keyboard.rb,
#    which is where it belongs: the kernel is the thing under test there.
#  * The LED write is skipped for a fabricated node -- it would append event
#    structs to a regular file -- so the lamp is only ever measured on the
#    host. What is measured here is the state the lamp is supposed to agree
#    with, which is the half that decides behaviour.

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19386'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

KEYBOARD = 'input-0003-05ac-0256-usb-0000-00-14.0-4-input0'
POINTER = 'input-0003-046d-c52b-usb-0000-00-14.0-3-input0'
KEY_C = 46
KEY_A = 30
KEY_CAPSLOCK = 58
KEY_LEFTMETA = 125
KEY_KP8 = 72

abort("keyboard-settings gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch, 'PUT' => Net::HTTP::Put,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "kbd-#{rand(1 << 30)}" if method == 'POST'
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
  warn "keyboard-settings gate: FAIL #{message}"
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
  revision = device(id).fetch('revision')
  request('PATCH', "/v1/input/devices/#{id}",
          { 'configuration' => configuration },
          { 'If-Match' => revision.to_s })
end

def key(code, value)
  request('POST', '/v1/input/inject',
          'kind' => 'key', 'code' => code, 'value' => value,
          'device_id' => KEYBOARD)
end

def echoed(surface)
  request('GET', "/v1/surfaces/#{surface}/cells").last.fetch('cells')
         .sort_by { |c| [c.fetch('y', 0), c.fetch('x', 0)] }
         .map { |c| c.fetch('text', ' ') }.join.rstrip
end

# Press a key, optionally holding another first, and return the bytes the
# session saw -- as `cat -v` spells them, so a C0 control reads as ^ plus a
# letter.
def press(surface, code, hold: nil)
  before = echoed($surface)
  key(hold, 1) if hold
  key(code, 1)
  key(code, 0)
  key(hold, 0) if hold
  Timeout.timeout(5) do
    loop do
      text = echoed($surface)
      break text[before.length..] if text.length > before.length

      sleep 0.1
    end
  end
rescue Timeout::Error
  ''
end

INPUT_ROOT = Dir.mktmpdir('blit-kbd')
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
# `repeat=250,33` is what the kernel would have reported: this host's own
# keyboards read exactly that.
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

$server = nil
def start
  $server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                    'BLIT_INPUT_ROOT' => File.join(INPUT_ROOT, 'nodes'),
                    'BLIT_SETTINGS_PATH' => DOCUMENT,
                    'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
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
                      .fetch('devices').length >= 2
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

verdict('repeat-is-reported-only-where-the-kernel-autorepeats') do
  keyboard = device(KEYBOARD).to_h
  check(keyboard.fetch('capabilities', []).include?('repeat'),
        "the keyboard reports EV_REP, got #{keyboard['capabilities'].inspect}")
  check(keyboard.dig('configuration', 'repeat_delay_ms') == 250 &&
        keyboard.dig('configuration', 'repeat_period_ms') == 33 &&
        keyboard.dig('configuration', 'repeat_rate_hz') == 30,
        'and what the kernel has, with the rate derived from the period, got ' \
        "#{keyboard['configuration'].inspect}")
  pointer = device(POINTER).to_h
  check(!pointer.fetch('capabilities', []).include?('repeat'),
        'a pointer does not')
  check(!pointer.fetch('configuration', {}).key?('repeat_delay_ms'),
        'and is not offered a control it cannot use, got ' \
        "#{pointer['configuration'].inspect}")
  code, problem = configure(POINTER, { 'repeat_rate_hz' => 20 })
  check(code == 422 && problem.to_h['code'] == 'device_property_not_applicable',
        "asking anyway is refused and says why, got #{code} #{problem.inspect}")
end

verdict('a-rate-is-quantised-to-the-period-and-the-read-back-is-reported') do
  code, patched = configure(KEYBOARD, { 'repeat_delay_ms' => 400,
                                        'repeat_rate_hz' => 20 })
  check(code == 200, "PATCH -> #{code} #{patched.inspect}")
  configuration = patched.to_h.fetch('configuration', {})
  check(configuration['repeat_delay_ms'] == 400,
        "the delay is kept exactly, got #{configuration['repeat_delay_ms'].inspect}")
  check(configuration['repeat_period_ms'] == 50,
        "20 Hz is a 50 ms period, got #{configuration['repeat_period_ms'].inspect}")
  check(configuration['repeat_rate_hz'] == 20,
        "which reads back as 20 Hz, got #{configuration['repeat_rate_hz'].inspect}")
  # 7 Hz is 142.857 ms, which the kernel can only hold as 142 -- so the
  # read-back is 7 Hz again, and a request of 3 Hz is 333 ms and reads back as
  # 3. The point of reporting the read-back is that the two can differ.
  _, odd = configure(KEYBOARD, { 'repeat_rate_hz' => 7 })
  check(odd.to_h.dig('configuration', 'repeat_period_ms') == 142,
        "7 Hz is a 142 ms period, got #{odd.to_h.dig('configuration', 'repeat_period_ms').inspect}")
  check(odd.to_h.dig('configuration', 'repeat_rate_hz') == 7,
        'and reads back as 7 Hz')
  [[{ 'repeat_delay_ms' => 5 }, 'a delay below the floor'],
   [{ 'repeat_delay_ms' => 20_000 }, 'a delay above the ceiling'],
   [{ 'repeat_rate_hz' => 0 }, 'a rate of zero'],
   [{ 'repeat_rate_hz' => 400 }, 'a rate above the ceiling']].each do |body, label|
    code, problem = configure(KEYBOARD, body)
    check(code == 400 && problem.to_h['code'] == 'invalid_device_configuration',
          "#{label} is refused with the bound named, got #{code} " \
          "#{problem.to_h['detail'].inspect}")
  end
end

# An echoing session, recreated after every restart: a restart takes the
# sessions with it, which is the operational fact issue #63 measured and the
# reason these settings had to become durable in the first place.
def echo_session
  session = request('POST', '/v1/terminal-sessions',
                    'command' => '/bin/sh',
                    'arguments' => ['-c', 'stty -isig 2>/dev/null; cat -v'],
                    'display_id' => 'display-1',
                    'surface' => { 'width' => 120, 'height' => 20 }).last
  sleep 2.0
  session.fetch('surface_id')
end

$surface = echo_session

verdict('a-remapped-key-is-what-it-was-remapped-to') do
  check(press($surface, KEY_C) == 'c', 'a plain c is a c to begin with')
  code, = configure(KEYBOARD, { 'modifiers' => { '58' => 'control',
                                                 '125' => 'none' } })
  check(code == 200, "PATCH modifiers -> #{code}")
  check(press($surface, KEY_C, hold: KEY_CAPSLOCK) == '^C',
        'CapsLock remapped to Control makes caps+c a 0x03')
  # And it is no longer a lock: the keymap never sees the code, because a
  # modifier is not a character.
  key(KEY_CAPSLOCK, 1)
  key(KEY_CAPSLOCK, 0)
  check(press($surface, KEY_A) == 'a',
        'and pressing it alone no longer toggles caps')
  check(press($surface, KEY_LEFTMETA) == '',
        'a key remapped to none types nothing, which is how "disable this ' \
        'key" is said')
  check(device(KEYBOARD).to_h.dig('configuration', 'modifiers') ==
        { '58' => 'control', '125' => 'none' },
        'and the map is reported back, got ' \
        "#{device(KEYBOARD).to_h.dig('configuration', 'modifiers').inspect}")
end

verdict('a-remap-does-not-alias-a-real-modifier') do
  # Holding real Control, then pressing and releasing the remapped CapsLock,
  # must leave Control held. A first design rewrote the code to KEY_LEFTCTRL,
  # which made the two share one held bit and reported Control released here.
  #
  # The premise is asserted first, or this scenario is also true of a server
  # with no remap at all -- real Control would survive an unmapped CapsLock
  # for the trivial reason that nothing happened.
  check(press($surface, KEY_C, hold: KEY_CAPSLOCK) == '^C',
        'the remap is still in force, so there is something to alias')
  key(29, 1)                       # KEY_LEFTCTRL down
  key(KEY_CAPSLOCK, 1)
  key(KEY_CAPSLOCK, 0)
  before = echoed($surface)
  key(KEY_C, 1)
  key(KEY_C, 0)
  text = Timeout.timeout(5) do
    loop do
      current = echoed($surface)
      break current[before.length..] if current.length > before.length

      sleep 0.1
    end
  end
  key(29, 0)
  check(text == '^C',
        "real Control survives a remapped key's release, got #{text.inspect}")
end

verdict('a-boot-lock-state-decides-what-the-keypad-types') do
  configure(KEYBOARD, { 'modifiers' => {} })
  # NumLock on is the default, so the keypad types digits.
  check(press($surface, KEY_KP8) == '8', 'the keypad types digits by default')
  code, = configure(KEYBOARD, { 'num_lock_boot' => false })
  check(code == 200, "PATCH num_lock_boot -> #{code}")
  stop
  start
  $surface = echo_session
  check(device(KEYBOARD).to_h.dig('configuration', 'num_lock_boot') == false,
        'the setting came back, got ' \
        "#{device(KEYBOARD).to_h.dig('configuration', 'num_lock_boot').inspect}")
  check(press($surface, KEY_KP8) == '^[[A',
        'and the keypad now selects navigation, which is the whole point of ' \
        'the setting existing')
end

verdict('keyboard-settings-survive-a-restart') do
  code, = configure(KEYBOARD, { 'repeat_delay_ms' => 300,
                                'repeat_rate_hz' => 25,
                                'caps_lock_boot' => true,
                                'modifiers' => { '58' => 'control' } })
  check(code == 200, "PATCH -> #{code}")
  # Writes coalesce, so the document on disk lags an accepted write by up to
  # the flush interval. Waiting for it is the honest way to read it; asserting
  # immediately would be asserting on whatever the previous scenario left.
  begin
    Timeout.timeout(10) do
      sleep 0.1 until JSON.parse(File.read(DOCUMENT))
                        .dig('input_devices', KEYBOARD, 'repeat_delay_ms') == 300
    end
  rescue Timeout::Error, Errno::ENOENT
    check(false, 'the accepted write reached the document')
  end
  stored = File.exist?(DOCUMENT) ?
    JSON.parse(File.read(DOCUMENT)).fetch('input_devices', {}) : {}
  check(stored.key?(KEYBOARD),
        "the keyboard is stored against its stable id, got #{stored.keys.inspect}")
  check(stored.dig(KEYBOARD, 'repeat_delay_ms') == 300 &&
        stored.dig(KEYBOARD, 'repeat_rate_hz') == 25,
        "with the repeat it was given, got #{stored[KEYBOARD].inspect}")
  stop
  start
  configuration = device(KEYBOARD).to_h.fetch('configuration', {})
  check(configuration['repeat_delay_ms'] == 300 &&
        configuration['repeat_rate_hz'] == 25 &&
        configuration['repeat_period_ms'] == 40,
        "and it is in force again after a restart, got #{configuration.inspect}")
  check(configuration['caps_lock_boot'] == true &&
        configuration['num_lock_boot'] == false,
        "with both lock states, got #{configuration.inspect}")
  check(configuration['modifiers'] == { '58' => 'control' },
        "and the remap, got #{configuration['modifiers'].inspect}")
end

verdict('a-refused-role-leaves-the-map-alone') do
  before = device(KEYBOARD).to_h.dig('configuration', 'modifiers')
  code, problem = configure(KEYBOARD,
                            { 'modifiers' => { '58' => 'hyper' } })
  check(code == 400 && problem.to_h['code'] == 'invalid_device_modifiers',
        "an unknown role is refused and lists the roles, got #{code} " \
        "#{problem.to_h['detail'].inspect}")
  code, problem = configure(KEYBOARD, { 'modifiers' => { '0' => 'control' } })
  check(code == 400, "code 0 is refused, got #{code}")
  code, problem = configure(KEYBOARD,
                            { 'modifiers' => { 'leftctrl' => 'control' } })
  check(code == 400, "a non-numeric code is refused, got #{code}")
  check(device(KEYBOARD).to_h.dig('configuration', 'modifiers') == before,
        'and nothing was applied in the middle of a refused map, got ' \
        "#{device(KEYBOARD)['configuration']['modifiers'].inspect}")
end

unless $failures.empty?
  warn "keyboard-settings gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'KEYBOARD_SETTINGS_PASS'
