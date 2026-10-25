#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'net/http'
require 'socket'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

port = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19206'))))
base = URI("http://127.0.0.1:#{port}")
log_path = File.join(Dir.tmpdir, "blit-stream-replay-#{Process.pid}.log")
env = {
  **gate_display_env,
  'BLIT_COMMAND_ALLOWLIST' => '/bin/sh'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log_path, err: [:child, :out])

def http_request(base, method, path, body = nil)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    request = method.new(path)
    request['Content-Type'] = 'application/json' if body
    request.body = JSON.generate(body) if body
    response = http.request(request)
    unless response.is_a?(Net::HTTPSuccess)
      raise "HTTP #{response.code} for #{path}: #{response.body}"
    end
    response.body
  end
end

def read_exact(socket, length, timeout_seconds)
  data = +''
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout_seconds
  while data.bytesize < length
    remaining = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    raise Timeout::Error if remaining <= 0
    ready = IO.select([socket], nil, nil, remaining)
    raise Timeout::Error unless ready
    chunk = socket.readpartial(length - data.bytesize)
    data << chunk
  end
  data
end

def websocket_text(socket, timeout_seconds)
  header = read_exact(socket, 2, timeout_seconds).bytes
  length = header[1] & 0x7f
  length = read_exact(socket, 2, timeout_seconds).unpack1('n') if length == 126
  length = read_exact(socket, 8, timeout_seconds).unpack1('Q>') if length == 127
  read_exact(socket, length, timeout_seconds).force_encoding(Encoding::UTF_8)
end

def open_stream(port, path)
  socket = TCPSocket.new('127.0.0.1', port)
  key = ['blit-stream-replay'].pack('m0')
  socket.write("GET #{path} HTTP/1.1\r\nHost: 127.0.0.1:#{port}\r\n" \
              "Connection: Upgrade\r\nUpgrade: websocket\r\n" \
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: #{key}\r\n\r\n")
  response = +''
  until response.include?("\r\n\r\n")
    response << read_exact(socket, 1, 2)
  end
  raise "websocket handshake failed: #{response.lines.first}" unless response.start_with?('HTTP/1.1 101')
  socket
end

begin
  10.times do
    break if http_request(base, Net::HTTP::Get, '/v1/server')
    sleep 0.2
  rescue Errno::ECONNREFUSED, SocketError
    sleep 0.2
  end

  initial = open_stream(port, '/v1/stream')
  websocket_text(initial, 2)
  initial.close

  body = http_request(base, Net::HTTP::Post, '/v1/terminal-sessions', {
    command: '/bin/sh',
    arguments: ['-c', 'i=0; while [ "$i" -lt 100 ]; do printf "R"; i=$((i+1)); sleep 0.02; done'],
    surface: { width: 80, height: 24 }
  })
  session = JSON.parse(body)
  sleep 3

  replay = open_stream(port, '/v1/stream?since=1')
  frames = []
  begin
    loop { frames << websocket_text(replay, 0.25) }
  rescue Timeout::Error, EOFError
  ensure
    replay.close unless replay.closed?
  end

  gap = frames.any? { |frame| frame.include?('"code":"replay_gap"') }
  output = frames.any? { |frame| frame.include?('"type":"terminal_output"') }
  snapshot = http_request(base, Net::HTTP::Get, "/v1/surfaces/#{session.fetch('surface_id')}/cells")
  repaired = snapshot.include?('"revision"')
  unless gap && output && repaired
    warn "STREAM_REPLAY_FAIL gap=#{gap} terminal_output=#{output} snapshot=#{repaired} frames=#{frames.length}"
    exit 1
  end
  puts "STREAM_REPLAY_PASS gap=1 terminal_output=1 snapshot_repair=1 frames=#{frames.length}"
ensure
  begin
    http_request(base, Net::HTTP::Delete, "/v1/terminal-sessions/#{session.fetch('id')}") if session
  rescue StandardError
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  File.unlink(log_path) if File.exist?(log_path)
end
