#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0064 stage 5 (#53). An audio device is one ALSA PCM in one direction,
# and the choice of default reaches a launched session.
#
# What /v1/audio reported before: one item per *card*, with direction
# "duplex" when the card had both, `formats` hardcoded to `["s16le"]`, no
# channels, no sample rates, a literal `"revision":1`, and 32 x 32 x 2
# access() calls on the event-loop thread per request -- the same stall shape
# as issue #16. The live host has six cards and nine PCMs, four of them HDMI
# outputs on one card, which a control panel has to be able to tell apart:
# choosing between "HDMI 0" and "HDMI 3" is the whole of choosing an output.
#
# This environment has no /proc/asound and no /dev/snd, so the fixture
# fabricates both under BLIT_ASOUND_ROOT and BLIT_SND_ROOT -- the reads are
# ordinary file reads and the presence checks are access() on regular files.
# The fixture is modelled on what the live host actually publishes: a card id,
# per-PCM info files with a stream direction and a name, and a stream0 with
# separate Playback and Capture sections whose formats differ, which is what
# catches a parser that takes the first match in the file regardless of
# section.

require 'fileutils'
require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19249'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("audio device api gate: missing #{BINARY}") unless File.executable?(BINARY)

STREAM0 = <<~'TEXT'
  HyperX QuadCast at usb-0000:e6:00.4-1.1.3.4.3, full speed : USB Audio
  Playback:
    Status: Stop
    Interface 1
      Altset 1
      Format: S16_LE
      Channels: 2
      Rates: 48000
  Capture:
    Status: Stop
    Interface 2
      Altset 1
      Format: S24_3LE
      Channels: 1
      Rates: 8000, 11025, 16000, 22050, 32000, 44100, 48000
TEXT

def pcm_info(card, device, stream, name)
  "card: #{card}\ndevice: #{device}\nstream: #{stream}\nid: USB Audio\n" \
    "name: #{name}\nsubname: subdevice ##{0}\n"
end

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

def read_exact(socket, length, timeout_seconds)
  data = +''
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout_seconds
  while data.bytesize < length
    remaining = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    raise Timeout::Error if remaining <= 0
    raise Timeout::Error unless IO.select([socket], nil, nil, remaining)

    data << socket.readpartial(length - data.bytesize)
  end
  data
end

def websocket_text(socket, timeout_seconds)
  header = read_exact(socket, 2, timeout_seconds).bytes
  length = header[1] & 0x7f
  length = read_exact(socket, 2, timeout_seconds).unpack1('n') if length == 126
  length = read_exact(socket, 8, timeout_seconds).unpack1('Q>') if length == 127
  read_exact(socket, length, timeout_seconds).force_encoding(Encoding::UTF_8)
end

def open_hardware_stream(port)
  socket = TCPSocket.new('127.0.0.1', port)
  key = ['blit-audio-gate'].pack('m0')
  socket.write("GET /v1/stream?events=hardware HTTP/1.1\r\nHost: 127.0.0.1:#{port}\r\n" \
               "Connection: Upgrade\r\nUpgrade: websocket\r\n" \
               "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: #{key}\r\n\r\n")
  response = +''
  response << read_exact(socket, 1, 3) until response.include?("\r\n\r\n")
  raise "stream handshake failed: #{response.lines.first}" unless
    response.start_with?('HTTP/1.1 101')

  socket
end

def await(timeout: 8)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
  loop do
    value = yield
    return value if value
    return nil if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline

    sleep 0.15
  end
end

def surface_text(surface_id)
  code, snapshot = request('GET', "/v1/surfaces/#{surface_id}/cells")
  return '' unless code == 200

  grid = Hash.new(' ')
  snapshot.fetch('cells').each { |c| grid[[c['x'], c['y']]] = c['text'] || ' ' }
  (0...snapshot.fetch('height')).map do |y|
    (0...snapshot.fetch('width')).map { |x| grid[[x, y]] }.join.rstrip
  end.join("\n")
end

Dir.mktmpdir('blit-audio') do |root|
  asound = File.join(root, 'asound')
  snd = File.join(root, 'snd')
  [File.join(asound, 'card0', 'pcm0p'), File.join(asound, 'card0', 'pcm0c'),
   File.join(asound, 'card0', 'pcm3p'), snd].each do |dir|
    FileUtils.mkdir_p(dir)
  end
  File.write(File.join(asound, 'card0', 'id'), "QuadCast\n")
  File.write(File.join(asound, 'card0', 'stream0'), STREAM0)
  File.write(File.join(asound, 'card0', 'pcm0p', 'info'),
             pcm_info(0, 0, 'PLAYBACK', 'USB Audio'))
  File.write(File.join(asound, 'card0', 'pcm3p', 'info'),
             pcm_info(0, 3, 'PLAYBACK', 'HDMI 3'))
  File.write(File.join(asound, 'card0', 'pcm0c', 'info'),
             pcm_info(0, 0, 'CAPTURE', 'USB Mic'))
  File.write(File.join(snd, 'controlC0'), '')
  # The PCM device nodes are what the server tests for existence, and what the
  # hotplug watch observes; the procfs tree only supplies metadata.
  %w[pcmC0D0p pcmC0D3p pcmC0D0c].each { |n| File.write(File.join(snd, n), '') }

  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'BLIT_ASOUND_ROOT' => asound, 'BLIT_SND_ROOT' => snd,
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

    code, collection = request('GET', '/v1/audio')
    failures << "GET /v1/audio -> #{code}" unless code == 200
    devices = collection.is_a?(Hash) ? collection.fetch('devices') : []

    # Three PCMs, not one card. Two playback devices on one card is the case
    # the per-card inventory could not express at all.
    if devices.length != 3
      failures << "listed #{devices.length} audio devices for three PCMs: " \
                  "#{devices.map { |d| d['id'] }.inspect}"
    end
    playback = devices.select { |d| d['direction'] == 'playback' }
    if playback.length != 2
      failures << "#{playback.length} playback devices, wanted 2: " \
                  "#{playback.map { |d| d['id'] }.inspect}"
    end
    %w[audio-quadcast-pcm0-p audio-quadcast-pcm3-p audio-quadcast-pcm0-c].each do |want|
      failures << "no device with id #{want}: #{devices.map { |d| d['id'] }.inspect}" unless
        devices.any? { |d| d['id'] == want }
    end
    if collection.is_a?(Hash) && collection.fetch('revision') <= 1
      failures << "collection revision is #{collection['revision']}; it must " \
                  'count discoveries, not be a hardcoded 1'
    end

    envelope = %w[id class name node direction kinds formats identity
                  capabilities state configuration revision]
    devices.each do |device|
      missing = envelope.reject { |key| device.key?(key) }
      failures << "#{device['id']}: missing #{missing.inspect}" unless missing.empty?
      failures << "#{device['id']}: state #{device['state'].inspect} is " \
                  'missing present/accessible' unless
        device.dig('state', 'present') == true &&
        device['state'].key?('accessible')
    end

    # The capture device's format must come from the Capture section, not from
    # whichever Format line appears first in the file.
    capture = devices.find { |d| d['direction'] == 'capture' }
    if capture
      failures << "the capture device reports #{capture['formats'].inspect}, " \
                  'which is the playback section\'s format' unless
        capture['formats'] == ['S24_3LE']
      failures << "the capture device reports #{capture['channels'].inspect} " \
                  'channels, wanted 1' unless capture['channels'] == 1
      # ALSA publishes a comma-separated list, and the live QuadCast
      # publishes seven rates. Taking the first integer said the device
      # supported 8 kHz and nothing else -- a claim about the hardware, not a
      # shortcut, and one the first version of this fixture could not catch
      # because it had a single rate.
      failures << "the capture device reports #{capture['sample_rates'].inspect}, " \
                  'wanted every rate on the line' unless
        capture['sample_rates'] == [8000, 11025, 16000, 22050, 32000, 44100, 48000]
      failures << 'a comma-separated rate list was reported as continuous' if
        capture['sample_rates_continuous'] == true
    end
    first_playback = devices.find { |d| d['id'] == 'audio-quadcast-pcm0-p' }
    if first_playback
      failures << "a playback device reports #{first_playback['formats'].inspect}" unless
        first_playback['formats'] == ['S16_LE']
    end

    # One default per direction, and choosing one gives it up elsewhere.
    _, item = request('GET', '/v1/audio/audio-quadcast-pcm0-p')
    code, patched = request('PATCH', '/v1/audio/audio-quadcast-pcm0-p',
                            body: { 'configuration' => { 'default' => true } },
                            headers: { 'If-Match' => item.fetch('revision').to_s })
    failures << "PATCH default -> #{code} #{patched.inspect}" unless code == 200
    failures << 'the default was not recorded' unless
      patched.is_a?(Hash) && patched.dig('configuration', 'default') == true
    _, other = request('GET', '/v1/audio/audio-quadcast-pcm3-p')
    code, moved = request('PATCH', '/v1/audio/audio-quadcast-pcm3-p',
                          body: { 'configuration' => { 'default' => true } },
                          headers: { 'If-Match' => other.fetch('revision').to_s })
    failures << "PATCH the second default -> #{code} #{moved.inspect}" unless code == 200
    _, first_again = request('GET', '/v1/audio/audio-quadcast-pcm0-p')
    failures << 'two playback devices are default at once' if
      first_again.dig('configuration', 'default') == true
    _, capture_item = request('GET', '/v1/audio/audio-quadcast-pcm0-c')
    code, = request('PATCH', '/v1/audio/audio-quadcast-pcm0-c',
                    body: { 'configuration' => { 'default' => true } },
                    headers: { 'If-Match' => capture_item.fetch('revision').to_s })
    failures << "PATCH a capture default -> #{code}" unless code == 200
    _, playback_still = request('GET', '/v1/audio/audio-quadcast-pcm3-p')
    failures << 'choosing a capture default cleared the playback default, so ' \
                'the two directions are not independent' unless
      playback_still.dig('configuration', 'default') == true

    # The refusals.
    code, problem = request('PATCH', '/v1/audio/audio-quadcast-pcm3-p',
                            body: { 'configuration' => { 'volume' => 50 } })
    if code != 400
      failures << "an unknown audio property -> #{code}, wanted 400"
    elsif !problem.is_a?(Hash) || !problem.fetch('detail', '').include?('volume')
      failures << "the refusal does not name the property: #{problem.inspect}"
    end
    code, = request('PATCH', '/v1/audio/audio-quadcast-pcm3-p',
                    body: { 'revision' => 1,
                            'configuration' => { 'default' => false } })
    failures << "a stale body revision -> #{code}, wanted 409" unless code == 409
    code, = request('GET', '/v1/audio/audio-does-not-exist')
    failures << "an unknown audio id -> #{code}, wanted 404" unless code == 404

    # Hotplug. An ALSA card can appear and disappear while Blit runs -- two of
    # the live host's six are USB -- so the inventory cannot be whatever was
    # plugged in at boot. The watch is on the device-node root, because
    # inotify does not report changes under /proc, so the trigger is the
    # control node and not the procfs tree.
    stream = open_hardware_stream(PORT)
    revision_before_plug = nil
    code, before_plug = request('GET', '/v1/audio')
    revision_before_plug = before_plug.fetch('revision') if code == 200
    FileUtils.mkdir_p(File.join(asound, 'card1', 'pcm0p'))
    File.write(File.join(asound, 'card1', 'id'), "Webcam\n")
    File.write(File.join(asound, 'card1', 'pcm0p', 'info'),
               pcm_info(1, 0, 'PLAYBACK', 'USB Audio'))
    # The control node last: it is the event the watch acts on, so creating it
    # after the metadata means the rescan it triggers sees a complete card.
    File.write(File.join(snd, 'pcmC1D0p'), '')
    File.write(File.join(snd, 'controlC1'), '')
    plugged = await do
      _, collection = request('GET', '/v1/audio')
      collection.fetch('devices').any? { |d| d['id'] == 'audio-webcam-pcm0-p' } ?
        collection : nil
    end
    if plugged.nil?
      failures << 'a card created under the scan root was never discovered, ' \
                  'so the audio inventory is whatever was plugged in at boot'
    else
      failures << 'the collection revision did not move on an audio hotplug' unless
        plugged.fetch('revision') > revision_before_plug
    end

    # And it goes away again -- device nodes only, with the procfs tree left
    # behind on purpose.
    #
    # That is the case that decides where presence is tested. The watch is on
    # the device-node directory, because nothing under /proc fires inotify; so
    # if presence were tested on the procfs entry, a rescan triggered by the
    # node disappearing would still find the /proc tree and keep the device
    # present -- and no later event would ever correct it, because the only
    # thing that changed was under /proc. A real card's /proc entry does go
    # away, but not necessarily before the rescan runs, so the two must not be
    # allowed to disagree.
    File.delete(File.join(snd, 'pcmC1D0p'))
    File.delete(File.join(snd, 'controlC1'))
    gone = await do
      _, collection = request('GET', '/v1/audio')
      collection.fetch('devices').none? { |d| d['id'] == 'audio-webcam-pcm0-p' } ?
        collection : nil
    end
    failures << 'an unplugged audio device was never retired' if gone.nil?

    events = []
    begin
      loop { events << JSON.parse(websocket_text(stream, 0.3)) }
    rescue StandardError
      nil
    end
    stream.close
    audio_events = events.select do |e|
      e['type'] == 'device_changed' && e['class'] == 'audio'
    end
    if audio_events.empty?
      failures << 'no audio device_changed event on /v1/stream?events=hardware; ' \
                  "saw #{events.map { |e| [e['type'], e['class']] }.uniq.inspect}"
    else
      kinds = audio_events.map { |e| e['change'] }.uniq
      %w[discovered removed].each do |wanted|
        failures << "no audio device_changed with change=#{wanted}; saw " \
                    "#{kinds.inspect}" unless kinds.include?(wanted)
      end
    end

    # The chosen default reaches a session launched after the choice, which is
    # the whole of what Blit can honour: it has no audio engine and cannot
    # route a stream, and execve means it never reaches a running session.
    code, session = request('POST', '/v1/terminal-sessions',
                            body: { 'command' => '/bin/sh',
                                    'arguments' => ['-c', 'echo "CARD=$ALSA_CARD/$ALSA_PCM_DEVICE/$BLIT_AUDIO_PLAYBACK"; sleep 30'],
                                    'surface_mode' => 'terminal',
                                    'surface' => { 'width' => 100, 'height' => 6 } })
    if code != 201
      failures << "session create -> #{code} #{session.inspect}"
    else
      text = ''
      20.times do
        sleep 0.3
        text = surface_text(session.fetch('surface_id'))
        break if text.include?('CARD=')
      end
      want = 'CARD=QuadCast/3/audio-quadcast-pcm3-p'
      failures << "a session launched after the choice saw #{text.inspect}, " \
                  "wanted #{want.inspect}" unless text.include?(want)
    end
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

unless failures.empty?
  failures.each { |failure| warn "audio device api gate: #{failure}" }
  exit 1
end

puts 'AUDIO_DEVICE_API_PASS pcms=3 per_pcm_not_per_card=yes ' \
     'section_scoped_formats=yes rate_list=7 one_default_per_direction=yes ' \
     'unknown_property=400 stale_revision=409 unknown_id=404 ' \
     'default_exported_to_new_session=yes hotplug=discovered_and_retired ' \
     'stream=device_changed'
