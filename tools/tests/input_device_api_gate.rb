#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0064, stage 1 (#51). /v1/input/devices is a view of the runtime's
# device table, not a scan.
#
# What this asserts, and why each assertion is here:
#
#  * The item envelope. A control panel is written against
#    {id, class, name, node, kinds, route_ids, capabilities, identity, state,
#    configuration, revision}; before this change an item carried only
#    {id, name, kinds, route_ids, capabilities} and said nothing about whether
#    the device was live.
#  * The id is derived from the device, not from its node number. A node with
#    no identity block at all is the one case this environment can produce, so
#    the assertion is that such a device is named `input-node-<basename>` and
#    says so, rather than being given a number that looks stable.
#  * A GET of the collection performs no open(). This route used to run the
#    evdev scan that issue #16 measured at 3.46 s with the event loop stalled
#    behind it, so a controller that polled the inventory froze the server.
#  * The collection revision is real. It was a hardcoded 1, which made hotplug
#    and configuration changes undetectable.
#  * The serialised count agrees with the runtime's own resource count. Every
#    *_json helper in the server breaks out of its loop on overflow and emits a
#    truncated-but-closed object, which is indistinguishable from a host with
#    fewer devices.
#
# The gate environment has no /dev/input and no /dev/uinput, so the devices
# here are fabricated: regular files named event0.. under BLIT_INPUT_ROOT.
# Every evdev ioctl fails on them, which is exactly the "device with no
# identity" path. Real-hardware derivation is gated separately and purely by
# tools/tests/input_identity_gate.c; identity stability across a restart is
# evidenced on the live host.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19246'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
NODES = %w[event0 event1 event3].freeze

abort("input device api gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def get(path, expect: '200')
  uri = BASE + path
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "#{path} -> #{response.code} (wanted #{expect})" unless response.code == expect
  return nil if response.body.to_s.empty?

  JSON.parse(response.body)
end

# A short root: the poll set stores a node path in 128 bytes and matches
# devices by it, so the server refuses a path that would be truncated.
Dir.mktmpdir('blit-dev') do |root|
  NODES.each { |node| File.write(File.join(root, node), '') }
  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'BLIT_INPUT_ROOT' => root,
                   'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
                 BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        begin
          break if Net::HTTP.start(BASE.hostname, BASE.port) { |h|
            h.request(Net::HTTP::Get.new(BASE + '/v1/version'))
          }.code == '200'
        rescue StandardError
          nil
        end
        sleep 0.2
      end
    end

    opens_before = get('/v1/diagnostics/runtime')
                   .fetch('performance').fetch('evdev_scan_opens')
    collection = get('/v1/input/devices')
    opens_after = get('/v1/diagnostics/runtime')
                  .fetch('performance').fetch('evdev_scan_opens')

    if opens_after != opens_before
      failures << "a GET of /v1/input/devices made #{opens_after - opens_before} " \
                  'open() call(s); the route must serve the runtime table and ' \
                  'never rescan (#16)'
    end

    devices = collection.fetch('devices')
    if devices.length != NODES.length
      failures << "listed #{devices.length} devices for #{NODES.length} nodes: " \
                  "#{devices.map { |d| d['id'] }.inspect}"
    end
    if collection.fetch('revision') <= 1
      failures << "collection revision is #{collection.fetch('revision')}; it " \
                  'must count discoveries, not be a hardcoded 1'
    end
    if collection.fetch('count') != devices.length
      failures << "count #{collection.fetch('count')} disagrees with " \
                  "#{devices.length} serialised devices"
    end
    unless collection.fetch('dropped').zero?
      failures << "#{collection.fetch('dropped')} device(s) did not fit the " \
                  'response; a dropped device must never be silent'
    end

    resources = get('/v1/diagnostics/runtime').fetch('snapshot').fetch('resources')
    if resources.fetch('input_devices') != devices.length
      failures << "the runtime counts #{resources.fetch('input_devices')} " \
                  "devices and the response carries #{devices.length}"
    end

    envelope = %w[id class name node kinds route_ids capabilities identity
                  state configuration revision]
    devices.each do |device|
      missing = envelope.reject { |key| device.key?(key) }
      failures << "#{device['id']}: missing #{missing.inspect}" unless missing.empty?
    end

    NODES.each do |node|
      want = "input-node-#{node}"
      device = devices.find { |d| d['id'] == want }
      if device.nil?
        failures << "no device derived the id #{want} from a node with no " \
                    "identity block; got #{devices.map { |d| d['id'] }.inspect}"
        next
      end
      failures << "#{want}: class #{device['class'].inspect}" unless
        device['class'] == 'input'
      failures << "#{want}: node #{device['node'].inspect} does not end in /#{node}" unless
        device['node'].to_s.end_with?("/#{node}")
      # Blit probed the node and reads it under no kind. Reporting it as a
      # keyboard -- which the scanning inventory did, on the live host's
      # consumer-control node -- claims input that never arrives.
      failures << "#{want}: kinds #{device['kinds'].inspect}, wanted [\"other\"]" unless
        device['kinds'] == ['other']
      failures << "#{want}: route_ids #{device['route_ids'].inspect} for a device " \
                  'of no kind' unless device['route_ids'] == []
      state = device.fetch('state', {})
      failures << "#{want}: state #{state.inspect} is missing present/open" unless
        state.key?('present') && state.key?('open')
      failures << "#{want}: present #{state['present'].inspect}" unless
        state['present'] == true
      failures << "#{want}: open #{state['open'].inspect} for a node no kind " \
                  'claimed' unless state['open'] == false
      failures << "#{want}: revision #{device['revision'].inspect} must be a " \
                  'positive integer an If-Match can carry' unless
        device['revision'].is_a?(Integer) && device['revision'] >= 1
    end

    first = devices.first
    begin
      item = first && get("/v1/input/devices/#{first.fetch('id')}")
      # The two reads are seconds apart, so the elapsed-time fields differ by
      # construction. Everything that describes the device must not.
      stable = lambda do |device|
        copy = device.dup
        copy['state'] = device.fetch('state').reject do |key, _|
          %w[last_seen_ms uptime_ms].include?(key)
        end
        copy
      end
      if item && stable.call(item) != stable.call(first)
        failures << 'the item endpoint disagrees with the collection for ' \
                    "#{first.fetch('id')}: #{stable.call(item).inspect} vs " \
                    "#{stable.call(first).inspect}"
      end
    rescue StandardError => error
      failures << "the item endpoint: #{error.message}"
    end
    begin
      get('/v1/input/devices/input-does-not-exist', expect: '404')
    rescue StandardError => error
      failures << "an unknown device id: #{error.message}"
    end
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

unless failures.empty?
  failures.each { |failure| warn "input device api gate: #{failure}" }
  exit 1
end

puts "INPUT_DEVICE_API_PASS nodes=#{NODES.length} envelope=11 " \
     'scan_opens_on_get=0'
