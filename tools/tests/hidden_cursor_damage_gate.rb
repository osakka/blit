#!/usr/bin/env ruby
# frozen_string_literal: true

# A hidden VT cursor may move without changing any cell. This gate ensures
# that such control traffic does not schedule presentation frames.

require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

port = gate_port(Integer(ENV.fetch('BLIT_HIDDEN_CURSOR_PORT', '19249')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-hidden-cursor-#{Process.pid}.log"
env = {
  **gate_display_env,
  
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

def binary_request(base, path)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    response = http.request(Net::HTTP::Get.new(path))
    raise "HTTP #{response.code} for #{path}" unless response.is_a?(Net::HTTPSuccess)

    response.body
  end
end

def nonblack_pixels(bmp, sample_width = nil, sample_height = nil)
  raise 'not a BMP' unless bmp.byteslice(0, 2) == 'BM'

  offset = bmp.byteslice(10, 4).unpack1('V')
  width = bmp.byteslice(18, 4).unpack1('l<')
  height = bmp.byteslice(22, 4).unpack1('l<')
  bits = bmp.byteslice(28, 2).unpack1('v')
  raise 'unexpected BMP depth' unless bits == 24 || bits == 32

  bytes_per_pixel = bits / 8
  stride = ((width * bytes_per_pixel + 3) / 4) * 4
  sample_width = width if sample_width.nil? || sample_width > width
  sample_height = height if sample_height.nil? || sample_height > height
  count = 0
  sample_height.times do |row|
    start = offset + (height - row - 1) * stride
    sample_width.times do |column|
      pixel = bmp.byteslice(start + column * bytes_per_pixel, 3)
      count += 1 if pixel.bytes.any?(&:positive?)
    end
  end
  count
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
                    "printf '0\\033[H\\033[2J\\033[?25l'; i=0; " \
                    "while [ $i -lt 200 ]; do " \
                    "printf '\\033[1;%dH' $((i % 80 + 1)); " \
                    'i=$((i + 1)); done; sleep 2'],
                  'surface_mode' => 'terminal',
                  'surface' => {'width' => 80, 'height' => 24})
session_id = session.fetch('id')
sleep 1
blank_frame = binary_request(base, '/v1/diagnostics/screenshots/display-1')
left_edge_pixels = nonblack_pixels(blank_frame, 256, 256)
unless left_edge_pixels.zero?
  raise "hidden cursor left rendered pixels at left edge: #{left_edge_pixels}"
end
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
