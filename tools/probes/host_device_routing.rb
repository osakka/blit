#!/usr/bin/env ruby
# frozen_string_literal: true
# Live oracle for ADR 0064 stage 3 on real keyboards. The local route gate
# uses fabricated devices; this routes two of the host's actual keyboards to
# two different sessions and reads each session's own surface back.
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
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) { |h| h.request(req) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end

def surface_text(surface_id)
  _, d = call('GET', "/v1/surfaces/#{surface_id}/cells")
  return '' unless d.is_a?(Hash)

  grid = Hash.new(' ')
  d.fetch('cells').each { |c| grid[[c['x'], c['y']]] = c['text'] || ' ' }
  (0...d.fetch('height')).map do |y|
    (0...d.fetch('width')).map { |x| grid[[x, y]] }.join.rstrip
  end.join("\n")
end

_, inventory = call('GET', '/v1/input/devices')
keyboards = inventory.fetch('devices')
                     .select { |d| d['kinds'].include?('keyboard') && d['state']['open'] }
abort "need two open keyboards, found #{keyboards.length}" if keyboards.length < 2
a, b = keyboards[0], keyboards[1]
puts "keyboard A #{a['id']}  (#{a['name']})"
puts "keyboard B #{b['id']}  (#{b['name']})"

CHILD = "stty -echo 2>/dev/null; while read -r l; do printf 'GOT[%s]\\r\\n' \"$l\"; done"
sessions = []
failures = []
begin
  2.times do
    code, session = call('POST', '/v1/terminal-sessions',
                         { 'command' => '/bin/sh', 'arguments' => ['-c', CHILD],
                           'surface_mode' => 'terminal',
                           'surface' => { 'width' => 60, 'height' => 8 } })
    abort "session create -> #{code} #{session.inspect}" unless code == 201
    sessions << session
  end
  sleep 2
  [[a, sessions[0]], [b, sessions[1]]].each do |device, session|
    _, item = call('GET', "/v1/input/devices/#{device.fetch('id')}")
    code, patched = call('PATCH', "/v1/input/devices/#{device.fetch('id')}",
                         { 'configuration' => {
                           'route' => { 'target_session_id' => "session-#{session.fetch('id')}",
                                        'enabled' => true } } },
                         { 'If-Match' => item.fetch('revision').to_s })
    failures << "PATCH route on #{device['id']} -> #{code} #{patched.inspect}" unless code == 200
  end
  # 'a' through keyboard A, 'b' through keyboard B, each followed by Enter.
  [[a, 30], [b, 48]].each do |device, letter|
    [[letter, 1], [letter, 0], [28, 1], [28, 0]].each do |code, value|
      status, = call('POST', '/v1/input/inject',
                     { 'kind' => 'key', 'code' => code, 'value' => value,
                       'device_id' => device.fetch('id') })
      failures << "inject #{code}/#{value} -> #{status}" unless status == 202
    end
  end
  sleep 2
  first = surface_text(sessions[0].fetch('surface_id'))
  second = surface_text(sessions[1].fetch('surface_id'))
  puts "session 1 #{first.split("\n").first.inspect}"
  puts "session 2 #{second.split("\n").first.inspect}"
  failures << "session 1 did not get keyboard A's key: #{first.inspect}" unless
    first.include?('GOT[a]')
  failures << "session 2 did not get keyboard B's key: #{second.inspect}" unless
    second.include?('GOT[b]')
  failures << "session 1 received keyboard B's key: #{first.inspect}" if
    first.include?('GOT[b]')
  failures << "session 2 received keyboard A's key: #{second.inspect}" if
    second.include?('GOT[a]')
ensure
  # Remove the routes -- not merely disable them, or the devices stay
  # "configured" and the host is not as it was found -- and remove the
  # sessions.
  [a, b].each do |device|
    _, item = call('GET', "/v1/input/devices/#{device.fetch('id')}")
    next unless item.is_a?(Hash) && item.dig('configuration', 'route')

    code, = call('PATCH', "/v1/input/devices/#{device.fetch('id')}",
                 { 'configuration' => { 'route' => { 'enabled' => false } } },
                 { 'If-Match' => item.fetch('revision').to_s })
    warn "live device routing: could not remove #{device['id']}'s route (#{code})" unless
      code == 200
  end
  sessions.each { |s| call('DELETE', "/v1/terminal-sessions/#{s.fetch('id')}") }
end

if failures.empty?
  puts 'LIVE_DEVICE_ROUTING_PASS keyboards=2 sessions=2 keys_landed_per_device=yes'
else
  failures.each { |f| warn "live device routing: #{f}" }
  exit 1
end
