#!/usr/bin/env ruby
# frozen_string_literal: true

require 'net/http'
require 'socket'

port = Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19202')))
slow = TCPSocket.new('127.0.0.1', port)
slow.write("GET /v1/diagnostics HTTP/1.1\r\n")

thread = Thread.new do
  sleep 1.0
  slow.write("Host: 127.0.0.1\r\nAuthorization: Bearer " \
             "#{ENV.fetch('BLIT_GATE_TOKEN', 'blit-network-gate')}\r\n" \
             "Connection: close\r\n\r\n")
  slow.read
ensure
  slow.close unless slow.closed?
end

started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
response = Net::HTTP.start('127.0.0.1', port, read_timeout: 2) do |http|
  request = Net::HTTP::Get.new('/v1/diagnostics')
  request['Authorization'] = "Bearer #{ENV.fetch('BLIT_GATE_TOKEN', 'blit-network-gate')}"
  http.request(request)
end
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
thread.join

unless response.code == '200' && elapsed < 0.8
  warn "HTTP_FRAGMENTATION_FAIL status=#{response.code} elapsed=#{elapsed.round(3)}"
  exit 1
end

puts "HTTP_FRAGMENTATION_PASS elapsed=#{elapsed.round(3)}"
