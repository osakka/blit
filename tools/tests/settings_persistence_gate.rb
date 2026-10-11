#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0067, epic #64 stage 1. Until this, a device's configuration lived only
# in memory -- ADR 0064 said so deliberately -- and issue #63 measured the
# cost: a supervised update stops and starts the unit, so every pointer speed
# and keyboard layout was lost on every release.
#
# The thing this gate exists to assert is not "a file appears". It is that the
# right things persist, the wrong things do not, and a machine that a stored
# setting has locked out can be recovered. Those three are the whole decision:
#
#   * a setting written against a stable id survives a restart
#   * a setting written against an `input-node-<basename>` id does not, because
#     that id does not survive a restart either and restoring it would land
#     one device's settings on whatever replaced it
#   * a document that cannot be read never stops a boot, and says so where
#     issue #55 taught this host to look
#   * a *valid* setting that disables the only keyboard is recoverable through
#     the API, and recoverable without it
#
# A fabricated node is a regular file, so every identity ioctl fails and the
# device reports nothing -- which is exactly the unstable-id case. The sidecar
# `<node>.identity` supplies what the ioctls would have, read only when
# BLIT_INPUT_ROOT is set, so this gate can have one device of each kind.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19348'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
STABLE = 'input-0003-046d-c52b-usb-0000'
SECOND = 'input-0003-05ac-0256-usb-0000'
UNSTABLE = 'input-node-event1'

abort("settings gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "settings-#{rand(1 << 30)}" if method == 'POST'
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
  warn "settings gate: FAIL #{message}"
end

def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

$server = nil
def start(env)
  $server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                    'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') }
                    .merge(env),
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
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
  # The opener thread adopts the fabricated nodes a moment after the boot
  # scan, and a device acquires its stored settings as it enters the table.
  Timeout.timeout(20) do
    sleep 0.1 until request('GET', '/v1/input/devices').last
                      .fetch('devices').length >= 3
  end
end

def stop
  return if $server.nil?

  Process.kill('TERM', $server) rescue nil
  Process.wait($server) rescue nil
  $server = nil
end
at_exit { stop }

def devices
  request('GET', '/v1/input/devices').last.fetch('devices')
    .to_h { |d| [d.fetch('id'), d] }
end

def sensitivity(id)
  devices[id]&.dig('configuration', 'sensitivity_percent')
end

Dir.mktmpdir('blit-settings') do |root|
  input = File.join(root, 'input')
  Dir.mkdir(input)
  File.write(File.join(input, 'event0'), '')
  # bustype:vendor:product:version:uniq:phys:name -- a device that reports
  # who it is, so ADR 0064 gives it an id derived from that and not from its
  # node number.
  File.write(File.join(input, 'event0.identity'),
             "3:046d:c52b:0111::usb-0000:00:14.0-3/input0:Fabricated Pointer\n")
  File.write(File.join(input, 'event1'), '')
  # A second device with an identity of its own, so that the dormant-entry
  # scenario can hold a refused entry and an accepted one at the same time.
  # Without it, "the refused value was not applied" would also be true of a
  # server that applies nothing at all, and the scenario would pass against
  # the very build it is meant to catch.
  File.write(File.join(input, 'event2'), '')
  File.write(File.join(input, 'event2.identity'),
             "3:05ac:0256:0100::usb-0000:00:14.0-4/input0:Fabricated Keyboard\n")
  document = File.join(root, 'settings.json')
  env = { 'BLIT_INPUT_ROOT' => input, 'BLIT_SETTINGS_PATH' => document }

  start(env)

  verdict('settings-document-starts-empty') do
    code, settings = request('GET', '/v1/settings')
    check(code == 200, "GET /v1/settings -> #{code}")
    # A server with no settings resource at all must not stop the gate here:
    # the scenario that measures what this ticket is about -- a setting lost
    # across a restart -- is further down, and it is the stronger evidence.
    settings = {} unless settings.is_a?(Hash)
    check(settings['persisted'] == true, 'the document is being kept')
    check(settings['path'] == document,
          "the path is reported, got #{settings['path'].inspect}")
    check(settings.dig('document', 'input_devices') == {},
          "nothing is stored yet, got #{settings['document'].inspect}")
    check(!File.exist?(document),
          'a server that has been configured nothing writes nothing')
    check(devices.key?(STABLE) && devices.key?(SECOND) &&
          devices.key?(UNSTABLE),
          "two stable ids and one unstable, got #{devices.keys.inspect}")
  end

  verdict('a-write-is-stored-against-the-stable-id-only') do
    [STABLE, UNSTABLE].each do |id|
      code, = request('PATCH', "/v1/input/devices/#{id}",
                      { 'configuration' => { 'sensitivity_percent' => 250,
                                             'natural_scroll' => true } },
                      { 'If-Match' => '1' })
      check(code == 200, "PATCH #{id} -> #{code}")
    end
    begin
      Timeout.timeout(10) { sleep 0.1 until File.exist?(document) }
    rescue Timeout::Error
      check(false, 'an accepted write reached the document')
    end
    stored = File.exist?(document) ? JSON.parse(File.read(document)) : {}
    check(stored['version'] == 1,
          "the document carries its version, got #{stored.inspect}")
    check((stored['input_devices'] || {}).keys == [STABLE],
          'only the stable id is stored; an input-node id is not, because it ' \
          "does not survive a restart either -- got #{(stored['input_devices'] || {}).keys.inspect}")
    check((stored['input_devices'] || {})[STABLE] ==
          { 'sensitivity_percent' => 250, 'natural_scroll' => true },
          "only what differs from the default is written, got " \
          "#{(stored['input_devices'] || {})[STABLE].inspect}")
  end

  # The whole point. A restart is a real operational event -- a release is one
  # -- and before this it took every setting with it.
  verdict('a-setting-survives-a-restart') do
    stop
    start(env)
    check(sensitivity(STABLE) == 250,
          "the stable device kept its setting, got #{sensitivity(STABLE).inspect}")
    check(devices[STABLE].to_h.dig('configuration', 'natural_scroll') == true,
          'and kept natural_scroll')
    check(sensitivity(UNSTABLE) == 100,
          'the unstable id came back at the default, which is the honest ' \
          "answer rather than a wrong device configured, got #{sensitivity(UNSTABLE).inspect}")
  end

  # A write accepted inside the coalescing window has to reach the disk, and a
  # restart is exactly when it would not. This is the teardown flush.
  verdict('a-write-immediately-before-a-restart-is-not-lost') do
    revision = devices[STABLE].fetch('revision')
    code, = request('PATCH', "/v1/input/devices/#{STABLE}",
                    { 'configuration' => { 'sensitivity_percent' => 180 } },
                    { 'If-Match' => revision.to_s })
    check(code == 200, "PATCH -> #{code}")
    stop
    start(env)
    check(sensitivity(STABLE) == 180,
          "the write survived with no pause to let it settle, got " \
          "#{sensitivity(STABLE).inspect}")
  end

  verdict('the-document-is-a-resource') do
    code, settings = request('GET', '/v1/settings')
    settings = {} unless settings.is_a?(Hash)
    check(code == 200 && settings.dig('document', 'input_devices')
            .to_h.key?(STABLE), 'GET returns the whole document')
    revision = settings['revision'].to_i
    stale, = request('PUT', '/v1/settings',
                     { 'document' => { 'version' => 1, 'input_devices' => {} } },
                     { 'If-Match' => (revision + 7).to_s })
    check(stale == 412 || stale == 409,
          "a stale If-Match is refused, got #{stale}")
    code, written = request('PUT', '/v1/settings',
                            { 'document' =>
                              { 'version' => 1,
                                'input_devices' =>
                                  { STABLE => { 'sensitivity_percent' => 125 } },
                                'audio_devices' => {} } })
    check(code == 200, "PUT -> #{code}")
    check(written.is_a?(Hash) && written['applied'] == 1 &&
          written['refused'].to_i.zero?,
          "the write reports what it applied, got #{written.inspect}")
    check(sensitivity(STABLE) == 125,
          "a replaced document is applied at once, got #{sensitivity(STABLE).inspect}")
    code, reset = request('DELETE', '/v1/settings')
    check(code == 200, "DELETE -> #{code}")
    check(reset.is_a?(Hash) &&
          reset.dig('document', 'input_devices') == {},
          'DELETE empties the document')
    check(sensitivity(STABLE) == 100,
          "DELETE returns the device to the default, got #{sensitivity(STABLE).inspect}")
    check(!File.exist?(document), 'DELETE removes the file')
  end

  # ADR 0067's second failure shape. An unparseable document must never stop a
  # boot: it is renamed aside, the defaults apply, and the warning is where a
  # monitor already looks.
  verdict('an-unreadable-document-never-stops-a-boot') do
    stop
    File.write(document, 'this is not a settings document')
    start(env)
    _, diagnostics = request('GET', '/v1/diagnostics')
    check(diagnostics.fetch('warnings').include?('settings_document_unreadable'),
          "the warning is reported, got #{diagnostics['warnings'].inspect}")
    check(File.exist?("#{document}.broken"),
          'the unreadable document is kept aside rather than deleted')
    check(!File.exist?(document), 'and is out of the way')
    check(sensitivity(STABLE) == 100, 'the defaults applied')
  end

  # ADR 0067's first failure shape, and the one that matters most: a setting
  # that is perfectly valid and locks the machine out. Both recoveries.
  verdict('a-locked-out-device-is-recoverable') do
    stop
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { STABLE => { 'enabled' => false } },
      'audio_devices' => {}))
    start(env)
    check(devices[STABLE].to_h.dig('configuration', 'enabled') == false,
          'the stored setting is applied, which is what makes this a trap')
    code, = request('DELETE', '/v1/settings')
    check(code == 200, "DELETE -> #{code}")
    check(devices[STABLE].to_h.dig('configuration', 'enabled') == true,
          'the API recovers it')

    # And without the API, which is the case an API-only deployment has to
    # survive: the operator cannot ask the server to undo a setting that has
    # taken away the thing they were using.
    stop
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { STABLE => { 'enabled' => false } },
      'audio_devices' => {}))
    start(env.merge('BLIT_SETTINGS' => 'ignore'))
    check(devices[STABLE].to_h.dig('configuration', 'enabled') == true,
          'BLIT_SETTINGS=ignore starts from the defaults')
    _, settings = request('GET', '/v1/settings')
    check(settings.is_a?(Hash) && settings['persisted'] == false,
          'and says it is not keeping settings')
    check(File.exist?(document) &&
          JSON.parse(File.read(document)).fetch('input_devices').key?(STABLE),
          'and leaves the document alone -- an operator recovering a machine ' \
          'should not also lose what was in it')
    refused, problem = request('PUT', '/v1/settings',
                               { 'document' => { 'version' => 1 } })
    check(refused == 409 && problem.is_a?(Hash) &&
          problem['code'] == 'settings_not_persisted',
          "a write that could not stick is refused rather than silently " \
          "applied, got #{refused} #{problem.inspect}")
    code, = request('DELETE', '/v1/settings')
    check(code == 200,
          "a DELETE still clears the document, so the recovery can be " \
          "finished without a shell, got #{code}")
    check(!File.exist?(document), 'the document is gone')
  end

  verdict('a-stored-property-the-device-contradicts-stays-dormant') do
    stop
    # A fabricated node reports no kinds, and ADR 0064's rule is that a
    # kind-less device accepts any property -- so the contradiction is built
    # with a value the validator refuses instead. The second entry is valid,
    # which is what makes this scenario say something: a server that applies
    # nothing would fail it rather than pass it by accident.
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { STABLE => { 'sensitivity_percent' => 9999 },
                           SECOND => { 'sensitivity_percent' => 140 } },
      'audio_devices' => {}))
    start(env)
    check(sensitivity(STABLE) == 100,
          "a stored value the validator refuses is not applied, got " \
          "#{sensitivity(STABLE).inspect}")
    check(sensitivity(SECOND) == 140,
          "and one refusal does not stop the rest of the document, got " \
          "#{sensitivity(SECOND).inspect}")
    check(File.exist?(document) &&
          JSON.parse(File.read(document)).fetch('input_devices').key?(STABLE),
          'and is kept rather than deleted, because a refusal is far likelier ' \
          'to be a re-probe than a controller mistake')
  end
end

unless $failures.empty?
  warn "settings gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'SETTINGS_PERSISTENCE_PASS'
