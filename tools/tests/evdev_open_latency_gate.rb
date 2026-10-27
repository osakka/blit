#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #16. Opening an evdev node must not stall the event loop.
#
# The live host recorded one scan taking 3.456 seconds with max_loop_gap_ms
# 3460.284 behind it: for 3.46 s nothing was presented, no input dispatched and
# no client serviced. The inotify watch added in 0.1.239 changed *when* the
# scan runs, not what it costs -- the open was still a synchronous call on the
# loop thread, and there is no asynchronous open() for a character device.
#
# So the open moved to a thread. This gate measures the property that change
# was for: with device opens made slow on purpose, the loop keeps running.
#
# BLIT_INPUT_OPEN_DELAY_US makes each open take a known time. It exists
# because the stall cannot otherwise be reproduced: O_NONBLOCK makes a FIFO
# open return immediately, and creating a character device whose driver blocks
# needs root. The alternative to the hook is an untestable fix.
#
# What is asserted is max_loop_gap_ms, the server's own measurement of its
# longest iteration -- not the wall time of a request, which would also pass
# if the server were merely fast at answering while frozen elsewhere.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19251'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

# 150ms per open, eight nodes: 1.2s of opens if they happen on the loop, and
# more as kinds are claimed. A frame is 16ms at 60Hz; the bound here is
# deliberately looser than one frame, because a general-purpose loop under a
# gate suite has other reasons to miss a frame and this gate is about seconds,
# not jitter.
OPEN_DELAY_US = 150_000
NODES = 8
LOOP_GAP_BUDGET_MS = 250.0

abort("evdev open latency gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def get(path)
  uri = BASE + path
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "#{path} -> #{response.code}" unless response.code == '200'

  JSON.parse(response.body)
end

Dir.mktmpdir('blit-open') do |root|
  # One node at boot. The rest arrive *after* the server is up, because the
  # boot scan runs before the event loop and its opens therefore never appear
  # in max_loop_gap_ms at all -- which is how the first version of this gate
  # passed against the defect. The live 3.46s event was a rescan: inotify sees
  # a node appear and reconnect_evdev_devices runs inside the loop.
  File.write(File.join(root, 'event0'), '')
  server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                   'BLIT_INPUT_ROOT' => root,
                   'BLIT_INPUT_OPEN_DELAY_US' => OPEN_DELAY_US.to_s,
                   },
                 BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  begin
    # The server must answer while the opens are still in flight. With the
    # opens on the loop thread it cannot: the readiness loop itself is what
    # waits for them.
    Timeout.timeout(20) do
      loop do
        begin
          break if get('/v1/version')
        rescue StandardError
          nil
        end
        sleep 0.1
      end
    end

    # The loop's worst iteration before the hotplug, so the measurement below
    # is attributable to it.
    baseline = get('/v1/diagnostics/runtime').fetch('performance')
                 .fetch('max_loop_gap_ms')

    # Now the hotplug the ticket is about.
    (1...NODES).each { |index| File.write(File.join(root, "event#{index}"), '') }
    sleep(NODES * OPEN_DELAY_US / 1_000_000.0 + 2.0)
    performance = get('/v1/diagnostics/runtime').fetch('performance')
    gap = performance.fetch('max_loop_gap_ms')
    scan = performance.fetch('max_evdev_scan_ms')
    opens = performance.fetch('evdev_scan_opens')

    if gap > LOOP_GAP_BUDGET_MS
      failures << "max_loop_gap_ms #{gap} exceeds #{LOOP_GAP_BUDGET_MS}ms " \
                  "with #{NODES} nodes at #{OPEN_DELAY_US / 1000}ms per open: " \
                  'a device open is still costing the loop its iteration'
    end
    if scan > LOOP_GAP_BUDGET_MS
      failures << "max_evdev_scan_ms #{scan} exceeds #{LOOP_GAP_BUDGET_MS}ms; " \
                  'the scan is still doing the opening'
    end
    # The devices must actually arrive -- a loop that never opens anything
    # would pass the bound above trivially.
    collection = get('/v1/input/devices')
    if collection.fetch('count') != NODES
      failures << "#{collection.fetch('count')} of #{NODES} nodes were " \
                  'registered; the opener has to answer, not just not block'
    end
    if opens < NODES
      failures << "evdev_scan_opens #{opens} is below the #{NODES} nodes; the " \
                  'scan did not request them'
    end
    puts "max_loop_gap_ms=#{gap} (baseline #{baseline}) " \
         "max_evdev_scan_ms=#{scan} " \
         "devices=#{collection.fetch('count')} opens=#{opens}"
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

unless failures.empty?
  failures.each { |failure| warn "evdev open latency gate: #{failure}" }
  exit 1
end

puts "EVDEV_OPEN_LATENCY_PASS nodes=#{NODES} " \
     "open_delay_ms=#{OPEN_DELAY_US / 1000} budget_ms=#{LOOP_GAP_BUDGET_MS}"
