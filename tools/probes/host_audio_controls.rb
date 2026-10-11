#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #68 on the live host: the real SNDRV_CTL_IOCTL_* round-trip, which no
# gate can do -- a fabricated control node is a regular file and serves no
# ioctl, so the gate reads a stand-in.
#
# This host's six cards between them exercise every branch of the resolution
# rule, which is why the rule is shaped the way it is rather than guessed:
# one has Master, one has only PCM Playback Volume, one has both Mic and
# Speaker Playback Volume and no Master, and three have no playback volume at
# all.
#
# Every value this touches is read first and restored at the end. Blit plays
# no audio, but something else on the host might, and a test box left at 10%
# is a test box somebody has to fix by hand.

require 'json'
require 'net/http'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')

def call(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  headers.each { |k, v| req[k] = v }
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
  warn "live audio: FAIL #{message}"
end

def card(id)
  call('GET', "/v1/audio/cards/#{id}").last.to_h
end

def configure(id, configuration)
  revision = card(id).fetch('revision', 0)
  call('PATCH', "/v1/audio/cards/#{id}",
       { 'configuration' => configuration }, { 'If-Match' => revision.to_s })
end

_, cards = call('GET', '/v1/audio/cards')
listing = cards.to_h.fetch('cards', [])
puts "cards: #{listing.length}"
abort('live audio: no cards') if listing.empty?

resolved = []
ambiguous = []
listing.each do |entry|
  configuration = entry.fetch('configuration')
  puts format('  %-10s %2d elements  accessible=%s  shares=%d  volume=%s',
              entry.fetch('id'), entry.fetch('control_count'),
              entry.dig('state', 'control_accessible'),
              entry.fetch('shares_controls_with').length,
              configuration['volume_control'] || '(none)')
  check(entry.dig('state', 'control_accessible') == true,
        "#{entry['id']} could not be read: the audio group may be missing")
  if configuration['volume_control']
    resolved << entry
  elsif entry.fetch('controls').any? { |c| c['name'].end_with?(' Playback Volume') }
    ambiguous << entry
  end
end

# Every PCM points at its card, and a card with several says which.
_, devices = call('GET', '/v1/audio')
pcms = devices.to_h.fetch('devices', [])
puts "pcms: #{pcms.length}"
check(pcms.all? { |d| d['card_controls'].to_s.start_with?('/v1/audio/cards/') },
      'every PCM points at its card mixer')
shared = listing.max_by { |c| c.fetch('shares_controls_with').length }
puts format('  the most shared mixer: %s, %d PCMs: %s', shared.fetch('id'),
            shared.fetch('shares_controls_with').length,
            shared.fetch('shares_controls_with').inspect)
check(shared.fetch('shares_controls_with').length >= 2,
      'at least one card has several PCMs sharing its mixer, which is the ' \
      'reason the mixer is its own resource')

check(!resolved.empty?,
      'at least one card resolves a playback volume')
puts "  resolved: #{resolved.map { |c| c['id'] }.inspect}"
puts "  ambiguous (playback volumes but no single one): #{ambiguous.map { |c| c['id'] }.inspect}"

ambiguous.each do |entry|
  code, problem = configure(entry.fetch('id'), { 'volume_percent' => 50 })
  check(code == 422 && problem.to_h['code'] == 'audio_control_ambiguous',
        "#{entry['id']} refuses a volume it cannot resolve, got #{code}")
end

# The write path, on a card that resolves. Read, write, read back, restore.
subject = resolved.first
id = subject.fetch('id')
element_name = subject.dig('configuration', 'volume_control')
element = subject.fetch('controls').find { |c| c['name'] == element_name }
original = element.fetch('values')
puts format('  subject: %s %s range=%d..%d channels=%d value=%s',
            id, element_name, element.fetch('minimum'),
            element.fetch('maximum'), element.fetch('channels'),
            original.inspect)

[25, 50, 75].each do |percent|
  code, patched = configure(id, { 'volume_percent' => percent })
  check(code == 200, "PATCH #{percent}% -> #{code} #{patched.to_h['detail'].inspect}")
  next unless code == 200

  written = patched.fetch('controls').find { |c| c['name'] == element_name }
  back = patched.dig('configuration', 'volume_percent')
  puts format('    asked %3d%% -> card holds %-12s -> reads back %3d%%',
              percent, written.fetch('values').inspect, back)
  check(written.fetch('values').uniq.length == 1,
        'every channel took the same value')
  # The settling property, on real hardware: writing the read-back lands on
  # the same raw value.
  _, again = configure(id, { 'volume_percent' => back })
  settled = again.to_h.fetch('controls', [])
                 .find { |c| c['name'] == element_name }.to_h
  check(settled['values'] == written.fetch('values'),
        "and writing the read-back settles rather than drifting: " \
        "#{written.fetch('values').inspect} then #{settled['values'].inspect}")
end

# mute, and that it is the switch's inverse.
if subject.dig('configuration', 'mute_control')
  switch_name = subject.dig('configuration', 'mute_control')
  switch_original = subject.fetch('controls')
                           .find { |c| c['name'] == switch_name }.fetch('values')
  _, muted = configure(id, { 'mute' => true })
  switch = muted.to_h.fetch('controls', []).find { |c| c['name'] == switch_name }.to_h
  puts "    mute true -> #{switch_name} #{switch['values'].inspect}"
  check(switch['values'].to_a.all?(&:zero?),
        "muting turns the playback switch off, got #{switch['values'].inspect}")
  _, unmuted = configure(id, { 'mute' => false })
  check(unmuted.to_h.fetch('controls', []).find { |c| c['name'] == switch_name }
          .to_h['values'] == switch_original,
        'and unmuting restores it')
end

# A named write and an out-of-range refusal, on the element's own range.
code, problem = configure(id, { 'controls' => { element_name => element.fetch('maximum') + 1 } })
puts "    out of range -> #{code} #{problem.to_h['code']}"
check(code == 400,
      "a value past the element's maximum is refused, got #{code}")
code, = configure(id, { 'controls' => { 'No Such Control' => 1 } })
check(code == 404, "an unknown element is a 404, got #{code}")

# Restore.
configure(id, { 'controls' => { element_name => original.first } })
restored = card(id).fetch('controls').find { |c| c['name'] == element_name }
puts "    restored: #{restored.fetch('values').inspect}"
check(restored.fetch('values') == original,
      "the host is left as it was found, got #{restored.fetch('values').inspect}")

if $failures.empty?
  puts "LIVE_AUDIO_PASS cards=#{listing.length} pcms=#{pcms.length} " \
       "resolved=#{resolved.length} ambiguous=#{ambiguous.length} " \
       'ioctl=SNDRV_CTL settles=yes restored=yes'
else
  warn "live audio: #{$failures.length} failure(s)"
  exit 1
end
