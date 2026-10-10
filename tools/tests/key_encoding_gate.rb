#!/usr/bin/env ruby
# frozen_string_literal: true

# Issues #34 and, in part, #35. What a physical key press puts on a session's
# PTY, and the modifier state machine behind it.
#
# Keys arrive through POST /v1/input/inject with an evdev code and value, which
# runs the same dispatch a real device does -- modifier state machine,
# counters, PTY write and stream broadcast -- so what this measures is the
# encoder and not a test-only shortcut. There is no /dev/uinput in the build
# container, which is the other reason this path exists.
#
# The session echoes its input with `cat -v`, so the bytes written to the PTY
# come back as cells: ESC reads as ^[ and a C0 control as ^ plus a letter. Its
# tty has signal characters disabled, because this is about byte delivery and
# one of the sequences under test is ^\, which would otherwise raise SIGQUIT
# and kill the thing doing the echoing.
#
# Keys whose bytes are invisible once echoed -- a literal tab or carriage
# return move the cursor and leave no cell -- are not listed; 'a' covers the
# unmodified-literal path.

require 'fileutils'
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19254'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

# linux/input-event-codes.h
KEY = {
  'a' => 30, '1' => 2, 'minus' => 12, 'leftbrace' => 26, 'backslash' => 43,
  'slash' => 53, 'space' => 57, 'tab' => 15, 'enter' => 28,
  'f1' => 59, 'f5' => 63, 'home' => 102, 'pageup' => 104, 'delete' => 111,
  'up' => 103, 'left' => 105, 'right' => 106, 'down' => 108,
  'leftshift' => 42, 'leftctrl' => 29, 'leftalt' => 56,
  'rightshift' => 54, 'rightalt' => 100, 'capslock' => 58, 'numlock' => 69,
  'f11' => 87, 'f12' => 88, 'f13' => 183, 'kp8' => 72, 'kp9' => 73,
  'kpdot' => 83, 'key102nd' => 86
}.freeze

abort("key-encoding gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "key-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("key-encoding gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

def key(code, value, device = nil)
  event = { 'kind' => 'key', 'code' => code, 'value' => value }
  event['device_id'] = device if device
  response = request('POST', '/v1/input/inject', event)
  abort("key-encoding gate: key inject refused: #{response.body}") unless
    response.code.start_with?('2')
end

# Press `code` with the named modifiers held, and return what the session saw.
# `device` attributes every event to one keyboard; `modifier_device` lets the
# modifiers come from a *different* keyboard, which is how per-device modifier
# state is tested.
def press(surface_id, code, modifiers = [], device: nil,
          modifier_device: nil)
  before = echoed(surface_id)
  modifier_device ||= device
  modifiers.each { |name| key(KEY.fetch(name), 1, modifier_device) }
  key(code, 1, device)
  key(code, 0, device)
  modifiers.reverse_each { |name| key(KEY.fetch(name), 0, modifier_device) }
  Timeout.timeout(5) do
    loop do
      text = echoed(surface_id)
      break text[before.length..] if text.length > before.length

      sleep 0.1
    end
  end
rescue Timeout::Error
  ''
end

def echoed(surface_id)
  json('GET', "/v1/surfaces/#{surface_id}/cells").fetch('cells')
      .sort_by { |c| [c.fetch('y', 0), c.fetch('x', 0)] }
      .map { |c| c.fetch('text', ' ') }.join.rstrip
end

# Two fabricated input devices, so an injected key can be attributed to one
# keyboard or the other. ADR 0064 stage 4 made the layout and the modifier
# state per keyboard; without two devices to address, neither can be tested.
INPUT_ROOT = Dir.mktmpdir('blit-keys')
%w[event0 event1].each { |node| File.write(File.join(INPUT_ROOT, node), '') }
DEVICE_A = 'input-node-event0'
DEVICE_B = 'input-node-event1'
at_exit { FileUtils.remove_entry(INPUT_ROOT) rescue nil }

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'BLIT_INPUT_ROOT' => INPUT_ROOT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

session = json('POST', '/v1/terminal-sessions',
               'command' => '/bin/sh',
               'arguments' => ['-c', 'stty -isig 2>/dev/null; cat -v'],
               'display_id' => 'display-1',
               'surface' => { 'width' => 120, 'height' => 20 })
surface = session.fetch('surface_id')
sleep 2.0

failures = []

# code, modifiers, what the application must receive, as `cat -v` spells it.
cases = [
  [KEY['a'], [], 'a', 'an unmodified letter'],
  [KEY['a'], %w[leftshift], 'A', 'shift with a letter'],
  [KEY['a'], %w[leftctrl], '^A', 'ctrl with a letter'],
  [KEY['tab'], %w[leftshift], '^[[Z', 'shift+tab is a back-tab'],
  [KEY['left'], [], '^[[D', 'an unmodified arrow'],
  [KEY['right'], %w[leftctrl], '^[[1;5C', 'ctrl with an arrow'],
  [KEY['up'], %w[leftshift], '^[[1;2A', 'shift with an arrow'],
  [KEY['left'], %w[leftalt], '^[[1;3D', 'alt with an arrow'],
  [KEY['home'], %w[leftctrl], '^[[1;5H', 'ctrl with home'],
  [KEY['f5'], %w[leftshift], '^[[15;2~', 'shift with a function key'],
  [KEY['f1'], [], '^[OP', 'an unmodified F1'],
  [KEY['f1'], %w[leftshift], '^[[1;2P', 'shift with F1'],
  [KEY['delete'], %w[leftctrl], '^[[3;5~', 'ctrl with delete'],
  [KEY['pageup'], %w[leftctrl], '^[[5;5~', 'ctrl with page up'],
  [KEY['leftbrace'], %w[leftctrl], '^[', 'ctrl+[ is ESC'],
  [KEY['backslash'], %w[leftctrl], '^\\', 'ctrl+backslash is 0x1c'],
  [KEY['minus'], %w[leftctrl], '^_', 'ctrl+minus is 0x1f'],
  [KEY['slash'], %w[leftctrl], '^_', 'ctrl+slash is 0x1f'],
  [KEY['space'], %w[leftctrl], '^@', 'ctrl+space is NUL'],
  # #35: keys that previously produced nothing, and one that read past the
  # end of a table because KEY_F11 is not KEY_F1 + 10.
  [KEY['f11'], [], '^[[23~', 'F11'],
  [KEY['f12'], [], '^[[24~', 'F12'],
  [KEY['f13'], [], '^[[25~', 'F13'],
  [KEY['key102nd'], [], '\\', 'the ISO key beside left shift'],
  # AltGr is not Alt: it must not prefix ESC the way the left Alt does.
  [KEY['a'], %w[rightalt], 'a', 'altgr with a letter does not send ESC'],
  [KEY['a'], %w[leftalt], '^[a', 'alt with a letter sends ESC']
]


# The whole main block, unshifted and shifted.
#
# This exists because the gate used to test exactly one letter, 'a', and 'a'
# was the one letter the encoder got right. evdev numbers keys by physical
# position, not alphabetically -- KEY_A is 30, KEY_S is 31, KEY_D is 32 -- and
# the encoder computed a letter as `'a' + code - KEY_A`. So the S key typed
# 'b', the D key typed 'c', the semicolon key typed 'j', the backslash key
# typed 'n', and Q through P and X through M, which lie outside 30..44, typed
# nothing at all. Of thirty main-block keys exactly one produced the right
# character, and the gate's single case was that one.
#
# The row order below is the keyboard's, which is also evdev's code order, so
# a misplaced entry is visible as a key out of place rather than as an index.
MAIN_BLOCK = [
  # code, unshifted, shifted
  [2, '1', '!'], [3, '2', '@'], [4, '3', '#'], [5, '4', '$'], [6, '5', '%'],
  [7, '6', '^'], [8, '7', '&'], [9, '8', '*'], [10, '9', '('], [11, '0', ')'],
  [12, '-', '_'], [13, '=', '+'],
  [16, 'q', 'Q'], [17, 'w', 'W'], [18, 'e', 'E'], [19, 'r', 'R'],
  [20, 't', 'T'], [21, 'y', 'Y'], [22, 'u', 'U'], [23, 'i', 'I'],
  [24, 'o', 'O'], [25, 'p', 'P'], [26, '[', '{'], [27, ']', '}'],
  [30, 'a', 'A'], [31, 's', 'S'], [32, 'd', 'D'], [33, 'f', 'F'],
  [34, 'g', 'G'], [35, 'h', 'H'], [36, 'j', 'J'], [37, 'k', 'K'],
  [38, 'l', 'L'], [39, ';', ':'], [40, "'", '"'], [41, '`', '~'],
  [43, '\\', '|'],
  [44, 'z', 'Z'], [45, 'x', 'X'], [46, 'c', 'C'], [47, 'v', 'V'],
  [48, 'b', 'B'], [49, 'n', 'N'], [50, 'm', 'M'], [51, ',', '<'],
  [52, '.', '>'], [53, '/', '?']
].freeze

MAIN_BLOCK.each do |code, plain, shifted|
  cases << [code, [], plain, "the #{plain.inspect} key"]
  cases << [code, %w[leftshift], shifted, "shift with the #{plain.inspect} key"]
end

cases.each do |code, modifiers, expected, description|
  got = press(surface, code, modifiers)
  warn "  #{description}: #{got.inspect}" if ENV['BLIT_GATE_DEBUG']
  next if got == expected

  failures << "#{description}: expected #{expected.inspect}, got #{got.inspect}"
end

# #35. Modifier state is what is actually held, not the last event seen.
# Holding both shift keys and releasing one used to leave shift off.
key(KEY['leftshift'], 1)
key(KEY['rightshift'], 1)
key(KEY['leftshift'], 0)
both_shifts = press(surface, KEY['a'])
key(KEY['rightshift'], 0)
unless both_shifts == 'A'
  failures << 'with two shift keys held and one released, shift was treated ' \
              "as up: expected \"A\", got #{both_shifts.inspect}"
end

# CapsLock is a lock, not a held modifier, and it inverts shift for letters.
key(KEY['capslock'], 1)
key(KEY['capslock'], 0)
locked = press(surface, KEY['a'])
shifted_while_locked = press(surface, KEY['a'], %w[leftshift])
key(KEY['capslock'], 1)
key(KEY['capslock'], 0)
unlocked = press(surface, KEY['a'])
failures << "capslock did not upper-case a letter: #{locked.inspect}" unless
  locked == 'A'
failures << 'shift with capslock did not lower-case a letter: ' \
            "#{shifted_while_locked.inspect}" unless shifted_while_locked == 'a'
failures << "capslock did not toggle off: #{unlocked.inspect}" unless
  unlocked == 'a'

# NumLock selects between the keypad's digits and its cursor block.
numeric = press(surface, KEY['kp8'])
key(KEY['numlock'], 1)
key(KEY['numlock'], 0)
cursor = press(surface, KEY['kp8'])
delete = press(surface, KEY['kpdot'])
key(KEY['numlock'], 1)
key(KEY['numlock'], 0)
failures << "keypad 8 with numlock on was not a digit: #{numeric.inspect}" unless
  numeric == '8'
failures << "keypad 8 with numlock off was not Up: #{cursor.inspect}" unless
  cursor == '^[[A'
failures << "keypad . with numlock off was not Delete: #{delete.inspect}" unless
  delete == '^[[3~'

# ADR 0064 stage 4. A keyboard's layout is its own.
#
# The fixture is the QWERTZ swap plus the key that carries a third level,
# because those are the two things a US-only translator cannot express: Z and
# Y exchanged, ß where the hyphen is, and AltGr on Q.
QWERTZ = {
  'base' => 'us',
  'keys' => {
    '21' => %w[z Z] + ['', ''],     # the Y key types z
    '44' => %w[y Y] + ['', ''],     # the Z key types y
    '12' => ['ß', '?', '\\', ''],   # where US has the hyphen
    '16' => ['q', 'Q', '@', 'Ω']    # AltGr and shift+AltGr levels
  }
}.freeze

def patch_device(device, configuration)
  response = request('PATCH', "/v1/input/devices/#{device}",
                     'configuration' => configuration)
  abort("key-encoding gate: PATCH #{device} -> #{response.code} " \
        "#{response.body}") unless response.code == '200'
  JSON.parse(response.body)
end

patched = patch_device(DEVICE_A, 'keymap' => QWERTZ)
failures << "the keymap was not recorded: #{patched['configuration'].inspect}" unless
  patched.dig('configuration', 'keymap', 'override_count') == 4

# Through the keyboard that carries the layout.
[[KEY_Y_POSITION = 21, [], 'z'],
 [21, %w[leftshift], 'Z'],
 [44, [], 'y'],
 [12, [], 'ß'],
 [12, %w[leftshift], '?'],
 [16, [], 'q'],
 [16, %w[rightalt], '@'],
 [16, %w[rightalt leftshift], 'Ω']].each do |code, modifiers, expected|
  got = press(surface, code, modifiers, device: DEVICE_A)
  next if got == expected

  failures << "code #{code}#{modifiers.empty? ? '' : '+' + modifiers.join('+')} " \
              "on the keyboard with the layout: expected #{expected.inspect}, " \
              "got #{got.inspect}"
end

# The other keyboard has no layout, so the same codes are US. This is the
# assertion that the layout belongs to a device and not to the server.
[[21, 'y'], [44, 'z'], [12, '-']].each do |code, expected|
  got = press(surface, code, [], device: DEVICE_B)
  next if got == expected

  failures << "code #{code} on the keyboard without the layout: expected " \
              "#{expected.inspect}, got #{got.inspect} -- a keymap set on one " \
              'device reached another'
end

# AltGr on a key with no third level falls back to the unmodified one, which
# is what AltGr does on a US keyboard where it selects nothing.
altgr_without_level = press(surface, 44, %w[rightalt], device: DEVICE_A)
failures << 'altgr on a key with no third level did not fall back to the ' \
            "unmodified one: #{altgr_without_level.inspect}" unless
  altgr_without_level == 'y'

# ADR 0064 stage 4. Modifier state is a keyboard's own.
#
# Shift held on one keyboard must not modify another keyboard's keys. This was
# one global set, so it did -- invisible until per-device routing made two
# keyboards able to feed two different sessions.
cross = press(surface, KEY['a'], %w[leftshift], device: DEVICE_B,
              modifier_device: DEVICE_A)
failures << 'shift held on one keyboard modified another keyboard\'s key: ' \
            "expected \"a\", got #{cross.inspect}" unless cross == 'a'
same = press(surface, KEY['a'], %w[leftshift], device: DEVICE_B)
failures << 'shift on the same keyboard did not modify its own key: ' \
            "#{same.inspect}" unless same == 'A'

# A lock is a keyboard's own too: the LED is on that keyboard.
key(KEY['capslock'], 1, DEVICE_A)
key(KEY['capslock'], 0, DEVICE_A)
locked_a = press(surface, KEY['a'], [], device: DEVICE_A)
unlocked_b = press(surface, KEY['a'], [], device: DEVICE_B)
key(KEY['capslock'], 1, DEVICE_A)
key(KEY['capslock'], 0, DEVICE_A)
failures << "capslock on one keyboard did not upper-case its own letter: " \
            "#{locked_a.inspect}" unless locked_a == 'A'
failures << 'capslock on one keyboard upper-cased another keyboard\'s ' \
            "letter: #{unlocked_b.inspect}" unless unlocked_b == 'a'

# A modifier press on its own produces no bytes, so it is not a key for the
# delivered/undelivered counters ADR 0057 relies on.
def key_counters
  performance = json('GET', '/v1/diagnostics/runtime').fetch('performance')
  %w[evdev_keyboard_key_presses evdev_keyboard_keys_delivered
     evdev_keyboard_keys_undelivered].map { |k| performance.fetch(k) }
end

before = key_counters
2.times do
  key(KEY['leftshift'], 1)
  key(KEY['leftshift'], 0)
end
sleep 0.3
after = key_counters
unless before == after
  failures << 'pressing a modifier on its own moved the key counters ' \
              "#{before.inspect} -> #{after.inspect}"
end

request('DELETE', "/v1/terminal-sessions/#{session.fetch('id')}")

unless failures.empty?
  failures.each { |f| warn "key-encoding gate: #{f}" }
  exit 1
end

puts "KEY_ENCODING_PASS cases=#{cases.length} keymap=per_device modifiers=per_device both_shifts=held_set " \
     'capslock=toggles numlock=selects_keypad modifier_only_uncounted=yes'
