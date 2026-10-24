#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #68, epic #64 stage 4. `configuration.default` was the entire writable
# surface of an audio device, so a control panel could not set a volume -- the
# first thing anyone reaches for. The reason it could not is that Blit had
# never opened an ALSA *control* device, only the PCM nodes' presence.
#
# The model this asserts, which is the part worth getting right:
#
#  * The mixer belongs to the **card**, not the PCM. ADR 0064's audio id is
#    per PCM because that is what an application opens, and the live host has
#    four HDMI outputs on one card -- so a mixer held per PCM would be four
#    copies of one thing and a write through one would leave three stale. It
#    is its own resource, and every PCM points at it and says which others
#    share it.
#  * `volume` and `mute` resolve by a *rule*, not a heuristic: Master, else
#    the only playback element, else absent. A card with two candidates and no
#    Master is ambiguous, and guessing would adjust the wrong hardware.
#  * A percentage is converted against the element's own range and what gets
#    reported is the value read back -- the same discipline as a keyboard's
#    repeat rate, and for the same reason.
#
# What this gate cannot do: serve an ioctl. A fabricated control node is a
# regular file, so `controlC<N>.elements` stands in for the card, in the same
# shape and for the same reason as an input device's `.identity`. The real
# SNDRV_CTL_IOCTL_* round-trip is measured on 192.168.10.54 by
# tools/probes/host_audio_controls.rb, where six cards between them exercise
# every branch of the resolution rule. The arithmetic itself is
# tools/tests/audio_control_gate.c.

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19456'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("audio-card gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  req['Idempotency-Key'] = "audio-#{rand(1 << 30)}" if method == 'POST'
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
  warn "audio-card gate: FAIL #{message}"
end

def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

def card(id)
  request('GET', "/v1/audio/cards/#{id}").last.to_h
end

def configure(id, configuration)
  revision = card(id).fetch('revision', 0)
  request('PATCH', "/v1/audio/cards/#{id}",
          { 'configuration' => configuration },
          { 'If-Match' => revision.to_s })
end

def element(id, name)
  card(id).fetch('controls', []).find { |c| c.fetch('name') == name }.to_h
end

ROOTDIR = Dir.mktmpdir('blit-audio')
at_exit { FileUtils.remove_entry(ROOTDIR) rescue nil }
dev = File.join(ROOTDIR, 'dev')
proc_root = File.join(ROOTDIR, 'proc')
Dir.mkdir(dev)
Dir.mkdir(proc_root)

def card_fixture(dev, proc_root, number, id, pcms, elements)
  Dir.mkdir(File.join(proc_root, "card#{number}"))
  File.write(File.join(proc_root, "card#{number}", 'id'), "#{id}\n")
  File.write(File.join(dev, "controlC#{number}"), '')
  pcms.each { |pcm| File.write(File.join(dev, "pcmC#{number}D#{pcm}"), '') }
  return if elements.nil?

  File.write(File.join(dev, "controlC#{number}.elements"), elements)
end

# A card with Master: the rule's first branch, and two PCMs sharing it.
card_fixture(dev, proc_root, 0, 'PCH', %w[0p 0c], <<~ELEMENTS)
  [Master Playback Volume]
  type=integer
  channels=2
  min=0
  max=87
  value=60,60
  [Master Playback Switch]
  type=boolean
  channels=2
  min=0
  max=1
  value=1,1
  [PCM Playback Volume]
  type=integer
  channels=2
  min=0
  max=255
  value=255,255
  [Capture Volume]
  type=integer
  channels=2
  min=0
  max=63
  writable=0
  value=39,39
ELEMENTS

# Two playback volumes and no Master: genuinely ambiguous. Four PCMs, the
# shape the live host's HDMI card has.
card_fixture(dev, proc_root, 1, 'HDMI', %w[0p 1p 2p 3p], <<~ELEMENTS)
  [Mic Playback Volume]
  type=integer
  channels=1
  min=0
  max=36
  value=33
  [Speaker Playback Volume]
  type=integer
  channels=1
  min=0
  max=31
  value=23
ELEMENTS

# A card whose control device cannot be read at all: no sidecar, and a
# regular file serves no ioctl. This is done-when 6 -- the PCM is fine and the
# mixer is not, and the two have to be reported separately.
card_fixture(dev, proc_root, 2, 'USB', %w[0c], nil)

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                 'BLIT_SND_ROOT' => dev, 'BLIT_ASOUND_ROOT' => proc_root,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end
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
  sleep 0.1 until request('GET', '/v1/audio/cards').last.to_h
                    .fetch('cards', []).length >= 3
end

verdict('a-mixer-belongs-to-the-card-and-says-who-shares-it') do
  cards = request('GET', '/v1/audio/cards').last.to_h.fetch('cards', [])
  check(cards.map { |c| c['id'] }.sort == %w[HDMI PCH USB],
        "every card is listed, got #{cards.map { |c| c['id'] }.inspect}")
  hdmi = card('HDMI')
  check(hdmi.fetch('shares_controls_with').length == 4,
        'the four-PCM card says all four share its mixer, got ' \
        "#{hdmi['shares_controls_with'].inspect}")
  # And every PCM points back, so a controller never has to guess which card
  # a device's volume lives on.
  devices = request('GET', '/v1/audio').last.to_h.fetch('devices', [])
  hdmi_pcms = devices.select { |d| d.dig('identity', 'card_id') == 'HDMI' }
  check(hdmi_pcms.length == 4 &&
        hdmi_pcms.all? { |d| d['card_controls'] == '/v1/audio/cards/HDMI' },
        'each of them points at the card, got ' \
        "#{hdmi_pcms.map { |d| d['card_controls'] }.inspect}")
  check(card('PCH').fetch('control_count') == 4,
        "the element count is reported, got #{card('PCH')['control_count']}")
end

verdict('an-element-reports-its-own-range-and-whether-it-is-writable') do
  master = element('PCH', 'Master Playback Volume')
  check(master['type'] == 'integer' && master['channels'] == 2 &&
        master['minimum'] == 0 && master['maximum'] == 87,
        "the range comes from the card, got #{master.inspect}")
  check(master['values'] == [60, 60] && master['percent'] == 69,
        "with its value and a percentage of its own range, got #{master.inspect}")
  switch = element('PCH', 'Master Playback Switch')
  check(switch['type'] == 'boolean' && switch['maximum'] == 1,
        "a switch is boolean, got #{switch.inspect}")
  capture = element('PCH', 'Capture Volume')
  check(capture['writable'] == false,
        "a read-only element says so, got #{capture.inspect}")
  code, problem = configure('PCH', { 'controls' => { 'Capture Volume' => 20 } })
  check(code == 422 && problem.to_h['code'] == 'audio_control_read_only',
        "and writing it is refused rather than silently ignored, got #{code}")
end

verdict('volume-and-mute-resolve-by-a-rule-or-not-at-all') do
  pch = card('PCH').fetch('configuration')
  check(pch['volume_control'] == 'Master Playback Volume',
        "Master wins even where PCM Playback Volume also exists, got #{pch.inspect}")
  check(pch['volume_percent'] == 69,
        "and carries its percentage, got #{pch['volume_percent'].inspect}")
  check(pch['mute'] == false && pch['mute_control'] == 'Master Playback Switch',
        "the switch resolves too, and mute is its inverse, got #{pch.inspect}")
  hdmi = card('HDMI').fetch('configuration')
  check(hdmi == {},
        'two candidates and no Master leaves both absent rather than ' \
        "guessing, got #{hdmi.inspect}")
  code, problem = configure('HDMI', { 'volume_percent' => 50 })
  check(code == 422 && problem.to_h['code'] == 'audio_control_ambiguous',
        "and asking is refused with the reason, got #{code} #{problem.to_h['code'].inspect}")
  check(problem.to_h['detail'].to_s.include?('controls'),
        'and the refusal says to name the element instead, got ' \
        "#{problem.to_h['detail'].inspect}")
end

verdict('a-percentage-is-converted-against-the-elements-own-range') do
  code, patched = configure('PCH', { 'volume_percent' => 50 })
  check(code == 200, "PATCH -> #{code} #{patched.to_h['detail'].inspect}")
  master = patched.to_h.fetch('controls', [])
                  .find { |c| c['name'] == 'Master Playback Volume' }.to_h
  check(master['values'] == [44, 44],
        "50% of 0..87 is 44 on every channel, got #{master['values'].inspect}")
  check(patched.to_h.dig('configuration', 'volume_percent') == 51,
        'and what is reported is the value read back, not the request -- 44 ' \
        "of 0..87 is 51%, got #{patched.to_h.dig('configuration', 'volume_percent').inspect}")
  # The same percentage on a different range is a different raw value, which
  # is the whole reason the conversion is per element.
  configure('PCH', { 'controls' => { 'PCM Playback Volume' => 128 } })
  pcm = element('PCH', 'PCM Playback Volume')
  check(pcm['values'] == [128, 128] && pcm['percent'] == 50,
        "128 of 0..255 is 50%, got #{pcm.inspect}")
  [[-1, 'below zero'], [101, 'above a hundred']].each do |value, label|
    code, problem = configure('PCH', { 'volume_percent' => value })
    check(code == 400 &&
          problem.to_h['code'] == 'invalid_device_configuration',
          "a percentage #{label} is refused with the bound named, got #{code}")
  end
end

verdict('mute-is-the-switchs-inverse') do
  code, patched = configure('PCH', { 'mute' => true })
  check(code == 200, "PATCH mute -> #{code}")
  switch = patched.to_h.fetch('controls', [])
                  .find { |c| c['name'] == 'Master Playback Switch' }.to_h
  check(switch['values'] == [0, 0],
        'muting turns the playback switch off, because an ALSA switch is on ' \
        "when audible, got #{switch['values'].inspect}")
  check(patched.to_h.dig('configuration', 'mute') == true,
        'and is reported as muted')
  _, unmuted = configure('PCH', { 'mute' => false })
  check(unmuted.to_h.fetch('controls', [])
               .find { |c| c['name'] == 'Master Playback Switch' }
               .to_h['values'] == [1, 1],
        'and unmuting turns it back on')
end

verdict('a-named-control-is-written-by-its-own-raw-value') do
  code, patched = configure('HDMI',
                            { 'controls' => { 'Speaker Playback Volume' => 20 } })
  check(code == 200, "PATCH -> #{code} #{patched.to_h['detail'].inspect}")
  speaker = patched.to_h.fetch('controls', [])
                   .find { |c| c['name'] == 'Speaker Playback Volume' }.to_h
  check(speaker['values'] == [20] && speaker['percent'] == 65,
        "20 of 0..31 is 65%, got #{speaker.inspect}")
  # The other candidate is untouched, which is what naming the element buys
  # over a guess.
  check(element('HDMI', 'Mic Playback Volume')['values'] == [33],
        'and the other playback volume is untouched')
  code, problem = configure('HDMI', { 'controls' => { 'Nonexistent' => 1 } })
  check(code == 404 && problem.to_h['code'] == 'audio_control_not_found',
        "a name the card does not have is a 404, got #{code}")
  code, problem = configure('HDMI',
                            { 'controls' => { 'Speaker Playback Volume' => 99 } })
  check(code == 400 && problem.to_h['detail'].to_s.include?('0 through 31'),
        "a value outside the element's range names the range, got #{code} " \
        "#{problem.to_h['detail'].inspect}")
  check(element('HDMI', 'Speaker Playback Volume')['values'] == [20],
        'and nothing was applied from a refused write')
end

verdict('a-card-whose-mixer-cannot-be-read-says-so') do
  # The premise first, or this scenario is also true of a server that reads no
  # mixer at all -- and would pass against the very build it exists to catch.
  check(card('PCH').fetch('controls').length == 4,
        'another card does have its mixer, so "this one does not" says ' \
        "something, got #{card('PCH')['controls'].length}")
  usb = card('USB')
  check(usb.dig('state', 'control_accessible') == false,
        "the control device is reported separately, got #{usb['state'].inspect}")
  check(usb.fetch('controls') == [] && usb.fetch('control_count') == 0,
        "with no controls rather than a crash, got #{usb.inspect}")
  check(usb.fetch('configuration') == {},
        'and nothing to write')
  code, problem = configure('USB', { 'volume_percent' => 50 })
  check(code == 409 && problem.to_h['code'] == 'audio_control_unavailable',
        "a write is refused with the reason, got #{code} #{problem.to_h['code'].inspect}")
  # The PCM is fine even though the mixer is not: done-when 6, a deployment
  # can have one and not the other.
  devices = request('GET', '/v1/audio').last.to_h.fetch('devices', [])
  usb_pcm = devices.find { |d| d.dig('identity', 'card_id') == 'USB' }.to_h
  check(usb_pcm.dig('state', 'accessible') == true &&
        usb_pcm.dig('state', 'control_accessible') == false,
        'the PCM is accessible and the mixer is not, reported separately, ' \
        "got #{usb_pcm['state'].inspect}")
end

verdict('the-write-rules-the-rest-of-blit-uses-hold-here-too') do
  revision = card('PCH').fetch('revision')
  code, = request('PATCH', '/v1/audio/cards/PCH',
                  { 'configuration' => { 'volume_percent' => 40 } },
                  { 'If-Match' => (revision + 9).to_s })
  check(code == 412 || code == 409,
        "a stale If-Match is refused, got #{code}")
  code, problem = configure('PCH', { 'balance' => 10 })
  check(code == 400 && problem.to_h['code'] == 'invalid_device_property',
        "an unknown property is named, got #{code} #{problem.to_h['detail'].inspect}")
  code, = request('PATCH', '/v1/audio/cards/PCH', { 'volume_percent' => 40 })
  check(code == 400,
        "a property outside configuration does not leak in, got #{code}")
  code, = request('GET', '/v1/audio/cards/Nope')
  check(code == 404, "an unknown card is a 404, got #{code}")
end

unless $failures.empty?
  warn "audio-card gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'AUDIO_CARD_PASS'
