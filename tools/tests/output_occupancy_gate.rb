#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #55. A Blit restart left the host with no desktop -- the session was
# gone, nothing relaunched it, and health was green the whole time:
# ready true, backend sdl3, errors []. The screen was blank and nothing in the
# API said so.
#
# Blit cannot know on its own whether a given output is *supposed* to be
# showing something: an idle development server is not broken, and every gate
# in this repository runs one. So the shape is two-tier. The count is always a
# warning, because a display server presenting nothing is worth saying out
# loud. It becomes an error only for a deployment that declared it expects a
# desktop, and only after it has had time to start one -- the live host takes
# about twelve seconds from POST to the first painted cell.
#
# What this gate asserts is the distinction, which is the part that could
# silently rot into either "always an error" (breaking every idle server) or
# "never an error" (the original defect).

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19252'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
# Must match BLIT_OUTPUT_IDLE_US in the server.
IDLE_SECONDS = 30

abort("output occupancy gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def with_server(port, environment)
  base = URI("http://127.0.0.1:#{port}")
  server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                   'BLIT_COMMAND_ALLOWLIST' => '/bin/sh',
                   }
                   .merge(environment),
                 BINARY, port.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        begin
          break if Net::HTTP.start(base.hostname, base.port) { |h|
            h.request(Net::HTTP::Get.new(base + '/v1/version'))
          }.code == '200'
        rescue StandardError
          nil
        end
        sleep 0.2
      end
    end
    yield base
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

def health(base)
  uri = base + '/v1/diagnostics'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "diagnostics -> #{response.code}" unless response.code == '200'

  JSON.parse(response.body)
end

def create_session(base)
  uri = base + '/v1/terminal-sessions'
  request = Net::HTTP::Post.new(uri)
  request['Content-Type'] = 'application/json'
  request.body = JSON.generate('command' => '/bin/sh',
                               'arguments' => ['-c', 'sleep 300'],
                               'surface_mode' => 'terminal',
                               'surface' => { 'width' => 40, 'height' => 8 })
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(request)
  end
  raise "session create -> #{response.code}: #{response.body}" unless
    response.code == '201'

  JSON.parse(response.body)
end

# An output presenting nothing is always said out loud.
with_server(PORT, {}) do |base|
  document = health(base)
  unless document.fetch('warnings').include?('output_without_session')
    failures << 'an output with no session is not reported as a warning: ' \
                "#{document.fetch('warnings').inspect}"
  end
  # And a session clears it, so the warning tracks occupancy and is not just
  # always on.
  create_session(base)
  sleep 1
  document = health(base)
  if document.fetch('warnings').include?('output_without_session')
    failures << 'the warning survived a session being created: ' \
                "#{document.fetch('warnings').inspect}"
  end
end

# Without the declaration, an idle server never becomes an error, however long
# it idles. This is the assertion that keeps every other gate's fixture
# working -- they all run a server with no session.
with_server(PORT + 1, {}) do |base|
  sleep(IDLE_SECONDS + 3)
  document = health(base)
  unless document.fetch('errors').empty?
    failures << 'an idle server with no declared expectation reported ' \
                "#{document.fetch('errors').inspect} after #{IDLE_SECONDS}s; " \
                'an idle server is not a broken one'
  end
end

# With it, the outage becomes an error -- but not before the grace window, or
# a desktop launch would trip it on every boot.
with_server(PORT + 2, { 'BLIT_EXPECT_DESKTOP' => '1' }) do |base|
  early = health(base)
  unless early.fetch('errors').empty?
    failures << 'a declared deployment reported an error before the grace ' \
                "window: #{early.fetch('errors').inspect}"
  end
  sleep(IDLE_SECONDS + 3)
  late = health(base)
  unless late.fetch('errors').include?('output_without_session')
    failures << 'a declared deployment with no session did not report an ' \
                "error after #{IDLE_SECONDS}s: #{late.fetch('errors').inspect}"
  end
  # And a session clears it, so recovery is visible too.
  create_session(base)
  sleep 1
  recovered = health(base)
  if recovered.fetch('errors').include?('output_without_session')
    failures << 'the error survived a session being created: ' \
                "#{recovered.fetch('errors').inspect}"
  end
end

unless failures.empty?
  failures.each { |failure| warn "output occupancy gate: #{failure}" }
  exit 1
end

puts "OUTPUT_OCCUPANCY_PASS warning=always error=only_when_declared " \
     "grace_s=#{IDLE_SECONDS} cleared_by_session=yes"
