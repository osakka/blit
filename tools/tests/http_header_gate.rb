#!/usr/bin/env ruby
# frozen_string_literal: true

# Security review SR-19 (Gitea #201): request headers are read from the
# header block only, by name without regard to case, and a body framed by
# anything but Content-Length is refused. Headers used to be found with
# strstr over the whole request, body included, and in one spelling only:
#
#   - a body holding "Authorization: Bearer <token>" authenticated a request
#     that sent no such header (the server before this change answered 200);
#   - `authorization:` and `content-length:` in lower case were not found
#     (401, and a body read as empty: 400);
#   - Transfer-Encoding was ignored, so a chunked body was taken for none and
#     its chunks for the next request (a proxy that reads them as chunks ends
#     the request somewhere else);
#   - two different Content-Lengths were not noticed.
#
# Raw sockets, because Net::HTTP and curl write header names their own way.
# network_auth_gate.sh starts the server (with a token, and /bin/true the one
# allowed command) and hands over its port; this gate is a client of it.

require 'json'
require 'socket'
require 'timeout'

port = Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19205')))
token = ENV.fetch('BLIT_GATE_TOKEN', 'blit-network-gate')

# Sends `parts` (header block, then body) with a pause between them, so a
# server that has the whole header block decides on its own whether to wait
# for a body. Returns [status, body], status nil for a closed connection.
def exchange(port, *parts)
  socket = TCPSocket.new('127.0.0.1', port)
  parts.each_with_index do |part, index|
    sleep 0.3 if index.positive?
    socket.write(part)
  rescue Errno::EPIPE, Errno::ECONNRESET
    break # answered and closed before the body: what a refusal may do
  end
  response = +''
  begin
    Timeout.timeout(5) { loop { response << socket.readpartial(65_536) } }
  rescue EOFError, Errno::ECONNRESET, Timeout::Error
    nil
  end
  socket.close
  status = response[%r{\AHTTP/1\.1 (\d{3})}, 1]
  [status && Integer(status), response.split("\r\n\r\n", 2)[1].to_s]
end

failures = []
check = lambda do |name, expected, (status, body)|
  if expected.include?(status)
    puts "PASS http-header #{name} #{status.inspect}"
  else
    failures << "#{name}: expected #{expected.inspect}, got #{status.inspect} #{body[0, 160].inspect}"
  end
end

session_body = JSON.generate('command' => '/bin/true',
                             'surface' => { 'width' => 20, 'height' => 4 })

# 1. The token only in the body: no Authorization header.
smuggled = "Authorization: Bearer #{token}\r\n"
check.call('authorization-in-body', [401],
           exchange(port, "GET /v1/server HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
                          "Content-Length: #{smuggled.bytesize}\r\n\r\n", smuggled))

# 2. Header names in lower case: `content-length:` with the body after a
#    pause (the server before this change read no body: 400), and
#    `authorization:` (it found no token: 401).
check.call('lower-case-content-length', [201],
           exchange(port, "POST /v1/terminal-sessions HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
                          "Authorization: Bearer #{token}\r\n" \
                          "Content-Type: application/json\r\n" \
                          "content-length: #{session_body.bytesize}\r\n\r\n", session_body))
check.call('lower-case-authorization', [200],
           exchange(port, "GET /v1/server HTTP/1.1\r\nhost: 127.0.0.1\r\n" \
                          "authorization: Bearer #{token}\r\nConnection: close\r\n\r\n"))

# 3. A chunked body: refused, before anything reads it.
chunked = "#{session_body.bytesize.to_s(16)}\r\n#{session_body}\r\n0\r\n\r\n"
check.call('transfer-encoding-refused', [501],
           exchange(port, "POST /v1/terminal-sessions HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
                          "Authorization: Bearer #{token}\r\n" \
                          "Content-Type: application/json\r\n" \
                          "Transfer-Encoding: chunked\r\n\r\n", chunked))

# 4. Two Content-Lengths that disagree: not answered as a request.
check.call('conflicting-content-length', [nil, 400],
           exchange(port, "POST /v1/terminal-sessions HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
                          "Authorization: Bearer #{token}\r\n" \
                          "Content-Type: application/json\r\n" \
                          "Content-Length: #{session_body.bytesize}\r\n" \
                          "Content-Length: 2\r\n\r\n", session_body))

# 5. Security review SR-23: a refusal sent before the request is authorized
#    does not say which Blit answered; an authorized answer does. (The server
#    before this change sent X-Blit-Version on the 401 too.)
version_header = lambda do |request|
  socket = TCPSocket.new('127.0.0.1', port)
  socket.write(request)
  response = +''
  begin
    Timeout.timeout(5) { loop { response << socket.readpartial(65_536) } }
  rescue EOFError, Errno::ECONNRESET, Timeout::Error
    nil
  end
  socket.close
  head = response.split("\r\n\r\n", 2)[0].to_s
  [head[%r{\AHTTP/1\.1 (\d{3})}, 1], head[/^X-Blit-Version: *(.*)$/i, 1]]
end
status, version = version_header.call("GET /v1/server HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
if status == '401' && version.nil?
  puts 'PASS http-header unauthenticated-no-version 401'
else
  failures << "unauthenticated-no-version: #{status.inspect} with X-Blit-Version #{version.inspect}"
end
status, version = version_header.call("GET /v1/server HTTP/1.1\r\nHost: 127.0.0.1\r\n" \
                                      "Authorization: Bearer #{token}\r\nConnection: close\r\n\r\n")
if status == '200' && version.to_s.strip =~ /\A\d+\.\d+\.\d+/
  puts "PASS http-header authenticated-version 200 #{version.strip}"
else
  failures << "authenticated-version: #{status.inspect} with X-Blit-Version #{version.inspect}"
end

unless failures.empty?
  failures.each { |failure| warn "HTTP_HEADER_FAIL #{failure}" }
  exit 1
end
puts 'HTTP_HEADER_PASS'
