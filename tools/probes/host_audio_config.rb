#!/usr/bin/env ruby
# frozen_string_literal: true
# Live oracle for ADR 0064 stage 5. The local gate runs against a fabricated
# /proc/asound; this runs against the host's real ALSA tree and checks that a
# chosen default reaches a session launched afterwards.
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

def surface_text(surface_id)
  _, d = call('GET', "/v1/surfaces/#{surface_id}/cells")
  return '' unless d.is_a?(Hash)

  grid = Hash.new(' ')
  d.fetch('cells').each { |c| grid[[c['x'], c['y']]] = c['text'] || ' ' }
  (0...d.fetch('height')).map do |y|
    (0...d.fetch('width')).map { |x| grid[[x, y]] }.join.rstrip
  end.join("\n")
end

failures = []
_, inventory = call('GET', '/v1/audio')
devices = inventory.fetch('devices')
puts "#{devices.length} PCMs, collection revision #{inventory.fetch('revision')}"
playback = devices.select { |d| d['direction'] == 'playback' }
capture = devices.select { |d| d['direction'] == 'capture' }
puts "  #{playback.length} playback, #{capture.length} capture"
ids = devices.map { |d| d.fetch('id') }
failures << "duplicate audio ids: #{ids.tally.select { |_, n| n > 1 }.keys.inspect}" unless
  ids.uniq.length == ids.length
# A card with several PCMs is the case the per-card inventory could not
# express; the host's HDA card has four HDMI outputs.
by_card = devices.group_by { |d| d.dig('identity', 'card_id') }
multi = by_card.select { |_, list| list.count { |d| d['direction'] == 'playback' } > 1 }
failures << 'no card with more than one playback PCM, so this host cannot ' \
            'demonstrate per-PCM granularity' if multi.empty?
multi.each do |card, list|
  names = list.select { |d| d['direction'] == 'playback' }.map { |d| d['name'] }
  puts "  card #{card}: #{names.inspect}"
end
devices.each do |d|
  rates = d['sample_rates']
  next if rates.nil?

  failures << "#{d['id']} reports #{rates.inspect}, which is not a list of " \
              'positive integers' unless rates.is_a?(Array) && rates.all? { |r| r.is_a?(Integer) && r > 0 }
end
multirate = devices.select { |d| (d['sample_rates'] || []).length > 1 }
puts "  #{multirate.length} PCM(s) report more than one rate" unless multirate.empty?
multirate.each { |d| puts "    #{d['id']} #{d['sample_rates'].inspect}" }

target = playback.find { |d| (d['sample_rates'] || []).length > 1 } || playback.first
abort 'no playback PCM on the host' if target.nil?
original = devices.find { |d| d.dig('configuration', 'default') == true }

session = nil
begin
  _, item = call('GET', "/v1/audio/#{target.fetch('id')}")
  code, patched = call('PATCH', "/v1/audio/#{target.fetch('id')}",
                       { 'configuration' => { 'default' => true } },
                       { 'If-Match' => item.fetch('revision').to_s })
  failures << "PATCH default -> #{code} #{patched.inspect}" unless code == 200
  code, session = call('POST', '/v1/terminal-sessions',
                       { 'command' => '/bin/sh',
                         'arguments' => ['-c', 'echo "A=$ALSA_CARD/$ALSA_PCM_DEVICE"; sleep 25'],
                         'surface_mode' => 'terminal',
                         'surface' => { 'width' => 90, 'height' => 6 } })
  if code != 201
    failures << "session create -> #{code} #{session.inspect}"
    session = nil
  else
    text = ''
    20.times do
      sleep 0.4
      text = surface_text(session.fetch('surface_id'))
      break if text.include?('A=')
    end
    want = "A=#{target.dig('identity', 'card_id')}/#{target.dig('identity', 'device')}"
    puts "  session saw #{text.split("\n").first.inspect}, wanted #{want.inspect}"
    failures << "the chosen default did not reach a session launched after " \
                "the write: saw #{text.inspect}" unless text.include?(want)
  end
ensure
  call('DELETE', "/v1/terminal-sessions/#{session.fetch('id')}") if session
  # Put the default back where it was found.
  _, item = call('GET', "/v1/audio/#{target.fetch('id')}")
  if item.is_a?(Hash) && item.dig('configuration', 'default') == true &&
     (original.nil? || original.fetch('id') != target.fetch('id'))
    call('PATCH', "/v1/audio/#{target.fetch('id')}",
         { 'configuration' => { 'default' => false } },
         { 'If-Match' => item.fetch('revision').to_s })
  end
  if original && original.fetch('id') != target.fetch('id')
    _, back = call('GET', "/v1/audio/#{original.fetch('id')}")
    call('PATCH', "/v1/audio/#{original.fetch('id')}",
         { 'configuration' => { 'default' => true } },
         { 'If-Match' => back.fetch('revision').to_s }) if back.is_a?(Hash)
  end
end

if failures.empty?
  puts "LIVE_AUDIO_CONFIG_PASS pcms=#{devices.length} playback=#{playback.length} " \
       "capture=#{capture.length} multi_pcm_cards=#{multi.length} " \
       "default_exported=yes"
else
  failures.each { |f| warn "live audio config: #{f}" }
  exit 1
end
