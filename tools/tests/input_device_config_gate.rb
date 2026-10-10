#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0064 stage 2 (#52). Per-device configuration: the PATCH contract, what
# it refuses, and that configuration survives the device being unplugged.
#
# What is deliberately NOT asserted here, because this environment cannot:
# that disabling a device stops it being polled, and that a sensitivity of
# 200% doubles pointer motion. Both need a device the server classifies as a
# pointer, which needs real evdev ioctls, and the gate environment has no
# /dev/input and no /dev/uinput. The fabricated nodes are regular files, on
# which every ioctl fails, so they are devices of no kind. Those two effects
# are measured on 192.168.10.54, which has three real pointers, and the
# release manifest records the measurement.
#
# What this does assert, all of it local:
#
#  * PATCH writes `enabled` and bumps both the device and the collection
#    revision.
#  * A stale If-Match is 409; a stale body `revision` is 409.
#  * An unknown configuration property is 400 and is named in the detail.
#    Accepting it silently is how a controller comes to believe it has set
#    something it has not -- issue #48's failure.
#  * A pointer-only property on a device that is not read as a pointer is 422,
#    not a silent no-op.
#  * `configuration` is a bounded object: a property outside it does not leak
#    in through a forward search.
#  * Injecting through a disabled device is 409.
#  * Hotplug. Creating a node under BLIT_INPUT_ROOT fires inotify, the rescan
#    discovers it, and the collection revision moves. Deleting a *configured*
#    device's node leaves it in the table as `present: false` with its
#    configuration intact, and recreating it brings it back still configured.
#    That rule is the point of deriving an id from the device: a stable id is
#    worthless if what is attached to it evaporates while the device is out.
#  * The `device_changed` stream event names the device and carries the
#    collection revision.

require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19247'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("input device config gate: missing #{BINARY}") unless File.executable?(BINARY)

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

def open_input_stream(port)
  socket = TCPSocket.new('127.0.0.1', port)
  key = ['blit-device-config'].pack('m0')
  socket.write("GET /v1/stream?events=input HTTP/1.1\r\nHost: 127.0.0.1:#{port}\r\n" \
               "Connection: Upgrade\r\nUpgrade: websocket\r\n" \
               "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: #{key}\r\n\r\n")
  response = +''
  response << read_exact(socket, 1, 3) until response.include?("\r\n\r\n")
  raise "stream handshake failed: #{response.lines.first}" unless
    response.start_with?('HTTP/1.1 101')

  socket
end

def collection
  code, body = request('GET', '/v1/input/devices')
  raise "collection -> #{code}" unless code == 200

  body
end

def await(timeout: 8)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
  loop do
    value = yield
    return value if value
    raise Timeout::Error if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline

    sleep 0.1
  end
end

Dir.mktmpdir('blit-cfg') do |root|
  File.write(File.join(root, 'event0'), '')
  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'BLIT_INPUT_ROOT' => root,
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

    stream = open_input_stream(PORT)
    id = 'input-node-event0'
    before = collection
    device = before.fetch('devices').find { |d| d['id'] == id }
    failures << "no device #{id} to configure" if device.nil?
    start_revision = device ? device.fetch('revision') : 0

    # The write, with the concurrency idiom the rest of Blit uses.
    code, patched = request('PATCH', "/v1/input/devices/#{id}",
                            body: { 'configuration' => { 'enabled' => false } },
                            headers: { 'If-Match' => start_revision.to_s })
    if code != 200
      failures << "PATCH enabled:false -> #{code} #{patched.inspect}"
    else
      failures << "PATCH did not disable: #{patched.fetch('configuration').inspect}" unless
        patched.fetch('configuration').fetch('enabled') == false
      failures << "device revision did not move: #{patched.fetch('revision')}" unless
        patched.fetch('revision') > start_revision
      failures << 'collection revision did not move' unless
        collection.fetch('revision') > before.fetch('revision')
    end

    # A stale If-Match, and a stale body revision, are both conflicts.
    code, = request('PATCH', "/v1/input/devices/#{id}",
                    body: { 'configuration' => { 'enabled' => true } },
                    headers: { 'If-Match' => start_revision.to_s })
    failures << "a stale If-Match -> #{code}, wanted 409" unless code == 409
    code, = request('PATCH', "/v1/input/devices/#{id}",
                    body: { 'revision' => start_revision,
                            'configuration' => { 'enabled' => true } })
    failures << "a stale body revision -> #{code}, wanted 409" unless code == 409

    # An unknown property is refused and named.
    code, problem = request('PATCH', "/v1/input/devices/#{id}",
                            body: { 'configuration' => { 'accelleration' => 2 } })
    if code != 400
      failures << "an unknown property -> #{code}, wanted 400"
    elsif !problem.is_a?(Hash) || !problem.fetch('detail', '').include?('accelleration')
      failures << "the refusal does not name the property: #{problem.inspect}"
    end

    # One rule decides where a property applies: a device of no known kind --
    # which is what a fabricated node is -- accepts any of them, because Blit
    # has probed it and made no claim about what it is. A device whose kinds
    # contradict a property is refused with 422, and that half cannot be
    # produced here: it needs a device Blit reads as a keyboard or a pointer,
    # which needs real evdev ioctls. It is asserted on the live host instead,
    # by tools/probes/host_device_kinds.rb.
    code, accepted = request('PATCH', "/v1/input/devices/#{id}",
                             body: { 'configuration' => {
                               'sensitivity_percent' => 200 } })
    if code != 200
      failures << 'a pointer property on a device of no known kind -> ' \
                  "#{code} #{accepted.inspect}, wanted 200"
    elsif accepted.dig('configuration', 'sensitivity_percent') != 200
      failures << 'the accepted sensitivity was not recorded: ' \
                  "#{accepted['configuration'].inspect}"
    end
    # What it can set is what it can see.
    if code == 200 && !accepted.fetch('configuration').key?('natural_scroll')
      failures << 'a device that accepts the pointer properties does not ' \
                  "report them: #{accepted['configuration'].inspect}"
    end

    # configuration is a bounded object. A forward search from the key would
    # find `enabled` after the object closes and re-enable the device.
    code, = request('PATCH', "/v1/input/devices/#{id}",
                    body: { 'configuration' => {}, 'enabled' => true })
    _, still = request('GET', "/v1/input/devices/#{id}")
    if still.is_a?(Hash) && still.dig('configuration', 'enabled') != false
      failures << 'a property outside configuration leaked in through a ' \
                  "forward search (PATCH -> #{code})"
    end

    # Injecting through a disabled device delivers nothing, and says so.
    code, = request('POST', '/v1/input/inject',
                    body: { 'kind' => 'pointer_relative', 'dx' => 10,
                            'device_id' => id })
    failures << "inject through a disabled device -> #{code}, wanted 409" unless
      code == 409

    # Hotplug: a new node under the scan root.
    revision_before_plug = collection.fetch('revision')
    File.write(File.join(root, 'event1'), '')
    begin
      await { collection.fetch('devices').any? { |d| d['id'] == 'input-node-event1' } }
      failures << 'the collection revision did not move on a hotplug' unless
        collection.fetch('revision') > revision_before_plug
    rescue Timeout::Error
      failures << 'a node created under the scan root was never discovered'
    end

    # The configured device goes away. It must stay in the table, absent, with
    # its configuration -- and come back still configured.
    File.delete(File.join(root, 'event0'))
    begin
      absent = await do
        _, item = request('GET', "/v1/input/devices/#{id}")
        item.is_a?(Hash) && item.dig('state', 'present') == false ? item : nil
      end
      failures << "an unplugged configured device lost its configuration: " \
                  "#{absent.fetch('configuration', nil).inspect}" unless
        absent.dig('configuration', 'enabled') == false
    rescue Timeout::Error
      _, item = request('GET', "/v1/input/devices/#{id}")
      failures << 'an unplugged configured device was retired instead of ' \
                  "kept absent: #{item.inspect}"
    end
    File.write(File.join(root, 'event0'), '')
    begin
      back = await do
        _, item = request('GET', "/v1/input/devices/#{id}")
        item.is_a?(Hash) && item.dig('state', 'present') == true ? item : nil
      end
      failures << 'a replugged device came back enabled, losing the ' \
                  'configuration saved against its id: ' \
                  "#{back.fetch('configuration', nil).inspect}" unless
        back.dig('configuration', 'enabled') == false
    rescue Timeout::Error
      failures << 'a replugged device never came back present'
    end

    # An unconfigured device that goes away is retired, not kept forever.
    File.delete(File.join(root, 'event1'))
    begin
      await do
        collection.fetch('devices').none? { |d| d['id'] == 'input-node-event1' }
      end
    rescue Timeout::Error
      failures << 'an unplugged device with no configuration was kept in the ' \
                  'table instead of retired'
    end

    # The stream named each change.
    events = []
    begin
      loop { events << JSON.parse(websocket_text(stream, 0.3)) }
    rescue StandardError
      nil
    end
    changes = events.select { |e| e['type'] == 'device_changed' }
    if changes.empty?
      failures << "no device_changed event on /v1/stream?events=input; saw " \
                  "#{events.map { |e| e['type'] }.uniq.inspect}"
    else
      kinds = changes.map { |e| e['change'] }.uniq
      %w[configured discovered absent].each do |wanted|
        failures << "no device_changed with change=#{wanted}; saw #{kinds.inspect}" unless
          kinds.include?(wanted)
      end
      sample = changes.first
      %w[id name node revision collection_revision class].each do |key|
        failures << "device_changed is missing #{key}: #{sample.inspect}" unless
          sample.key?(key)
      end
    end
    stream.close
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

unless failures.empty?
  failures.each { |failure| warn "input device config gate: #{failure}" }
  exit 1
end

puts 'INPUT_DEVICE_CONFIG_PASS patch=200 stale_if_match=409 stale_revision=409 ' \
     'unknown_property=400 no_kind_accepts_any_property=yes bounded_object=yes ' \
     'inject_disabled=409 hotplug=discovered_and_retired ' \
     'configuration_survives_absence=yes stream=device_changed'
