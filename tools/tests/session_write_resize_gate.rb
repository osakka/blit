#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

port = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', ARGV.fetch(0, '19207'))))
base = URI("http://127.0.0.1:#{port}")
log_path = File.join(Dir.tmpdir, "blit-session-write-resize-#{Process.pid}.log")
env = {
  **gate_display_env,
  'BLIT_COMMAND_ALLOWLIST' => '/bin/sh'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log_path, err: [:child, :out])

def request(base, method, path, body = nil, headers = {})
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    req = method.new(path)
    headers.each { |key, value| req[key] = value }
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    response = http.request(req)
    unless response.is_a?(Net::HTTPSuccess)
      raise "HTTP #{response.code} for #{path}: #{response.body}"
    end
    body_text = response.body.to_s
    body_text.empty? ? {} : JSON.parse(body_text)
  end
end

begin
  Timeout.timeout(5) do
    loop do
      break if request(base, Net::HTTP::Get, '/v1/server')
      sleep 0.1
    rescue Errno::ECONNREFUSED, SocketError
      sleep 0.1
    end
  end

  created = request(base, Net::HTTP::Post, '/v1/terminal-sessions', {
    command: '/bin/sh',
    arguments: ['-c', 'i=0; while :; do printf "W%04d\\n" "$i"; i=$((i+1)); sleep 0.01; done'],
    surface: { width: 80, height: 24 }
  })
  id = created.fetch('id')
  path = "/v1/terminal-sessions/#{id}"

  state = Timeout.timeout(5) do
    loop do
      value = request(base, Net::HTTP::Get, path)
      break value if %w[running].include?(value.fetch('state'))

      sleep 0.02
    end
  end

  6.times do |index|
    width = 60 - (index * 3)
    state = request(base, Net::HTTP::Patch, path,
                    { surface: { width: width, height: 20 } },
                    'If-Match' => state.fetch('revision').to_s)
  end

  request(base, Net::HTTP::Delete, path)
  runtime = Timeout.timeout(5) do
    loop do
      value = request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')
      resources = value.fetch('snapshot').fetch('resources')
      break value if resources.fetch('sessions') == 0

      sleep 0.02
    end
  end
  raise 'server stopped responding' unless runtime.fetch('snapshot').fetch('resources').fetch('sessions') == 0

  puts 'SESSION_WRITE_RESIZE_PASS writes=1 resizes=6 delete=1 sessions=0'
ensure
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  File.unlink(log_path) if File.exist?(log_path)
end
