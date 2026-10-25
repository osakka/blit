#!/usr/bin/env ruby
# frozen_string_literal: true

# The chord that stops the server from the keyboard, asked for directly: when
# the screen is wrong or input is not reaching anything, the operator is in
# front of a machine whose only other control channel is an API they may not
# be able to get to.
#
# What has to hold, and each of these is a way it could be useless or
# dangerous rather than merely absent:
#
#   * the default is Ctrl+Alt+Backspace and it is **on**. A safety valve that
#     ships disabled is discovered at exactly the wrong moment.
#   * the key alone does nothing. A bare Backspace stopping the server would
#     be far worse than no chord at all.
#   * the modifiers alone do nothing, and a near miss does nothing.
#   * it fires on press, once. Not on release, not on autorepeat.
#   * it is programmable, and a reprogrammed chord survives a restart --
#     otherwise the operator who moved it off a chord their desktop uses gets
#     it back after the next update.
#   * disabling it works, which is what a kiosk needs.
#   * the server stops *cleanly*, exit 0, so the unit's Restart=on-failure
#     leaves it stopped rather than bringing it straight back.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19721'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
KEY_BACKSPACE = 14
KEY_LEFTCTRL = 29
KEY_LEFTALT = 56
KEY_LEFTSHIFT = 42
KEY_DELETE = 111

abort("shutdown key gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "zap-#{rand(1 << 30)}" if %w[POST PUT].include?(method)
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 15) { |h| h.request(req) }
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "shutdown key gate: FAIL #{message}"
end

$server = nil
def start(root, settings)
  $server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                    'BLIT_INPUT_ROOT' => root,
                    'BLIT_SETTINGS_PATH' => settings },
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').code == '200'
      rescue StandardError
        nil
      end
      sleep 0.2
    end
  end
end

def alive?
  request('GET', '/v1/version').code == '200'
rescue StandardError
  false
end

# Returns the exit status once the process is gone, or nil if it is still up.
def stopped?(timeout = 6)
  deadline = Time.now + timeout
  while Time.now < deadline
    pid, status = Process.waitpid2($server, Process::WNOHANG)
    return status if pid
    sleep 0.2
  end
  nil
end

# Tolerant by necessity: the press that fires the chord stops the server, so
# the release that follows it has nothing to talk to. That is the correct
# behaviour, not an error.
def key(code, value)
  request('POST', '/v1/input/inject', 'kind' => 'key', 'code' => code,
                                      'value' => value)
rescue StandardError
  nil
end

def chord(*codes)
  codes.each { |c| key(c, 1) }
  codes.reverse.each { |c| key(c, 0) }
end

Dir.mktmpdir('blit-zap') do |base|
  root = File.join(base, 'input')
  Dir.mkdir(root)
  File.write(File.join(root, 'event0.identity'), <<~SIDECAR)
    bustype=3
    vendor=046d
    product=c52b
    version=0111
    phys=usb-0000:00:14.0-3/input0
    name=Zap Keyboard
    kinds=keyboard
    capabilities=keys,repeat
  SIDECAR
  File.write(File.join(root, 'event0'), '')
  settings = File.join(base, 'settings.json')

  # ---- the default, and everything that must NOT fire -------------------
  start(root, settings)
  configured = JSON.parse(request('GET', '/v1/control/shutdown-key').body)
  check(configured['enabled'] == true, "the chord ships disabled: #{configured.inspect}")
  check(configured['code'] == KEY_BACKSPACE,
        "default code: #{configured.inspect}")
  check(configured['modifiers'].sort == %w[alt control],
        "default modifiers: #{configured.inspect}")

  chord(KEY_BACKSPACE)
  check(alive?, 'a bare Backspace stopped the server')
  chord(KEY_LEFTCTRL, KEY_LEFTALT)
  check(alive?, 'Ctrl+Alt with no key stopped the server')
  chord(KEY_LEFTCTRL, KEY_BACKSPACE)
  check(alive?, 'Ctrl+Backspace -- a near miss -- stopped the server')
  chord(KEY_LEFTALT, KEY_BACKSPACE)
  check(alive?, 'Alt+Backspace -- a near miss -- stopped the server')
  chord(KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFTSHIFT, KEY_BACKSPACE)
  # A superset is still the chord: every modifier it names is held.
  status = stopped?
  check(!status.nil?, 'the chord with an extra modifier held did not fire')
  check(status.nil? || status.exitstatus.zero?,
        "the server exited #{status&.exitstatus.inspect}, not 0 -- " \
        'Restart=on-failure would bring it straight back')

  # ---- release and autorepeat do not fire -------------------------------
  start(root, settings)
  key(KEY_LEFTCTRL, 1)
  key(KEY_LEFTALT, 1)
  key(KEY_BACKSPACE, 0)
  check(alive?, 'a Backspace *release* in the chord stopped the server')
  key(KEY_BACKSPACE, 2)
  check(alive?, 'a Backspace *autorepeat* in the chord stopped the server')
  key(KEY_BACKSPACE, 1)
  check(!stopped?.nil?, 'the chord did not fire on press')

  # ---- programmable, and the new chord survives a restart ---------------
  start(root, settings)
  response = request('PUT', '/v1/control/shutdown-key',
                     'code' => KEY_DELETE, 'modifiers' => %w[control shift])
  check(response.code == '200', "reprogramming -> #{response.code} #{response.body}")
  check(JSON.parse(response.body)['code'] == KEY_DELETE,
        "after reprogramming: #{response.body}")
  response = request('PUT', '/v1/control/shutdown-key', 'modifiers' => %w[meta])
  check(response.code == '400',
        "a modifier the chord cannot match must be refused, got #{response.code}")
  sleep 1.0
  Process.kill('TERM', $server)
  Process.wait($server)

  start(root, settings)
  configured = JSON.parse(request('GET', '/v1/control/shutdown-key').body)
  check(configured['code'] == KEY_DELETE &&
        configured['modifiers'].sort == %w[control shift],
        "the reprogrammed chord did not survive a restart: #{configured.inspect}")
  chord(KEY_LEFTCTRL, KEY_LEFTALT, KEY_BACKSPACE)
  check(alive?, 'the old chord still fires after reprogramming')
  chord(KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_DELETE)
  check(!stopped?.nil?, 'the reprogrammed chord did not fire')

  # ---- disabling it -----------------------------------------------------
  start(root, settings)
  request('PUT', '/v1/control/shutdown-key', 'enabled' => false)
  chord(KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_DELETE)
  check(alive?, 'the chord fired while disabled')
  request('PUT', '/v1/control/shutdown-key',
          'enabled' => true, 'code' => KEY_BACKSPACE,
          'modifiers' => %w[control alt])
  chord(KEY_LEFTCTRL, KEY_LEFTALT, KEY_BACKSPACE)
  check(!stopped?.nil?, 're-enabling did not restore the chord')
end

if $failures.empty?
  puts 'SHUTDOWN_KEY_PASS default=ctrl-alt-backspace bare=inert near_miss=inert ' \
       'release=inert autorepeat=inert programmable=yes persisted=yes ' \
       'disable=yes exit=0'
else
  warn "shutdown key gate: #{$failures.length} failure(s)"
  exit 1
end
