#!/usr/bin/env ruby
# frozen_string_literal: true

# What a terminal application actually receives on its PTY when the pointer
# moves or a button is pressed.
#
#   #37 the legacy (non-SGR) encoding's button byte is 32 + Cb for every
#       button, the wheel included
#
# The session under test echoes its input with `cat -v`, so the bytes the
# server wrote to the PTY come back as cells and can be read through the API
# with no framebuffer capture involved (#45). `cat -v` spells ESC as ^[ and a
# byte above 127 as M-<something>, which is what the expectations below are
# written in.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

ROOT = File.expand_path('../..', __dir__)
PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '19251'), 10)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
INSET = 4

abort("mouse-report gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "mouse-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("mouse-report gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

# Everything the session has echoed, as one string.
def echoed(surface_id)
  json('GET', "/v1/surfaces/#{surface_id}/cells").fetch('cells')
      .sort_by { |c| [c.fetch('y', 0), c.fetch('x', 0)] }
      .map { |c| c.fetch('text', ' ') }.join
end

def reporting_session(modes)
  json('POST', '/v1/terminal-sessions',
       'command' => '/bin/sh',
       'arguments' => ['-c', "printf '#{modes}'; cat -v"],
       'display_id' => 'display-1',
       'surface' => { 'width' => 60, 'height' => 8 })
end

def move_to(column, row)
  request('POST', '/v1/input/inject', 'kind' => 'pointer_motion',
          'x' => INSET + (column - 1) * CELL_W + 2,
          'y' => INSET + (row - 1) * CELL_H + 2)
end

def wait_for(surface_id, pattern, seconds = 5)
  text = ''
  Timeout.timeout(seconds) do
    loop do
      text = echoed(surface_id)
      break if text.include?(pattern)

      sleep 0.2
    end
  end
  [true, text]
rescue Timeout::Error
  [false, text]
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
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

failures = []

# #37. Legacy mode: ?1000h without ?1006h. A wheel-up is Cb 64, so the byte on
# the wire is 96, which is a backtick; wheel-down is 97, an 'a'. Sending 64 and
# 65 instead spells '@' and 'A' and decodes as button 0.
legacy = reporting_session('\\033[?1000h')
sleep 2.0
move_to(1, 1)
request('POST', '/v1/input/inject', 'kind' => 'pointer_wheel', 'delta' => 1)
found, text = wait_for(legacy.fetch('surface_id'), '^[[M')
if !found
  failures << "a legacy-mode session received no wheel report at all: #{text.inspect}"
else
  button_byte = text[text.index('^[[M') + 4]
  case button_byte
  when '`' then nil
  when '@'
    failures << 'the legacy wheel report used button byte 64 (@) where xterm ' \
                "defines 96 (`): #{text.inspect}"
  else
    failures << 'the legacy wheel report carried button byte ' \
                "#{button_byte.inspect}, expected 96 (`): #{text.inspect}"
  end
end
request('DELETE', "/v1/terminal-sessions/#{legacy.fetch('id')}")

unless failures.empty?
  failures.each { |f| warn "mouse-report gate: #{f}" }
  exit 1
end

puts 'MOUSE_REPORT_PASS legacy_wheel_byte=96'
