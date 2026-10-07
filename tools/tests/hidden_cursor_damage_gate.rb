#!/usr/bin/env ruby
# frozen_string_literal: true

# A hidden VT cursor may move without changing any cell. This gate ensures
# that such control traffic does not schedule presentation frames.

require 'json'
require 'net/http'
require 'timeout'

port = Integer(ENV.fetch('BLIT_HIDDEN_CURSOR_PORT', '19249'))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-hidden-cursor-#{Process.pid}.log"
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  if pid
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
  File.delete(log) if File.exist?(log)
end

def request(base, method, path, body = nil)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    req = method.new(path)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    response = http.request(req)
    raise "HTTP #{response.code} for #{path}: #{response.body}" unless
      response.is_a?(Net::HTTPSuccess)

    body = response.body.to_s
    body.empty? ? nil : JSON.parse(body)
  end
end

Timeout.timeout(8) do
  loop do
    begin
      request(base, Net::HTTP::Get, '/v1/server')
      break
    rescue StandardError
      sleep 0.1
    end
  end
end

session = request(base, Net::HTTP::Post, '/v1/terminal-sessions',
                  'command' => '/bin/sh',
                  'arguments' => ['-c',
                    "printf '\\033[?25l'; i=0; " \
                    "while [ $i -lt 200 ]; do " \
                    "printf '\\033[1;%dH' $((i % 80 + 1)); " \
                    'i=$((i + 1)); done; sleep 2'],
                  'surface_mode' => 'terminal',
                  'surface' => {'width' => 80, 'height' => 24})
session_id = session.fetch('id')
sleep 1
before = request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')
sleep 1
after = request(base, Net::HTTP::Get, '/v1/diagnostics/runtime')

before_performance = before.fetch('performance')
after_performance = after.fetch('performance')
unless after_performance.fetch('frames') == before_performance.fetch('frames') &&
       after_performance.fetch('present_calls') ==
         before_performance.fetch('present_calls')
  raise "hidden cursor scheduled frames: before=#{before_performance.inspect} " \
        "after=#{after_performance.inspect}"
end

request(base, Net::HTTP::Delete, "/v1/terminal-sessions/#{session_id}")
puts format('HIDDEN_CURSOR_DAMAGE_PASS frames=%d present_calls=%d',
            after_performance.fetch('frames'),
            after_performance.fetch('present_calls'))
