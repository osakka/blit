#!/usr/bin/env ruby
# frozen_string_literal: true
# Live oracle for ADR 0064 stage 4 on real keyboards: a layout belongs to a
# device, and so does its modifier state. The local gate uses fabricated
# nodes; this uses two of the host's actual keyboards.
require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')

def call(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  headers.each { |k, v| req[k] = v }
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 25) { |h| h.request(req) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end

def echoed(surface_id)
  _, d = call('GET', "/v1/surfaces/#{surface_id}/cells")
  return '' unless d.is_a?(Hash)

  d.fetch('cells').sort_by { |c| [c.fetch('y', 0), c.fetch('x', 0)] }
   .map { |c| c.fetch('text', ' ') }.join.rstrip
end

QWERTZ = {
  'base' => 'us',
  'keys' => {
    '21' => %w[z Z] + ['', ''],
    '44' => %w[y Y] + ['', ''],
    '12' => ['ß', '?', '\\', ''],
    '16' => ['q', 'Q', '@', 'Ω']
  }
}.freeze

_, inventory = call('GET', '/v1/input/devices')
keyboards = inventory.fetch('devices')
                     .select { |d| d['kinds'].include?('keyboard') && d['state']['open'] }
abort "need two open keyboards, found #{keyboards.length}" if keyboards.length < 2
a, b = keyboards[0], keyboards[1]
puts "keyboard A #{a['id']}"
puts "keyboard B #{b['id']}"

failures = []
session = nil
begin
  code, session = call('POST', '/v1/terminal-sessions',
                       { 'command' => '/bin/sh',
                         'arguments' => ['-c', 'stty -isig 2>/dev/null; cat -v'],
                         'surface_mode' => 'terminal',
                         'surface' => { 'width' => 120, 'height' => 8 } })
  abort "session create -> #{code} #{session.inspect}" unless code == 201
  surface = session.fetch('surface_id')
  sleep 2

  # Route both keyboards to this probe's own session.
  #
  # Without this the probe only works when no desktop is running: a Hibr
  # desktop owns the focused surface, and ADR 0057 refuses PTY writes to an
  # owner-authored surface, so every key is counted undelivered and types
  # nothing. Measured that way once -- 94 presses, 94 undelivered -- which is
  # the counter doing its job, not a lost key. Routing per device (ADR 0064
  # stage 3) is what makes the probe independent of what else is on screen.
  target = "session-#{session.fetch('id')}"
  [a, b].each do |device|
    _, before = call('GET', "/v1/input/devices/#{device.fetch('id')}")
    code, routed = call('PATCH', "/v1/input/devices/#{device.fetch('id')}",
                        { 'configuration' => {
                          'route' => { 'target_session_id' => target,
                                       'enabled' => true } } },
                        { 'If-Match' => before.fetch('revision').to_s })
    failures << "PATCH route on #{device['id']} -> #{code} #{routed.inspect}" unless
      code == 200
  end

  _, item = call('GET', "/v1/input/devices/#{a.fetch('id')}")
  code, patched = call('PATCH', "/v1/input/devices/#{a.fetch('id')}",
                       { 'configuration' => { 'keymap' => QWERTZ } },
                       { 'If-Match' => item.fetch('revision').to_s })
  failures << "PATCH keymap -> #{code} #{patched.inspect}" unless code == 200
  failures << "the keymap was not recorded: #{patched.inspect}" unless
    patched.is_a?(Hash) &&
    patched.dig('configuration', 'keymap', 'override_count') == 4

  press = lambda do |device, code_value, modifiers|
    before = echoed(surface)
    modifiers.each { |m| call('POST', '/v1/input/inject',
                              { 'kind' => 'key', 'code' => m, 'value' => 1,
                                'device_id' => device }) }
    [1, 0].each { |v| call('POST', '/v1/input/inject',
                           { 'kind' => 'key', 'code' => code_value, 'value' => v,
                             'device_id' => device }) }
    modifiers.reverse.each { |m| call('POST', '/v1/input/inject',
                                      { 'kind' => 'key', 'code' => m, 'value' => 0,
                                        'device_id' => device }) }
    got = ''
    20.times do
      sleep 0.2
      text = echoed(surface)
      if text.length > before.length
        got = text[before.length..]
        break
      end
    end
    got
  end

  [[21, [], 'z'], [44, [], 'y'], [12, [], 'ß'],
   [16, [100], '@'], [16, [100, 42], 'Ω']].each do |code_value, mods, want|
    got = press.call(a.fetch('id'), code_value, mods)
    puts "  A code #{code_value}#{mods.empty? ? '' : "+#{mods.join('+')}"} -> #{got.inspect}"
    failures << "A code #{code_value}: expected #{want.inspect}, got #{got.inspect}" unless
      got == want
  end
  [[21, 'y'], [44, 'z'], [12, '-']].each do |code_value, want|
    got = press.call(b.fetch('id'), code_value, [])
    puts "  B code #{code_value} -> #{got.inspect}"
    failures << "B code #{code_value}: expected #{want.inspect}, got #{got.inspect} " \
                '-- the layout reached a keyboard it was not set on' unless got == want
  end

  # Shift held on A must not modify B's key.
  call('POST', '/v1/input/inject', { 'kind' => 'key', 'code' => 42, 'value' => 1,
                                     'device_id' => a.fetch('id') })
  cross = press.call(b.fetch('id'), 30, [])
  call('POST', '/v1/input/inject', { 'kind' => 'key', 'code' => 42, 'value' => 0,
                                     'device_id' => a.fetch('id') })
  puts "  shift on A, 'a' on B -> #{cross.inspect}"
  failures << "shift held on one keyboard modified another's key: #{cross.inspect}" unless
    cross == 'a'
ensure
  call('DELETE', "/v1/terminal-sessions/#{session.fetch('id')}") if session
  # Leave the host as it was found: no keymap, no route.
  [a, b].each do |device|
    _, item = call('GET', "/v1/input/devices/#{device.fetch('id')}")
    next unless item.is_a?(Hash)

    if item.dig('configuration', 'keymap')
      code, = call('PATCH', "/v1/input/devices/#{device.fetch('id')}",
                   { 'configuration' => { 'keymap' => { 'base' => 'us' } } },
                   { 'If-Match' => item.fetch('revision').to_s })
      warn "live keyboard layout: could not clear #{device['id']}'s keymap " \
           "(#{code})" unless code == 200
      _, item = call('GET', "/v1/input/devices/#{device.fetch('id')}")
    end
    next unless item.is_a?(Hash) && item.dig('configuration', 'route')

    code, = call('PATCH', "/v1/input/devices/#{device.fetch('id')}",
                 { 'configuration' => { 'route' => { 'enabled' => false } } },
                 { 'If-Match' => item.fetch('revision').to_s })
    warn "live keyboard layout: could not remove #{device['id']}'s route " \
         "(#{code})" unless code == 200
  end
end

if failures.empty?
  puts 'LIVE_KEYBOARD_LAYOUT_PASS keyboards=2 layout_per_device=yes ' \
       'altgr_levels=yes modifiers_per_device=yes'
else
  failures.each { |f| warn "live keyboard layout: #{f}" }
  exit 1
end
