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
# Now one peer holds at most 8 pending connections (a ninth closes its
# oldest), a header block has 2 s to arrive, a full table gives a new
# connection the slot of the oldest pending one, and a request is
# authorized (or answered 401) as soon as its header block is whole.
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

# Has the server closed this connection (EOF or reset) by now?
def closed?(socket)
  return false unless socket.wait_readable(0)

  socket.read_nonblock(1)
  false
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

# 2. 32 idle connections from this peer: all but 8 are closed at once, and
#    an authenticated request is still served.
held = Array.new(32) { TCPSocket.new('127.0.0.1', port) }
sleep 0.3
status, took = authenticated(port, token)
closed = held.count { |socket| closed?(socket) }
if status == '200' && took < 1.0
  puts format('PASS http-slots full-table authenticated 200 in %.3fs', took)
else
  failures << "full-table: with 32 idle connections an authenticated request got #{status.inspect} in #{took.round(3)}s"
end
if closed >= 24
  puts "PASS http-slots per-peer #{closed} of 32 idle connections closed at once"
else
  failures << "per-peer: only #{closed} of 32 idle connections from one peer were closed at once, wanted >= 24"
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

unless failures.empty?
  failures.each { |failure| warn "HTTP_SLOTS_FAIL #{failure}" }
  exit 1
end
puts 'HTTP_SLOTS_PASS header_deadline=2s per_peer=8 full_table=served unauthorized=401'
