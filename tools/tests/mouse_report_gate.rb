#!/usr/bin/env ruby
# frozen_string_literal: true

# What a terminal application actually receives on its PTY when the pointer
# moves or a button is pressed.
#
#   #37 the legacy (non-SGR) encoding's button byte is 32 + Cb for every
#       button, the wheel included
#   #36 DEC 1002 reports motion only while a button is held, and both
#       tracking modes report once per cell rather than once per sample
#   #44 input the PTY would not take is counted, not lost in silence
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
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19251'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
INSET = 0

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

# `modes` arrives as a single-quoted Ruby literal so the backslash reaches the
# shell's printf. It must not be a real ESC byte: a JSON-encoded control
# character does not survive the server's argument parser (see the
# json_string_array issue).
def reporting_session(modes, prefix: '', tail: 'cat -v')
  json('POST', '/v1/terminal-sessions',
       'command' => '/bin/sh',
       'arguments' => ['-c', "#{prefix}printf '#{modes}'; #{tail}"],
       'display_id' => 'display-1',
       'surface' => { 'width' => 60, 'height' => 8 })
end

def move_to(column, row, offset = 2)
  request('POST', '/v1/input/inject', 'kind' => 'pointer_motion',
          'x' => INSET + (column - 1) * CELL_W + offset,
          'y' => INSET + (row - 1) * CELL_H + 2)
end

# A DELETE returns before the session is gone, and a session that still
# exists still wins the pointer hit test. Wait for it to actually go.
def delete_session(id)
  request('DELETE', "/v1/terminal-sessions/#{id}")
  Timeout.timeout(5) do
    loop do
      break if request('GET', "/v1/terminal-sessions/#{id}").code == '404'

      sleep 0.1
    end
  end
rescue Timeout::Error
  warn "mouse-report gate: session #{id} did not go away after DELETE"
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
delete_session(legacy.fetch('id'))

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
delete_session(tracking.fetch('id'))

# 1003 is any-event tracking: a bare hover is reported.
any_event = reporting_session('\033[?1003h\033[?1006h')
sleep 2.0
[1, 5, 9].each { |column| move_to(column, 2); sleep 0.3 }
anywhere = motions(any_event.fetch('surface_id'))
if anywhere.length < 2
  failures << "DEC 1003 reported #{anywhere.length} motion events for a " \
              "hover across three cells, expected at least 2: #{anywhere.inspect}"
end
delete_session(any_event.fetch('id'))

# blit#137. DEC 1016, SGR-Pixels: the same SGR report with the position in
# pixels of the surface, one-based, so an application drawing pixels is told
# where the pointer is rather than which cell it is in. The mode is asked for
# with DECRQM first, the way a program finds out it can have it.
pixels = reporting_session('\\033[?1016$p\\033[?1003h\\033[?1016h')
sleep 2.0
asked, text = wait_for(pixels.fetch('surface_id'), '^[[?1016;')
if !asked
  failures << "DECRQM for 1016 had no answer: #{text.inspect}"
elsif !text.include?('^[[?1016;2$y')
  failures << "DECRQM for 1016 before it was set should answer 2: #{text.inspect}"
end
def pixel_reports(surface_id)
  echoed(surface_id).scan(/\^\[\[<(\d+);(\d+);(\d+)([Mm])/)
                    .map { |code, x, y, k| [code.to_i, x.to_i, y.to_i, k] }
end
move_to(3, 2, 5)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => true)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => false)
want_x = (3 - 1) * CELL_W + 5 + 1
want_y = (2 - 1) * CELL_H + 2 + 1
found, text = wait_for(pixels.fetch('surface_id'), "^[[<0;#{want_x};#{want_y}m")
unless found
  failures << "a click at pixel #{want_x},#{want_y} under 1016 was not reported " \
              "in pixels: #{pixel_reports(pixels.fetch('surface_id')).inspect}"
end
# A sweep of forty samples with no pause between them is one per pixel, which
# under 1016 would be forty reports where a cell report sends three. Held to a
# frame, it is a handful, and the last one is where the pointer stopped.
before = pixel_reports(pixels.fetch('surface_id')).length
40.times { |i| move_to(1 + i / 8, 3, i % 8) }
sleep 0.5
swept = pixel_reports(pixels.fetch('surface_id'))[before..] || []
moved = swept.select { |code, _, _, _| code >= 32 }
last_x = (1 + 39 / 8 - 1) * CELL_W + 39 % 8 + 1
if moved.empty?
  failures << 'a sweep under 1003 and 1016 reported no motion at all'
else
  if moved.length > 20
    failures << "a sweep of 40 samples sent #{moved.length} motion reports " \
                'under 1016; it is held to one a frame'
  end
  unless moved.last[1] == last_x && moved.last[2] == CELL_H * 2 + 3
    failures << "the last motion report under 1016 was #{moved.last.inspect}, " \
                "not where the pointer stopped (#{last_x},#{CELL_H * 2 + 3})"
  end
end
pixel_sweep = moved.length
pixel_session = json('GET', "/v1/terminal-sessions/#{pixels.fetch('id')}")
unless pixel_session.dig('mouse_reporting', 'pixels') == true
  failures << "the session does not say it reports pixels: #{pixel_session['mouse_reporting'].inspect}"
end
delete_session(pixels.fetch('id'))

# #44. A terminal that has stopped reading fills its input queue, and the PTY
# master is nonblocking, so the report write is refused. That has to be
# counted rather than vanishing.
def dropped_reports
  json('GET', '/v1/diagnostics/runtime').fetch('performance')
      .fetch('input_reports_dropped', nil)
end

deaf = reporting_session('\\033[?1000h\\033[?1006h',
                         prefix: 'stty raw -echo 2>/dev/null; ',
                         tail: 'sleep 60')
sleep 2.0
before = dropped_reports
# Fill the input queue: the child puts its tty in raw mode so the queue
# applies backpressure instead of discarding, and never reads stdin.
filler = 'x' * 700
fills = 0
refused = nil
40.times do
  response = request('POST', "/v1/terminal-sessions/#{deaf.fetch('id')}/input",
                     'data' => filler)
  unless response.code.start_with?('2')
    refused = response.code
    break
  end
  fills += 1
end
# A refused 700-byte write only means there is no room for 700 bytes; a ten
# byte report would still fit. Top the queue up one byte at a time until even
# that is refused, so the report has nowhere to go.
300.times do
  response = request('POST', "/v1/terminal-sessions/#{deaf.fetch('id')}/input",
                     'data' => 'x')
  break unless response.code.start_with?('2')

  fills += 1
end
move_to(3, 1)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => true)
request('POST', '/v1/input/inject', 'kind' => 'pointer_button',
        'button' => 0, 'pressed' => false)
sleep 0.5
after = dropped_reports
if before.nil? || after.nil?
  failures << 'the runtime diagnostics do not report ' \
              'performance.input_reports_dropped at all, so a refused input ' \
              'write is still invisible'
elsif after <= before
  failures << 'a mouse report written to a session whose input queue is ' \
              "full was not counted as dropped (#{before} -> #{after}, " \
              "#{fills} fills accepted, then #{refused.inspect})"
end
delete_session(deaf.fetch('id'))

unless failures.empty?
  failures.each { |f| warn "mouse-report gate: #{f}" }
  exit 1
end

puts "MOUSE_REPORT_PASS legacy_wheel_byte=96 hover_1002=0 drag_1002=#{dragged.length} " \
     "hover_1003=#{anywhere.length} pixel_sweep=#{pixel_sweep} reports_dropped=#{before.inspect}->#{after.inspect}"
