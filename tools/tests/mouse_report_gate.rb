#!/usr/bin/env ruby
# frozen_string_literal: true

# What a terminal application actually receives on its PTY when the pointer
# moves or a button is pressed.
#
#   #37 the legacy (non-SGR) encoding's button byte is 32 + Cb for every
#       button, the wheel included
#   #36 DEC 1002 reports motion only while a button is held, and both
#       tracking modes report once per cell rather than once per sample
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

def move_to(column, row, offset = 2)
  request('POST', '/v1/input/inject', 'kind' => 'pointer_motion',
          'x' => INSET + (column - 1) * CELL_W + offset,
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

# #36. SGR reports are easier to count than to spell: code, column, row.
# A code with the 32 bit set is a motion report.
def motions(surface_id)
  echoed(surface_id).scan(/\^\[\[<(\d+);(\d+);(\d+)([Mm])/)
                    .map { |code, column, row, _| [code.to_i, column.to_i, row.to_i] }
                    .select { |code, _, _| code >= 32 }
end

# 1002 is button-event tracking: a hover is not a drag.
tracking = reporting_session('\033[?1002h\033[?1006h')
sleep 2.0
[1, 5, 9].each { |column| move_to(column, 1); sleep 0.3 }
hovered = motions(tracking.fetch('surface_id'))
unless hovered.empty?
  failures << "DEC 1002 reported #{hovered.length} motion events with no " \
              "button held: #{hovered.inspect}"
end

# With a button down, one report per cell entered and no more.
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => true)
sleep 0.4
[13, 17].each { |column| move_to(column, 1); sleep 0.4 }
dragged = motions(tracking.fetch('surface_id'))
if dragged.length < 2
  failures << "DEC 1002 reported #{dragged.length} motion events while " \
              "dragging across two cells, expected 2: #{dragged.inspect}"
end
# Three more samples at different pixels inside the cell it is already in
# must add nothing: the pointer moved, the cell did not.
[3, 5, 7].each { |offset| move_to(17, 1, offset); sleep 0.2 }
resampled = motions(tracking.fetch('surface_id'))
if resampled.length > dragged.length
  failures << "#{resampled.length - dragged.length} extra motion events were " \
              'reported for samples inside a cell the pointer was already in'
end
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => false)
request('DELETE', "/v1/terminal-sessions/#{tracking.fetch('id')}")

# 1003 is any-event tracking: a bare hover is reported.
any_event = reporting_session('\033[?1003h\033[?1006h')
sleep 2.0
[1, 5, 9].each { |column| move_to(column, 2); sleep 0.3 }
anywhere = motions(any_event.fetch('surface_id'))
if anywhere.length < 2
  failures << "DEC 1003 reported #{anywhere.length} motion events for a " \
              "hover across three cells, expected at least 2: #{anywhere.inspect}"
end
request('DELETE', "/v1/terminal-sessions/#{any_event.fetch('id')}")

unless failures.empty?
  failures.each { |f| warn "mouse-report gate: #{f}" }
  exit 1
end

puts "MOUSE_REPORT_PASS legacy_wheel_byte=96 hover_1002=0 drag_1002=#{dragged.length} " \
     "hover_1003=#{anywhere.length}"
