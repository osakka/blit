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
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19348'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
STABLE = 'input-0003-046d-c52b-usb-0000-00-14.0-3-input0'
SECOND = 'input-0003-05ac-0256-usb-0000-00-14.0-4-input0'
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
  $server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                    }
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
  # A device that reports who it is, so ADR 0064 gives it an id derived from
  # that and not from its node number. The sidecar is key=value lines because
  # every USB phys contains colons.
  File.write(File.join(input, 'event0.identity'), <<~SIDECAR)
    bustype=3
    vendor=046d
    product=c52b
    version=0111
    phys=usb-0000:00:14.0-3/input0
    name=Fabricated Pointer
    kinds=pointer
    capabilities=rel-x,rel-y,buttons
  SIDECAR
  File.write(File.join(input, 'event1'), '')
  # A second device with an identity of its own, so that the dormant-entry
  # scenario can hold a refused entry and an accepted one at the same time.
  # Without it, "the refused value was not applied" would also be true of a
  # server that applies nothing at all, and the scenario would pass against
  # the very build it is meant to catch.
  File.write(File.join(input, 'event2'), '')
  File.write(File.join(input, 'event2.identity'), <<~SIDECAR)
    bustype=3
    vendor=05ac
    product=0256
    version=0100
    phys=usb-0000:00:14.0-4/input0
    name=Fabricated Keyboard
    kinds=keyboard
    capabilities=keys,repeat
    repeat=250,33
  SIDECAR
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
      revision = devices.fetch(id).fetch('revision')
      code, = request('PATCH', "/v1/input/devices/#{id}",
                      { 'configuration' => { 'sensitivity_percent' => 250,
                                             'natural_scroll' => true } },
                      { 'If-Match' => revision.to_s })
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

  verdict('auxiliary-display-intent-validation-and-round-trip') do
    code, before = request('GET', '/v1/settings')
    check(code == 200, "GET settings before auxiliary policy -> #{code}")
    config_doc = before.fetch('document').merge(
      'auxiliary_displays' => [
        {'output' => 'DP-1', 'pane_ordinal' => 1},
        {'output' => 'eDP-1', 'pane_ordinal' => 2}
      ])
    code, saved = request('PUT', '/v1/settings', {'document' => config_doc},
                          {'If-Match' => before.fetch('revision').to_s})
    check(code == 200, "valid auxiliary policy PUT -> #{code}: #{saved.inspect}")
    check(saved.dig('document', 'auxiliary_displays') == config_doc['auxiliary_displays'],
          'auxiliary bindings did not round-trip through the canonical settings document')
    check(saved['storage']['state'] == 'clean', 'auxiliary policy was not durably saved')

    invalid_sets = [
      [{'output' => 'DP-1', 'pane_ordinal' => 1}, {'output' => 'DP-1', 'pane_ordinal' => 1}],
      [{'output' => 'DP-1', 'pane_ordinal' => 65}],
      [{'output' => '../DP-1', 'pane_ordinal' => 1}],
      [{'output' => 'DP-1', 'pane_ordinal' => 1, 'row' => 3}],
      Array.new(17) {|i| {'output' => "DP-#{i}", 'pane_ordinal' => 1}}
    ]
    invalid_sets.each do |bindings|
      candidate = config_doc.merge('auxiliary_displays' => bindings)
      status, problem = request('PUT', '/v1/settings', {'document' => candidate},
                                {'If-Match' => saved.fetch('revision').to_s})
      check(status == 400 && problem['code'] == 'invalid_settings_auxiliary_displays',
            "invalid auxiliary policy accepted or misreported: #{status} #{problem.inspect}")
      current = request('GET', '/v1/settings').last
      check(current['revision'] == saved['revision'] &&
            current.dig('document', 'auxiliary_displays') == config_doc['auxiliary_displays'],
            'invalid auxiliary policy changed live/durable settings')
    end
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
    # The revision is in the document, not only in memory. Every other
    # revision in Blit is per-process, but this one is the If-Match a
    # controller writes the whole document against -- a counter that restarted
    # at zero would let a stale write from before the restart match by
    # accident.
    _, settings = request('GET', '/v1/settings')
    check(settings.is_a?(Hash) && settings['revision'].to_i.positive?,
          "the document revision survives the restart, got " \
          "#{settings.is_a?(Hash) ? settings['revision'].inspect : settings.inspect}")
    check(settings.dig('document', 'auxiliary_displays') == [
            {'output' => 'DP-1', 'pane_ordinal' => 1},
            {'output' => 'eDP-1', 'pane_ordinal' => 2}],
          'saved auxiliary intent survives restart without becoming a transient pane id')
  end

  # Issue #75. The document stores only deviations from defaults, so a known
  # property deliberately returned to its default disappears from the live
  # configuration object. That absence means "remove the old value", not
  # "copy the old value back as if it came from a newer release". The latter
  # made enabled:false immortal: PATCH enabled:true worked only until restart.
  verdict('a-known-property-reset-to-default-stays-reset') do
    stop
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { STABLE => { 'enabled' => false } },
      'audio_devices' => {}))
    start(env)
    check(devices[STABLE].to_h.dig('configuration', 'enabled') == false,
          'the stored non-default is applied before the reset')
    _, before = request('GET', '/v1/settings')
    before_revision = before.to_h.fetch('revision', 0).to_i
    device_revision = devices.fetch(STABLE).fetch('revision')
    code, = request('PATCH', "/v1/input/devices/#{STABLE}",
                    { 'configuration' => { 'enabled' => true } },
                    { 'If-Match' => device_revision.to_s })
    check(code == 200, "PATCH enabled:true -> #{code}")
    begin
      Timeout.timeout(10) do
        loop do
          _, settings = request('GET', '/v1/settings')
          break if settings.to_h.fetch('revision', 0).to_i > before_revision

          sleep 0.1
        end
      end
    rescue Timeout::Error
      check(false, 'the reset reached the coalesced settings document')
    end
    stored = File.exist?(document) ? JSON.parse(File.read(document)) : {}
    check(!stored.dig('input_devices').to_h.fetch(STABLE, {}).key?('enabled'),
          'the known value reset to its default is removed, got ' \
          "#{stored.dig('input_devices', STABLE).inspect}")
    stop
    start(env)
    check(devices[STABLE].to_h.dig('configuration', 'enabled') == true,
          'the reset survives a restart instead of resurrecting enabled:false')
  end

  # The #75 fix must not buy reset semantics by discarding what the merge was
  # built to protect. A property from a newer release is refused by this one,
  # kept dormant, and copied beside a later known write.
  verdict('an-unknown-property-survives-a-known-write') do
    stop
    future = { 'mode' => 'from-a-newer-release' }
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { STABLE => { 'future_pointer_policy' => future } },
      'audio_devices' => {}))
    start(env)
    revision = devices.fetch(STABLE).fetch('revision')
    code, = request('PATCH', "/v1/input/devices/#{STABLE}",
                    { 'configuration' => { 'sensitivity_percent' => 140 } },
                    { 'If-Match' => revision.to_s })
    check(code == 200, "PATCH beside an unknown property -> #{code}")
    begin
      Timeout.timeout(10) do
        sleep 0.1 until File.exist?(document) &&
          JSON.parse(File.read(document))
              .dig('input_devices', STABLE, 'sensitivity_percent') == 140
      end
    rescue Timeout::Error
      check(false, 'the known write reached the settings document')
    end
    stored = File.exist?(document) ? JSON.parse(File.read(document)) : {}
    check(stored.dig('input_devices', STABLE, 'future_pointer_policy') == future,
          'an unknown future property remains dormant, got ' \
          "#{stored.dig('input_devices', STABLE).inspect}")
    # Keep this downgrade-only fixture out of the scenarios that follow. This
    # release correctly refuses the object carrying an unknown property at
    # load; the assertion above is only that a rewrite does not eat it.
    code, = request('DELETE', '/v1/settings')
    check(code == 200, "DELETE after the future-property fixture -> #{code}")
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
    # The contradiction ADR 0067 names: a pointer property stored for a device
    # the kernel reports as a keyboard. It is refused by the same 422 a PATCH
    # would return, because the loader goes through the same validator.
    #
    # The other entry is valid and must be applied, which is what makes this
    # scenario say something -- with only the refusal, "the refused value was
    # not applied" would also be true of a server that applies nothing at all,
    # and the scenario would pass against the very build it exists to catch.
    File.write(document, JSON.dump(
      'version' => 1,
      'input_devices' => { SECOND => { 'sensitivity_percent' => 140 },
                           STABLE => { 'natural_scroll' => true } },
      'audio_devices' => {}))
    start(env)
    check(devices[SECOND].to_h.dig('configuration', 'sensitivity_percent').nil?,
          'a keyboard does not report a pointer property at all, got ' \
          "#{devices[SECOND].to_h['configuration'].inspect}")
    check(devices[STABLE].to_h.dig('configuration', 'natural_scroll') == true,
          "one refusal does not stop the rest of the document, got " \
          "#{devices[STABLE].to_h['configuration'].inspect}")
    stored = File.exist?(document) ? JSON.parse(File.read(document)) : {}
    check(stored.dig('input_devices', SECOND) ==
          { 'sensitivity_percent' => 140 },
          'and the refused entry is kept rather than deleted, because a ' \
          'refusal is far likelier to be a kernel re-probe than a ' \
          "controller's mistake -- got #{stored.dig('input_devices', SECOND).inspect}")
    # "Dormant until something else is written" is not dormant. A later write
    # regenerates the document from the live devices, and the refused property
    # is not in any of them -- so without a merge it would disappear here.
    revision = devices.fetch(STABLE).fetch('revision')
    code, = request('PATCH', "/v1/input/devices/#{STABLE}",
                    { 'configuration' => { 'sensitivity_percent' => 160 } },
                    { 'If-Match' => revision.to_s })
    check(code == 200, "PATCH -> #{code}")
    Timeout.timeout(10) do
      sleep 0.1 until JSON.parse(File.read(document))
                        .dig('input_devices', STABLE, 'sensitivity_percent') == 160
    end
    stored = JSON.parse(File.read(document))
    check(stored.dig('input_devices', SECOND) ==
          { 'sensitivity_percent' => 140 },
          'and survives a later write to another device, got ' \
          "#{stored.dig('input_devices', SECOND).inspect}")
    check(stored.dig('input_devices', STABLE) ==
          { 'sensitivity_percent' => 160, 'natural_scroll' => true },
          'while the live device carries both of its own properties, got ' \
          "#{stored.dig('input_devices', STABLE).inspect}")
  end

  # Issue #70. These were previously boot-only environment variables. The
  # document must be silent before the environment is allowed to supply the
  # effective value, and a stored value must win after both a restart and a
  # release-shaped replacement. Pane layout is topology at runtime, so the
  # settings resource persists it while /v1/topology remains its live owner.
  verdict('server-settings-follow-document-over-boot-environment') do
    boot = { 'BLIT_RENDER_INTERVAL_MS' => '23',
             'BLIT_PANE_LAYOUT' => '2x1',
             'BLIT_KMS_ASYNC_PAGEFLIP' => '1',
             'BLIT_EXPECT_DESKTOP' => '1' }
    stop
    File.delete(document) if File.exist?(document)
    start(env.merge(boot))
    _, settings = request('GET', '/v1/settings')
    effective = settings.to_h.fetch('effective', {})
    check(effective['render_interval_ms'] == 23,
          "silent document honours BLIT_RENDER_INTERVAL_MS, got #{effective.inspect}")
    check(effective['pane_layout'] == '2x1',
          "silent document honours BLIT_PANE_LAYOUT, got #{effective.inspect}")
    check(effective['kms_async_pageflip'] == true,
          "silent document honours BLIT_KMS_ASYNC_PAGEFLIP, got #{effective.inspect}")
    check(effective['expect_desktop'] == true,
          "silent document honours BLIT_EXPECT_DESKTOP, got #{effective.inspect}")

    _, topology = request('GET', '/v1/topology')
    code, = request('PUT', '/v1/topology',
                    { 'revision' => topology.fetch('revision'),
                      'layout' => { 'columns' => 1, 'rows' => 2 } })
    check(code == 200, "PUT /v1/topology persists pane layout -> #{code}")
    _, settings = request('GET', '/v1/settings')
    check(settings.dig('document', 'server', 'pane_layout') == '1x2',
          "topology is the live owner and persists its layout, got #{settings.dig('document', 'server').inspect}")

    _, fonts = request('GET', '/v1/fonts')
    boot_font = fonts.to_h.fetch('active', {}).slice('id', 'scale', 'size_px', 'cell_pixels')
    font_id = fonts.to_h.fetch('profiles', [])
      .find { |profile| profile['available'] }&.fetch('id')
    replacement = {
      'version' => 1,
      'input_devices' => {},
      'audio_devices' => {},
      'font' => { 'font_id' => font_id, 'scale' => 1.1 },
      'server' => { 'render_interval_ms' => 7,
                    'pane_layout' => '1x2',
                    'kms_async_pageflip' => false,
                    'expect_desktop' => false }
    }
    code, = request('PUT', '/v1/settings', { 'document' => replacement })
    check(code == 200, "PUT server settings -> #{code}")
    _, settings = request('GET', '/v1/settings')
    effective = settings.to_h.fetch('effective', {})
    check(effective['render_interval_ms'] == 7 &&
          effective['pane_layout'] == '1x2' &&
          effective['kms_async_pageflip'] == false &&
          effective['expect_desktop'] == false,
          "stored server settings are effective, got #{effective.inspect}")
    _, diagnostics = request('GET', '/v1/diagnostics/runtime')
    check(diagnostics.dig('snapshot', 'render', 'interval_ms') == 7,
          "diagnostics report the live render interval, got #{diagnostics.dig('snapshot', 'render').inspect}")
    check(settings.dig('document', 'font', 'scale') == 1.1,
          "font scale is persisted (font_id=#{font_id.inspect}), got #{settings.dig('document', 'font').inspect}")

    stop
    start(env.merge(boot))
    _, settings = request('GET', '/v1/settings')
    effective = settings.to_h.fetch('effective', {})
    check(effective['render_interval_ms'] == 7 &&
          effective['pane_layout'] == '1x2' &&
          effective['kms_async_pageflip'] == false &&
          effective['expect_desktop'] == false,
          "stored values beat the environment after restart, got #{effective.inspect}")
    check(settings.dig('document', 'font', 'scale') == 1.1,
          "font scale survives a restart (font_id=#{font_id.inspect})")

    code, fonts = request('PUT', '/v1/fonts', {'size_px'=>23})
    check(code == 200 && fonts.dig('active','size_px') == 23 &&
          fonts.dig('active','sizing') == 'pixels', 'pixel font height applies live')
    # Saving is owned by the canonical settings document, not a second font file.
    _, settings = request('GET','/v1/settings')
    replacement = settings.fetch('document')
    replacement['font'] = {'font_id'=>font_id,'size_px'=>23}
    code, settings = request('PUT','/v1/settings',{'document'=>replacement},
                            {'If-Match'=>settings.fetch('revision').to_s})
    check(code == 200 && settings.dig('storage','state') == 'clean' &&
          !settings.dig('document','font').key?('scale'), 'only pixel size policy is saved')
    stop
    start(env.merge(boot))
    _, fonts = request('GET','/v1/fonts')
    check(fonts.dig('active','size_px') == 23 && fonts.dig('active','sizing') == 'pixels' &&
          fonts.dig('active','cell_pixels') == {'width'=>12,'height'=>23},
          'exact pixel font height survives restart')
    _, settings = request('GET','/v1/settings')
    invalid_document = Marshal.load(Marshal.dump(settings.fetch('document')))
    invalid_document['font']['scale'] = 1.0
    code, = request('PUT','/v1/settings',{'document'=>invalid_document},
                    {'If-Match'=>settings.fetch('revision').to_s})
    check(code == 400,'competing persisted font size controls are rejected')
    _, retained = request('GET','/v1/settings')
    check(retained['revision'] == settings['revision'] &&
          retained['document']['font'] == settings['document']['font'],
          'refused font settings leave durable revision and policy unchanged')

    code, = request('DELETE', '/v1/settings')
    check(code == 200, "DELETE server settings -> #{code}")
    _, settings = request('GET', '/v1/settings')
    effective = settings.to_h.fetch('effective', {})
    check(effective['render_interval_ms'] == 23 &&
          effective['pane_layout'] == '2x1' &&
          effective['kms_async_pageflip'] == true &&
          effective['expect_desktop'] == true,
          "DELETE restores boot defaults, got #{effective.inspect}")
    _, diagnostics = request('GET', '/v1/diagnostics/runtime')
    check(diagnostics.dig('snapshot', 'render', 'interval_ms') == 23,
          "diagnostics report the restored render interval, got #{diagnostics.dig('snapshot', 'render').inspect}")
    check(settings.dig('document', 'font') == {},
          "DELETE removes the persisted font choice, got #{settings.dig('document', 'font').inspect}")
    # And the boot font is active again: a reset is the one settings change
    # that installs it without the document naming it.
    _, fonts = request('GET', '/v1/fonts')
    restored = fonts.to_h.fetch('active', {}).slice('id', 'scale', 'size_px', 'cell_pixels')
    check(restored == boot_font, "DELETE restores the boot font, got #{restored.inspect}, boot #{boot_font.inspect}")
  end
end

unless $failures.empty?
  warn "settings gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'SETTINGS_PERSISTENCE_PASS'
