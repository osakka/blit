#!/usr/bin/env ruby
# frozen_string_literal: true

# Every session child is told which Blit it runs under (ADR 0196):
#   * BLIT_VERSION equals GET /v1/version's server_version, so a child such
#     as Hibr's desktop can key Blit-only glyphs on it with no token
#   * the binding coordinates are still there beside it

require 'json'
require 'rbconfig'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19551'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : JSON.parse(text)]
end

def check(ok, why)
  raise "CHILD_ENVIRONMENT_FAIL #{why}" unless ok
end

Dir.mktmpdir('blit-child-environment') do |root|
  out = File.join(root, 'env.json')
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    version = request('GET', '/v1/version')[1]['server_version']
    names = %w[BLIT_VERSION BLIT_SESSION_ID BLIT_SURFACE_ID BLIT_SERVER_PORT TERM]
    code, = request('POST', '/v1/terminal-sessions',
                    { command: RbConfig.ruby, display_id: 'display-1',
                      arguments: ['-rjson', '-e', "File.write(#{out.inspect}, JSON.generate(ENV.to_h.slice(*#{names.inspect}))); sleep 30"] })
    check(code == 201, "session -> #{code}")
    Timeout.timeout(10) { sleep 0.05 until File.size?(out) }
    seen = JSON.parse(File.read(out))
    check(seen['BLIT_VERSION'] == version, "BLIT_VERSION #{seen['BLIT_VERSION'].inspect}, server #{version}")
    check(seen['BLIT_SERVER_PORT'] == PORT.to_s && seen['BLIT_SURFACE_ID'].to_s.start_with?('surface-') &&
          seen['BLIT_SESSION_ID'].to_s =~ /\A\d+\z/ && seen['TERM'] == 'xterm-kitty', "binding #{seen}")
    puts "CHILD_ENVIRONMENT_PASS blit_version=#{version} binding=session+surface+port term=xterm-kitty"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
