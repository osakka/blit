#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #16. The input-device scan walks 32 paths. It used to open each one
# once per device kind -- 96 open() calls and 96 EVIOCGBIT probes for 32
# paths, whether or not anything was there -- and it runs inline on the event
# loop. The live host recorded one scan taking 3.456 seconds with
# max_loop_gap_ms 3460.284 behind it: for that time nothing was presented, no
# input dispatched and no client serviced.
#
# This asserts the open count, because that is the part that is bounded by
# construction rather than by luck. It deliberately does NOT claim that a
# single wedged device can no longer stall the loop: there is no asynchronous
# open() for a character device, so bounding that needs the open moved off the
# loop thread entirely. What is fixed is that a wedged device is touched once
# per scan instead of three times, and that every absent path costs one failed
# open instead of three.
#
# The scan at boot is the same code a hotplug runs, which is what makes this
# assertable without a hotplug -- forcing a rescan needs either write access to
# /dev/input or a device to lose, and a test host has neither.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19244'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
# The scan walks /dev/input/event0..31, so one probe open each is the floor.
SCANNED_PATHS = 32
# A scan has to fit inside a frame; it is on the loop thread between select()
# and rendering. 16ms is the 60Hz budget the renderer schedules against.
FRAME_BUDGET_MS = 16.0

abort("evdev scan gate: missing #{BINARY}") unless File.executable?(BINARY)

def get(path)
  uri = BASE + path
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "#{path} -> #{response.code}" unless response.code == '200'

  JSON.parse(response.body)
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

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

performance = get('/v1/diagnostics/runtime').fetch('performance')
resources = get('/v1/diagnostics/runtime').fetch('snapshot').fetch('resources')
opens = performance.fetch('evdev_scan_opens')
scan_ms = performance.fetch('max_evdev_scan_ms')
loop_gap_ms = performance.fetch('max_loop_gap_ms')
# A device that is two kinds at once needs a second open, because each kind
# drains its own fd and a shared open file description would let whichever
# read first consume the other's events. So the bound is one probe per path
# plus at most one extra per registered kind.
kinds = resources.fetch('keyboards') + resources.fetch('pointers') +
        resources.fetch('gamepads')
bound = SCANNED_PATHS + kinds

failures = []
if opens > bound
  failures << "scan made #{opens} open() calls for #{SCANNED_PATHS} paths and " \
              "#{kinds} registered kinds; one probe per path bounds it at #{bound} " \
              '(three-per-path would be 96)'
end
failures << "max_evdev_scan_ms #{scan_ms} exceeds the #{FRAME_BUDGET_MS}ms " \
            'frame budget' if scan_ms > FRAME_BUDGET_MS
failures << "max_loop_gap_ms #{loop_gap_ms} is implausibly large for an idle " \
            'fixture' if loop_gap_ms > 1000.0

unless failures.empty?
  failures.each { |f| warn "evdev scan gate: #{f}" }
  exit 1
end

puts "EVDEV_SCAN_PASS opens=#{opens} bound=#{bound} kinds=#{kinds} " \
     "max_scan_ms=#{scan_ms} max_loop_gap_ms=#{loop_gap_ms}"
