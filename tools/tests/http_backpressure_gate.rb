#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'net/http'
require 'socket'
require 'timeout'

port = Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19202')))
token = ENV.fetch('BLIT_GATE_TOKEN', 'blit-network-gate')
base = URI("http://127.0.0.1:#{port}")

def http_request(base, token, method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  request = klass.new(base + path)
  request['Authorization'] = "Bearer #{token}"
  if body
    request['Content-Type'] = 'application/json'
    request.body = JSON.generate(body)
  end
  response = Net::HTTP.start(base.hostname, base.port, read_timeout: 2) do |http|
    http.request(request)
  end
  raise "#{method} #{path}: HTTP #{response.code}" unless response.code.start_with?('2')

  response.body.to_s.empty? ? nil : JSON.parse(response.body)
end

session = http_request(base, token, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/true',
  'surface' => { 'width' => 512, 'height' => 160 }
})
slow = TCPSocket.new('127.0.0.1', port)
slow.setsockopt(Socket::SOL_SOCKET, Socket::SO_RCVBUF, 1024)
slow.write("GET /v1/surfaces/surface-#{session.fetch('id')}/cells HTTP/1.1\r\n")
slow.write("Host: 127.0.0.1\r\nAuthorization: Bearer #{token}\r\n")
slow.write("Connection: close\r\n\r\n")

started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
diagnostics = Timeout.timeout(2) do
  http_request(base, token, 'GET', '/v1/diagnostics')
end
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
slow.close
http_request(base, token, 'DELETE', "/v1/terminal-sessions/#{session.fetch('id')}")

unless diagnostics.fetch('health') && elapsed < 0.8
  warn "HTTP_BACKPRESSURE_FAIL elapsed=#{elapsed.round(3)}"
  exit 1
end

puts "HTTP_BACKPRESSURE_PASS elapsed=#{elapsed.round(3)}"
