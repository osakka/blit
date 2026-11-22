#!/usr/bin/env ruby
# frozen_string_literal: true

# Security review SR-17 (Gitea #199): connections that have not sent an
# authorized header block cannot hold the API's slots. Before this change:
#
#   - 32 idle connections took every slot, and the owner's own request was
#     refused 503 http_capacity until one of them was dropped (5 s idle);
#   - a connection that sent nothing was kept 5 s;
#   - a header block without the token, with a body declared and not sent,
#     held its slot for 5 s and was then dropped unanswered.
#
# Now a header block has 2 s to arrive, a full table gives a new connection
# the slot of the oldest pending connection of the busiest peer (answered
# 503 http_capacity unless its request had arrived), and a request is
# authorized (or answered 401) as soon as its header block is whole.
#
# Nothing is closed while a slot is free. 0.3.135 also capped one peer at 8
# pending connections (a ninth closed its oldest); every local client --
# Hibr, its panes, tools, an SSH tunnel -- is 127.0.0.1, so that closed
# their own bursts unanswered: 20 concurrent authorized GETs lost 29 of 400,
# and 16 connections opened before writing lost 8 of 16 (cases 4 and 5).
#
# network_auth_gate.sh starts the server (with a token) and hands over its
# port; this gate is a client of it.

require 'socket'
require 'timeout'

port = Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19205')))
token = ENV.fetch('BLIT_GATE_TOKEN', 'blit-network-gate')
failures = []

def monotonic
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

# The status of an authenticated GET /v1/server and how long it took.
def authenticated(port, token)
  started = monotonic
  socket = TCPSocket.new('127.0.0.1', port)
  socket.write("GET /v1/server HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
               "Authorization: Bearer #{token}\r\nConnection: close\r\n\r\n")
  response = +''
  begin
    Timeout.timeout(3) { loop { response << socket.readpartial(65_536) } }
  rescue EOFError, Errno::ECONNRESET, Timeout::Error
    nil
  end
  socket.close
  [response[%r{\AHTTP/1\.1 (\d{3})}, 1], monotonic - started]
end

# Has the server closed this connection (EOF or reset) by now? An answer
# before the close (a 503 to a connection given up for a newcomer) is read
# through.
def closed?(socket)
  return false unless socket.wait_readable(0)

  loop { socket.read_nonblock(4096) }
rescue EOFError, Errno::ECONNRESET
  true
rescue IO::WaitReadable
  false
end

# 1. A connection that sends nothing is closed at the header deadline.
idle = TCPSocket.new('127.0.0.1', port)
started = monotonic
gone = begin
  Timeout.timeout(6) { idle.read }
  monotonic - started
rescue Errno::ECONNRESET
  monotonic - started
rescue Timeout::Error
  nil
end
idle.close
if gone && gone >= 1.5 && gone <= 3.5
  puts format('PASS http-slots header-deadline closed after %.2fs', gone)
else
  failures << "header-deadline: an idle connection was closed after #{gone.inspect}s, wanted 2 s (1.5..3.5)"
end

# 2. 32 idle connections from this peer fill the table: an authenticated
#    request is still served, by closing one of them (the slot it needed),
#    and the rest go at the 2 s header deadline.
held = Array.new(32) { TCPSocket.new('127.0.0.1', port) }
sleep 0.3
status, took = authenticated(port, token)
closed = held.count { |socket| closed?(socket) }
if status == '200' && took < 1.0
  puts format('PASS http-slots full-table authenticated 200 in %.3fs', took)
else
  failures << "full-table: with 32 idle connections an authenticated request got #{status.inspect} in #{took.round(3)}s"
end
if closed == 1
  puts 'PASS http-slots full-table 1 of 32 idle connections closed for the request'
else
  failures << "full-table: #{closed} of 32 idle connections were closed at once, wanted 1 (the slot the request needed)"
end
deadline = monotonic + 3.0
sleep 0.1 while monotonic < deadline && !held.all? { |socket| closed?(socket) }
if held.all? { |socket| closed?(socket) }
  puts 'PASS http-slots full-table idle connections closed by the header deadline'
else
  failures << 'full-table: an idle connection outlived the 2 s header deadline'
end
held.each(&:close)

# 3. A whole header block without the token is answered 401 at once, though
#    the body it declares never comes.
socket = TCPSocket.new('127.0.0.1', port)
started = monotonic
socket.write("POST /v1/terminal-sessions HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
             "Content-Type: application/json\r\nContent-Length: 4000\r\n\r\n")
response = +''
begin
  Timeout.timeout(4) { loop { response << socket.readpartial(65_536) } }
rescue EOFError, Errno::ECONNRESET, Timeout::Error
  nil
end
took = monotonic - started
socket.close
answer = response[%r{\AHTTP/1\.1 (\d{3})}, 1]
if answer == '401' && took < 1.0
  puts format('PASS http-slots unauthorized-headers 401 in %.3fs', took)
else
  failures << "unauthorized-headers: got #{answer.inspect} after #{took.round(3)}s, wanted 401 at once"
end

# An authorized GET on an open connection; its status, or nil if the
# connection was closed unanswered.
def get_on(socket, token)
  socket.write("GET /v1/version HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
               "Authorization: Bearer #{token}\r\nConnection: close\r\n\r\n")
  response = +''
  Timeout.timeout(3) { loop { response << socket.readpartial(65_536) } }
rescue EOFError, Errno::ECONNRESET, Errno::EPIPE, Timeout::Error
  response[%r{\AHTTP/1\.1 (\d{3})}, 1]
ensure
  socket.close
end

# 4. A local burst: 16 connections from 127.0.0.1 are accepted before any
#    has written (an SSH tunnel's channel round trip, a pane's pre-opened
#    socket), then each sends its request. The table has room, so none is
#    closed.
burst = Array.new(16) { TCPSocket.new('127.0.0.1', port) }
sleep 0.3
answers = burst.map { |socket| get_on(socket, token) }
if answers.all?('200')
  puts 'PASS http-slots local-burst 16 of 16 pending connections from one peer served'
else
  failures << "local-burst: 16 connections from 127.0.0.1 opened before writing got #{answers.tally.inspect}, wanted 16 x 200"
end

# 5. Another peer (127.0.0.2, also loopback) holds 20 idle connections and
#    127.0.0.1 has 12 pending, so the table is full; one more request from
#    127.0.0.1 takes the busier peer's slot, and all 13 local requests are
#    served.
idle = Array.new(20) { TCPSocket.new('127.0.0.1', port, '127.0.0.2') }
local = Array.new(12) { TCPSocket.new('127.0.0.1', port) }
sleep 0.3
status, took = authenticated(port, token)
answers = local.map { |socket| get_on(socket, token) }
idle_closed = idle.count { |socket| closed?(socket) }
if status == '200' && answers.all?('200') && idle_closed == 1
  puts format('PASS http-slots busiest-peer 13 local requests served in a full table ' \
              '(%.3fs), 1 of 20 idle connections of the busier peer closed', took)
else
  failures << "busiest-peer: newcomer #{status.inspect}, pending local #{answers.tally.inspect}, " \
              "#{idle_closed} of 127.0.0.2's 20 idle closed; wanted 200, 12 x 200, 1"
end
idle.each(&:close)

unless failures.empty?
  failures.each { |failure| warn "HTTP_SLOTS_FAIL #{failure}" }
  exit 1
end
puts 'HTTP_SLOTS_PASS header_deadline=2s full_table=served unauthorized=401 local_burst=served busiest_peer=evicted'
