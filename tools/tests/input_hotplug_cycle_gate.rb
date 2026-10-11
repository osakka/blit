#!/usr/bin/env ruby
# frozen_string_literal: true

# Input robustness across hotplug, which is not a hypothetical: on 2026-10-11
# the live host's USB hub connected and disconnected 48 times in one dmesg
# buffer, taking the keyboard and mouse with it each time. Blit reported the
# devices correctly -- they genuinely were not there -- but "does the table
# come back clean after the fiftieth cycle" had never been asked.
#
# The table has two policies for an absent device and this exercises both,
# because they fail differently:
#
#   * a **configured** device stays in the table with `present: false`, so its
#     configuration is still there when it comes back. The thing to assert is
#     that it really does come back `present: true` with that configuration
#     intact -- a pointer set to 215% that silently reverts the next time a hub
#     blinks is worse than one that never took.
#   * an **unconfigured** device is dropped, and has to come back under *the
#     same id*. ADR 0064 derives the id from identity precisely so that a
#     replug is the same device; a cycle that minted a new one would scatter a
#     device across dozens of dead entries.
#
# And for both: the table does not grow (a duplicate per cycle is a slow leak
# that ends at BLIT_MAX_INPUT_DEVICES and then refuses new hardware), the
# process does not leak descriptors, and the inventory stays fast -- issue #16
# measured a 3.46 second /v1/input/devices on this host once.
#
# A fabricated node is a regular file, so it is never *opened* as a keyboard --
# `state.open` stays false. That bounds what this can prove: it covers the
# device table, the identity derivation and the settings replay, and it does
# not cover the poll set. The descriptor count is still watched because that is
# where a leak would show.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19613'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CYCLES = Integer(ENV.fetch('BLIT_GATE_CYCLES', '30'), 10)
KEYBOARD = 'input-0003-046d-c52b-usb-0000-00-14.0-3-input0'
POINTER = 'input-0003-05ac-0256-usb-0000-00-14.0-4-input0'

abort("hotplug gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch,
            'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "hotplug-#{rand(1 << 30)}" if %w[POST PATCH].include?(method)
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("hotplug gate: #{method} #{path} -> #{response.code} #{response.body.to_s[0, 200]}")
  end
  JSON.parse(response.body) rescue {}
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "hotplug gate: FAIL #{message}"
end

KEYBOARD_IDENTITY = <<~SIDECAR
  bustype=3
  vendor=046d
  product=c52b
  version=0111
  phys=usb-0000:00:14.0-3/input0
  name=Cycle Keyboard
  kinds=keyboard
  capabilities=keys,repeat
SIDECAR

POINTER_IDENTITY = <<~SIDECAR
  bustype=3
  vendor=05ac
  product=0256
  version=0100
  phys=usb-0000:00:14.0-4/input0
  name=Cycle Pointer
  kinds=pointer
  capabilities=rel-x,rel-y,buttons
SIDECAR

def plug(root, node, identity)
  File.write(File.join(root, "#{node}.identity"), identity)
  File.write(File.join(root, node), '')
end

def unplug(root, node)
  File.unlink(File.join(root, node)) rescue nil
  File.unlink(File.join(root, "#{node}.identity")) rescue nil
end

def devices
  json('GET', '/v1/input/devices').fetch('devices').to_h { |d| [d.fetch('id'), d] }
end

def wait_for(timeout = 15)
  Timeout.timeout(timeout) do
    sleep 0.15 until yield
  end
  true
rescue Timeout::Error
  false
end

def descriptor_count(pid)
  Dir.children("/proc/#{pid}/fd").length
rescue StandardError
  -1
end

Dir.mktmpdir('blit-hotplug') do |base|
  root = File.join(base, 'input')
  Dir.mkdir(root)
  settings = File.join(base, 'settings.json')
  plug(root, 'event0', KEYBOARD_IDENTITY)
  plug(root, 'event1', POINTER_IDENTITY)

  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms'),
                   'BLIT_INPUT_ROOT' => root,
                   'BLIT_SETTINGS_PATH' => settings },
                 BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  at_exit do
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').code == '200'
      rescue StandardError
        nil
      end
      sleep 0.2
    end
  end
  check(wait_for { devices.length >= 2 }, 'the fabricated devices never appeared')

  # The pointer is configured -- a value an operator would notice losing. The
  # keyboard is deliberately left alone, so the two take different paths
  # through retire_absent_input_devices.
  json('PATCH', "/v1/input/devices/#{POINTER}",
       'configuration' => { 'sensitivity_percent' => 215 })
  sleep 1.0
  baseline_count = devices.length
  baseline_fds = descriptor_count(server)
  check(baseline_fds.positive?, "could not read the descriptor count: #{baseline_fds}")

  lost_id = 0
  lost_setting = 0
  grew = 0
  slowest = 0.0
  completed = 0
  CYCLES.times do |cycle|
    unplug(root, 'event0')
    unplug(root, 'event1')
    # The unconfigured keyboard leaves the table; the configured pointer stays
    # in it and goes `present: false`. Both have to happen, and they are waited
    # for together: the two nodes are unlinked one after the other, so a sweep
    # can legitimately land between them and see one gone and one still there.
    unless wait_for { !devices.key?(KEYBOARD) &&
                      devices[POINTER]&.dig('state', 'present') == false }
      table = devices
      $failures << "cycle #{cycle}: unplugging left keyboard=" \
                   "#{table.key?(KEYBOARD)} pointer_present=" \
                   "#{table[POINTER]&.dig('state', 'present').inspect}"
      break
    end

    plug(root, 'event0', KEYBOARD_IDENTITY)
    plug(root, 'event1', POINTER_IDENTITY)
    unless wait_for { devices.key?(KEYBOARD) &&
                      devices[POINTER]&.dig('state', 'present') == true }
      $failures << "cycle #{cycle}: the devices never came back present"
      break
    end
    completed += 1

    started = Time.now
    table = devices
    slowest = [slowest, Time.now - started].max
    lost_id += 1 unless table.key?(KEYBOARD) && table.key?(POINTER)
    grew += 1 if table.length > baseline_count
    next unless table.key?(POINTER)

    next if table[POINTER].dig('configuration', 'sensitivity_percent') == 215

    lost_setting += 1
    if lost_setting == 1
      warn "hotplug gate: first loss at cycle #{cycle}: " \
           "pointer=#{table[POINTER]['configuration'].inspect}"
    end
  end

  final_fds = descriptor_count(server)
  final = devices
  puts "cycles=#{CYCLES} devices=#{final.length} fds #{baseline_fds} -> #{final_fds} " \
       "slowest_inventory=#{format('%.3f', slowest)}s"
  check(completed == CYCLES,
        "only #{completed} of #{CYCLES} cycles completed")
  check(lost_id.zero?,
        "#{lost_id} of #{CYCLES} cycles did not bring both devices back under " \
        'their own ids')
  check(lost_setting.zero?,
        "#{lost_setting} of #{CYCLES} cycles came back without the stored " \
        'configuration, so a hub blink silently reverts an operator setting')
  check(grew.zero?,
        "#{grew} of #{CYCLES} cycles left the table larger than #{baseline_count}")
  check(final.length == baseline_count,
        "the table settled at #{final.length}, not #{baseline_count}")
  check(final_fds <= baseline_fds + 4,
        "descriptors grew from #{baseline_fds} to #{final_fds} across " \
        "#{CYCLES} cycles")
  check(slowest < 1.0,
        "the inventory took #{format('%.3f', slowest)}s, which is issue #16's " \
        'shape returning')
  # The server has to still be a server afterwards.
  check(request('GET', '/v1/diagnostics').code == '200',
        'diagnostics stopped answering after the cycles')
end

if $failures.empty?
  puts "INPUT_HOTPLUG_CYCLE_PASS cycles=#{CYCLES} id=stable settings=kept " \
       'absent=reported table=bounded descriptors=bounded'
else
  $failures.each { |f| warn "hotplug gate: #{f}" }
  warn "hotplug gate: #{$failures.length} failure(s)"
  exit 1
end
