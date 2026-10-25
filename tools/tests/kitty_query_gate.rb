#!/usr/bin/env ruby
# frozen_string_literal: true

# Kitty's a=q capability probe is a header-only query. Hibr sends it before a
# wallpaper upload; rejecting the empty data section prevents the later image
# from ever reaching Blit.
require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19243'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
QUERY = "\e_Gi=31,s=1,v=1,a=q,t=d,f=24;\e\\"
EXPECTED = '1b5f47693d33313b4f4b1b5c'

def request(method, path, body = nil)
  klass = {'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post}.fetch(method)
  req = klass.new(BASE + path)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  Net::HTTP.start(BASE.hostname, BASE.port, read_timeout: 5) do |http|
    http.request(req)
  end
end

def surface_text(body)
  JSON.parse(body).fetch('cells').sort_by { |cell| [cell['y'], cell['x']] }
      .map { |cell| cell.fetch('text') }.join
end

server = Process.spawn({
  **gate_display_env,
  
}, BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(10) do
  loop do
    break if request('GET', '/v1/version').code == '200'
    sleep 0.1
  rescue StandardError
    sleep 0.1
  end
end

command = "stty raw -echo; printf '#{QUERY}'; " \
          "dd if=/dev/stdin bs=1 count=12 2>/dev/null | " \
          "od -An -tx1 -v | tr -d ' \\n'; stty sane; sleep 30"
created = JSON.parse(request('POST', '/v1/terminal-sessions',
  'command' => '/bin/sh', 'arguments' => ['-c', command],
  'surface' => {'width' => 80, 'height' => 8}).body)
session = created.fetch('id')
surface = created.fetch('surface_id')

begin
  text = nil
  Timeout.timeout(10) do
    loop do
      text = surface_text(request('GET', "/v1/surfaces/#{surface}").body)
      break if text.include?(EXPECTED)
      sleep 0.1
    end
  end
  state = JSON.parse(request('GET', "/v1/terminal-sessions/#{session}").body)
  raise "query response was not OK: #{text.inspect}" unless text.include?(EXPECTED)
  raise 'query-only probe stored an image' unless state.fetch('graphics').empty?
  puts 'KITTY_QUERY_PASS response=OK image_count=0'
ensure
  request('POST', "/v1/terminal-sessions/#{session}/input",
          'data' => "\u0003", 'mode' => 'text') rescue nil
end
