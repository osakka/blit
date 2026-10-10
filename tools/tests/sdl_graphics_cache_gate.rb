#!/usr/bin/env ruby
# frozen_string_literal: true

# Prove that repeated terminal damage does not re-upload an unchanged
# Kitty/Sixel image.  This is the regression gate for Hibr wallpaper cost.

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'

port = gate_port(Integer(ENV.fetch('BLIT_GRAPHICS_CACHE_PORT', '19210')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-graphics-cache-#{Process.pid}.log"
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  File.delete(log) if File.file?(log) && ENV['BLIT_GRAPHICS_CACHE_KEEP_LOG'] != '1'
end

def request(base, method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  uri = base + path
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                              read_timeout: 3) do |http|
    req = klass.new(uri)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.start_with?('2')
  response.body.nil? || response.body.empty? ? nil : JSON.parse(response.body)
end

Timeout.timeout(8) do
  loop do
    begin
      request(base, 'GET', '/v1/server')
      break
    rescue StandardError
      sleep 0.05
    end
  end
end

data = Base64.strict_encode64("\xFF\x00\x00" * 4)
kitty = "\\033_Ga=T,f=24,s=2,v=2,c=2,r=1,i=77,m=1,q=2;#{data[0, 4]}"
kitty += "\\033\\\\"
kitty += "\\033_Gq=2,i=77,m=0;#{data[4..]}\\033\\\\"
created = request(base, 'POST', '/v1/terminal-sessions',
                  'command' => '/bin/sh',
                  'arguments' => ['-c', "printf '%b' '#{kitty}'; while :; do printf X; sleep .2; done"],
                  'surface' => { 'width' => 20, 'height' => 4 })
id = created.fetch('id')
surface_path = "/v1/surfaces/surface-#{id}/cells"
Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{id}")
    break if state.fetch('graphics', []).any? { |image| image['complete'] }
    sleep 0.02
  end
end

begin
  Timeout.timeout(5) do
    loop do
      probe = request(base, 'GET', '/v1/diagnostics/runtime').fetch('performance')
      break if probe.fetch('graphics_texture_uploads') > 0
      sleep 0.02
    end
  end
rescue Timeout::Error
  warn JSON.generate(request(base, 'GET', '/v1/diagnostics/runtime'))
  warn JSON.generate(request(base, 'GET', "/v1/terminal-sessions/#{id}"))
  raise
end
before = request(base, 'GET', '/v1/diagnostics/runtime').fetch('performance')
sleep 1.0
after = request(base, 'GET', '/v1/diagnostics/runtime').fetch('performance')
request(base, 'DELETE', "/v1/terminal-sessions/#{id}")

warn "graphics metrics before=#{before.slice('graphics_cache_hits', 'graphics_texture_uploads')} after=#{after.slice('graphics_cache_hits', 'graphics_texture_uploads')}" if ENV['BLIT_GRAPHICS_CACHE_TRACE'] == '1'

uploads = after.fetch('graphics_texture_uploads') - before.fetch('graphics_texture_uploads')
hits = after.fetch('graphics_cache_hits') - before.fetch('graphics_cache_hits')
raise "unchanged image was uploaded #{uploads} additional times" unless uploads == 0
raise "unchanged graphics texture was never reused (before=#{before.fetch('graphics_cache_hits')} after=#{after.fetch('graphics_cache_hits')})" unless hits > 0
puts "SDL_GRAPHICS_CACHE_PASS uploads=#{uploads} hits=#{hits}"
