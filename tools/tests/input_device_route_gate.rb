#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0064 stage 3 (#56). A device's own route wins over the aggregate route
# for its kind, so one keyboard can feed one session while another feeds a
# different one.
#
# Before this, InputRoute.device_id was an enum of three aggregate
# pseudo-devices and every discovered keyboard fed the one keyboard route.
# The live host has four keyboards and three pointers and no way to treat them
# differently, which is the headline thing a control panel wants.
#
# This is the first input test that can run without hardware, because
# POST /v1/input/inject takes a device_id (0.1.434): an injected key travels
# the same modifier state machine, encoder, route resolution and PTY write a
# physical key travels, and the route comes from the device, not from the
# injected event.
#
# The fixture: two shells, two fabricated devices (regular files under
# BLIT_INPUT_ROOT, on which every evdev ioctl fails -- so they are devices of
# no kind, which routing does not care about). Each device is routed to one
# shell; a letter injected through each must appear in that shell's surface
# and not the other's.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19248'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("input device route gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def request(method, path, body: nil, headers: {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch,
            'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(req)
  end
  parsed = begin
    response.body.to_s.empty? ? nil : JSON.parse(response.body)
  rescue JSON::ParserError
    response.body
  end
  [response.code.to_i, parsed]
end

def surface_text(surface_id)
  code, snapshot = request('GET', "/v1/surfaces/#{surface_id}/cells")
  return '' unless code == 200

  grid = Hash.new(' ')
  snapshot.fetch('cells').each do |cell|
    grid[[cell.fetch('x', 0), cell.fetch('y', 0)]] = cell['text'] || ' '
  end
  (0...snapshot.fetch('height')).map do |y|
    (0...snapshot.fetch('width')).map { |x| grid[[x, y]] }.join.rstrip
  end.join("\n")
end

# A shell that echoes one marker per line it reads, so a routed keystroke is
# visible in that session's own surface and nowhere else.
CHILD = <<~'SH'
  stty -echo 2>/dev/null
  while read -r line; do printf 'GOT[%s]\r\n' "$line"; done
SH

def create_session(name)
  code, session = request('POST', '/v1/terminal-sessions',
                          body: { 'command' => '/bin/sh',
                                  'arguments' => ['-c', CHILD],
                                  'surface_mode' => 'terminal',
                                  'surface' => { 'width' => 60, 'height' => 12 } })
  raise "#{name}: session create -> #{code} #{session.inspect}" unless code == 201

  session
end

def inject_letter(device_id, letter)
  code = { 'a' => 30, 'b' => 48 }.fetch(letter)
  %w[press release].each_with_index do |_, index|
    status, = request('POST', '/v1/input/inject',
                      body: { 'kind' => 'key', 'code' => code,
                              'value' => 1 - index, 'device_id' => device_id })
    raise "inject #{letter} -> #{status}" unless status == 202
  end
  # The newline the echo loop reads a line on.
  %w[press release].each_with_index do |_, index|
    request('POST', '/v1/input/inject',
            body: { 'kind' => 'key', 'code' => 28, 'value' => 1 - index,
                    'device_id' => device_id })
  end
end

def await(timeout: 6)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
  loop do
    value = yield
    return value if value
    return nil if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline

    sleep 0.1
  end
end

Dir.mktmpdir('blit-route') do |root|
  %w[event0 event1].each { |node| File.write(File.join(root, node), '') }
  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'BLIT_INPUT_ROOT' => root,
                   'BLIT_COMMAND_ALLOWLIST' => '/bin/sh',
                   'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
                 BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        begin
          break if request('GET', '/v1/version').first == 200
        rescue StandardError
          nil
        end
        sleep 0.2
      end
    end

    first = create_session('first')
    second = create_session('second')
    sleep 1

    device_a = 'input-node-event0'
    device_b = 'input-node-event1'
    [[device_a, first], [device_b, second]].each do |id, session|
      _, item = request('GET', "/v1/input/devices/#{id}")
      revision = item.is_a?(Hash) ? item.fetch('revision') : 0
      code, patched = request('PATCH', "/v1/input/devices/#{id}",
                              body: { 'configuration' => {
                                'route' => {
                                  'target_session_id' => "session-#{session.fetch('id')}",
                                  'enabled' => true
                                }
                              } },
                              headers: { 'If-Match' => revision.to_s })
      if code != 200
        failures << "PATCH route on #{id} -> #{code} #{patched.inspect}"
        next
      end
      route = patched.dig('configuration', 'route')
      failures << "#{id}: route not reported: #{patched.fetch('configuration').inspect}" if
        route.nil?
    end

    # A route to a session that does not exist, and one to a dead session, are
    # refused rather than silently swallowing the device's input.
    code, = request('PATCH', "/v1/input/devices/#{device_a}",
                    body: { 'configuration' => {
                      'route' => { 'target_session_id' => 'session-9999',
                                   'enabled' => true } } })
    failures << "a route to a session that does not exist -> #{code}, wanted 400" unless
      code == 400
    code, = request('PATCH', "/v1/input/devices/#{device_a}",
                    body: { 'configuration' => {
                      'route' => { 'target_session_id' => "session-#{first.fetch('id')}",
                                   'enabled' => true, 'focus' => true } } })
    failures << "an unknown route property -> #{code}, wanted 400" unless code == 400

    inject_letter(device_a, 'a')
    inject_letter(device_b, 'b')

    first_text = await { t = surface_text(first.fetch('surface_id')); t.include?('GOT[') ? t : nil }
    second_text = await { t = surface_text(second.fetch('surface_id')); t.include?('GOT[') ? t : nil }
    first_text ||= surface_text(first.fetch('surface_id'))
    second_text ||= surface_text(second.fetch('surface_id'))

    unless first_text.include?('GOT[a]')
      failures << "the first session did not receive its device's key: " \
                  "#{first_text.inspect}"
    end
    unless second_text.include?('GOT[b]')
      failures << "the second session did not receive its device's key: " \
                  "#{second_text.inspect}"
    end
    if first_text.include?('GOT[b]')
      failures << "the first session received the other device's key, so the " \
                  "route is not per device: #{first_text.inspect}"
    end
    if second_text.include?('GOT[a]')
      failures << "the second session received the other device's key, so the " \
                  "route is not per device: #{second_text.inspect}"
    end

    # A device route that exists but is disabled falls through to the rule
    # that applied before it, rather than blocking the device's input.
    _, item = request('GET', "/v1/input/devices/#{device_b}")
    code, patched = request('PATCH', "/v1/input/devices/#{device_b}",
                            body: { 'configuration' => {
                              'route' => {
                                'target_session_id' => "session-#{second.fetch('id')}",
                                'enabled' => false } } },
                            headers: { 'If-Match' => item.fetch('revision').to_s })
    failures << "disabling a device route -> #{code} #{patched.inspect}" unless code == 200
    if code == 200
      failures << 'a disabled route lost its target, so a panel cannot show ' \
                  "what it pointed at: #{patched.dig('configuration', 'route').inspect}" unless
        patched.dig('configuration', 'route', 'target_session_id') ==
        "session-#{second.fetch('id')}"
      failures << 'a disabled route still reports enabled' if
        patched.dig('configuration', 'route', 'enabled') != false
    end
    before_fallthrough = surface_text(second.fetch('surface_id'))
    inject_letter(device_b, 'b')
    sleep 1
    after_fallthrough = surface_text(second.fetch('surface_id'))
    if after_fallthrough.scan('GOT[b]').length > before_fallthrough.scan('GOT[b]').length
      failures << 'a disabled device route still delivered to its target'
    end

    # A device that is routed is a configured device, so unplugging it must
    # not retire it and lose the route.
    File.delete(File.join(root, 'event0'))
    absent = await do
      _, item = request('GET', "/v1/input/devices/#{device_a}")
      item.is_a?(Hash) && item.dig('state', 'present') == false ? item : nil
    end
    if absent.nil?
      _, item = request('GET', "/v1/input/devices/#{device_a}")
      failures << "a routed device was retired when unplugged: #{item.inspect}"
    elsif absent.dig('configuration', 'route').nil?
      failures << 'an unplugged routed device lost its route: ' \
                  "#{absent.fetch('configuration').inspect}"
    end
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

unless failures.empty?
  failures.each { |failure| warn "input device route gate: #{failure}" }
  exit 1
end

puts 'INPUT_DEVICE_ROUTE_PASS devices=2 sessions=2 keys_landed_per_device=yes ' \
     'unknown_target=400 unknown_route_property=400 disabled_route=falls_through ' \
     'routed_device_survives_unplug=yes'
