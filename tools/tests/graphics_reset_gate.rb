#!/usr/bin/env ruby
# frozen_string_literal: true

# The explicit lifecycle recovery operation must remove a retained Kitty
# object without stopping the PTY or changing terminal cells.

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GRAPHICS_RESET_PORT', '19268'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("graphics-reset gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  Net::HTTP.start(BASE.hostname, BASE.port, read_timeout: 10) do |http|
    http.request(req)
  end
end

def json(path)
  response = request('GET', path)
  abort("graphics-reset gate: GET #{path}: HTTP #{response.code}") unless
    response.code == '200'
  JSON.parse(response.body)
end

server = spawn({ **gate_display_env,
                 'BLIT_PSF_FONT' => FONT,
                 'BLIT_COMMAND_ALLOWLIST' => '/bin/sh',
                 },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(10) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.05
  end
end

rgb = ([255, 0, 0] * (4 * 8)).pack('C*')
data = Base64.strict_encode64(rgb)
kitty = "\\033_Ga=T,f=24,s=4,v=8,c=2,r=2,z=-1,i=9902,m=0,q=2;#{data}\\033\\\\"
command = 'printf %b "$1"; sleep 20'
created = JSON.parse(request('POST', '/v1/terminal-sessions',
  'command' => '/bin/sh',
  'arguments' => ['-c', command, 'sh', kitty],
  'surface' => { 'width' => 40, 'height' => 8 }).body)
session = created.fetch('id')

begin
  Timeout.timeout(10) do
    loop do
      state = json("/v1/terminal-sessions/#{session}")
      break if state.fetch('graphics').any? do |image|
        image.fetch('id') == 9902 && image.fetch('visible')
      end
      sleep 0.05
    end
  end

  reset = request('DELETE', "/v1/terminal-sessions/#{session}/graphics")
  abort("graphics-reset gate: reset HTTP #{reset.code}: #{reset.body}") unless
    reset.code == '200'
  after = JSON.parse(reset.body)
  abort("graphics-reset gate: image remained: #{after['graphics'].inspect}") unless
    after.fetch('graphics').empty?
  live = json("/v1/terminal-sessions/#{session}")
  abort("graphics-reset gate: session stopped: #{live['state']}") unless
    live['state'] == 'running'
  puts 'GRAPHICS_RESET_PASS image_removed=true session_running=true'
ensure
  request('DELETE', "/v1/terminal-sessions/#{session}") rescue nil
end
