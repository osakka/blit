#!/usr/bin/env ruby
# frozen_string_literal: true

# Security review SR-22 (Gitea #204): with no token, Blit listens on
# loopback only, and refuses what a web page in a browser on the same machine
# could send it. Blit serves no pages, so the only origin it calls its own is
# its listener's: http://127.0.0.1:PORT, http://localhost:PORT or
# http://[::1]:PORT. A request is refused when it
#
#   - carries any other Origin: 403 cross_origin (a cross-origin POST in
#     text/plain needs no preflight, a WebSocket none at all);
#   - names a Host other than loopback: 403 host_not_loopback (DNS
#     rebinding: a page whose own name resolves to 127.0.0.1 sends that
#     name, and its Origin matches it);
#   - has a body that is not application/json: 415 unsupported_media_type.
#
# The server before this change answered all of them, and created the
# session. Every refusal leaves the session count where it was; a program
# (no Origin, a loopback Host, JSON) is still answered, and so is a page of
# the server's own origin.
#
# network_auth_gate.sh starts the tokenless loopback server and hands over
# its port; this gate is a client of it.

require 'json'
require 'socket'
require 'timeout'

port = Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19207')))

def exchange(port, request)
  socket = TCPSocket.new('127.0.0.1', port)
  socket.write(request)
  response = +''
  begin
    # Until the server closes; an upgraded stream it keeps open, so a
    # deadline ends the wait.
    Timeout.timeout(5) { loop { response << socket.readpartial(65_536) } }
  rescue EOFError, Errno::ECONNRESET, Timeout::Error
    nil
  end
  socket.close
  status = response[%r{\AHTTP/1\.1 (\d{3})}, 1]
  body = response.split("\r\n\r\n", 2)[1].to_s
  [status && Integer(status), body]
end

def session_post(port, headers, body = nil, content_type = 'application/json')
  body ||= JSON.generate('command' => '/bin/true',
                         'surface' => { 'width' => 20, 'height' => 4 })
  lines = ["POST /v1/terminal-sessions HTTP/1.1"]
  lines << "Host: 127.0.0.1:#{port}" unless headers.key?('Host')
  headers.each { |name, value| lines << "#{name}: #{value}" }
  lines << "Content-Type: #{content_type}" if content_type
  lines << "Content-Length: #{body.bytesize}" << 'Connection: close'
  exchange(port, lines.join("\r\n") + "\r\n\r\n" + body)
end

def session_count(port)
  status, body = exchange(port, "GET /v1/terminal-sessions HTTP/1.1\r\n" \
                                "Host: 127.0.0.1:#{port}\r\nConnection: close\r\n\r\n")
  raise "GET /v1/terminal-sessions: #{status.inspect}" unless status == 200

  JSON.parse(body).fetch('count')
end

failures = []
refused = lambda do |name, status_code, code, (status, body)|
  problem = (JSON.parse(body) rescue {})
  if status == status_code && problem['code'] == code
    puts "PASS http-origin #{name} #{status}"
  else
    failures << "#{name}: expected #{status_code} #{code}, got #{status.inspect} #{body[0, 160].inspect}"
  end
end
answered = lambda do |name, (status, body)|
  if status == 201
    puts "PASS http-origin #{name} 201"
  else
    failures << "#{name}: expected 201, got #{status.inspect} #{body[0, 160].inspect}"
  end
end

before = session_count(port)
refused.call('foreign-origin', 403, 'cross_origin',
             session_post(port, 'Origin' => 'http://example.com'))
refused.call('null-origin', 403, 'cross_origin',
             session_post(port, 'Origin' => 'null'))
refused.call('other-port-origin', 403, 'cross_origin',
             session_post(port, 'Origin' => "http://127.0.0.1:#{port + 1}"))
refused.call('rebound-name', 403, 'cross_origin',
             session_post(port, 'Host' => "rebound.example:#{port}",
                                'Origin' => "http://rebound.example:#{port}"))
refused.call('foreign-host', 403, 'host_not_loopback',
             session_post(port, 'Host' => "rebound.example:#{port}"))
refused.call('text-plain-body', 415, 'unsupported_media_type',
             session_post(port, {}, nil, 'text/plain'))
refused.call('no-content-type', 415, 'unsupported_media_type',
             session_post(port, {}, nil, nil))
refused.call('websocket-foreign-origin', 403, 'cross_origin',
             exchange(port, "GET /v1/stream HTTP/1.1\r\nHost: 127.0.0.1:#{port}\r\n" \
                            "Origin: http://example.com\r\nUpgrade: websocket\r\n" \
                            "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n" \
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"))
after = session_count(port)
failures << "refused requests created #{after - before} sessions" unless after == before

answered.call('program', session_post(port, {}))
answered.call('own-origin', session_post(port, 'Origin' => "http://127.0.0.1:#{port}"))
answered.call('localhost', session_post(port, 'Host' => "localhost:#{port}",
                                              'Origin' => "http://localhost:#{port}"))
answered.call('json-charset', session_post(port, {}, nil, 'application/json; charset=utf-8'))

unless failures.empty?
  failures.each { |failure| warn "HTTP_ORIGIN_FAIL #{failure}" }
  exit 1
end
puts 'HTTP_ORIGIN_PASS'
