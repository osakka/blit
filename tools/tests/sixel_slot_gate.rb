#!/usr/bin/env ruby
# frozen_string_literal: true

# Gitea #85. A sixel that has scrolled off the screen can never be shown
# again -- it has no id a client can place -- yet it kept one of the
# session's image slots (eight then, sixteen since 0.3.17) for the life of
# the session, so the next sixel (and every Kitty image after it) was
# refused with image-store-full and a program printing pictures simply
# stopped producing them. This gate prints twenty, more than the slots,
# sixels, scrolling each one off, then one more, and requires the last one
# to be on screen and nothing to have been rejected.

require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

port = gate_port(Integer(ENV.fetch('BLIT_SIXEL_SLOT_PORT', '19235')))
base = URI("http://127.0.0.1:#{port}")
binary = ENV.fetch('BLIT_GATE_BINARY', 'build/blit/blit-server')
log = "/tmp/blit-sixel-slot-#{Process.pid}.log"
env = {
  **gate_display_env,
  
}
pid = Process.spawn(env, binary, port.to_s, out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def request(base, method, path, body = nil)
  klass = { get: Net::HTTP::Get, post: Net::HTTP::Post,
            delete: Net::HTTP::Delete }.fetch(method)
  uri = base + path
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                             read_timeout: 10) do |http|
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
      request(base, :get, '/v1/version')
      break
    rescue StandardError
      sleep 0.05
    end
  end
end

SCRIPT = <<~'SH'
  sixel() { printf '\033Pq#0;2;100;0;0#0!8~\033\\'; }
  i=0
  while [ $i -lt 20 ]; do
    sixel
    printf '\n\n\n\n\n\n\n\n'
    i=$((i+1))
  done
  sixel
  printf DONE
  sleep 30
SH

id = nil
begin
  created = request(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh', 'arguments' => ['-c', SCRIPT],
                    'surface' => { 'width' => 20, 'height' => 6 })
  id = created.fetch('id')
  Timeout.timeout(15) do
    loop do
      cells = request(base, :get, "/v1/surfaces/surface-#{id}/cells")
      text = cells.fetch('cells').map { |c| c['text'].to_s }.join
      break if text.include?('DONE')
      sleep 0.05
    end
  end
  state = request(base, :get, "/v1/terminal-sessions/#{id}")
  diag = state['graphics_diagnostics'] || {}
  rejected = diag['rejected'].to_i
  raise "sixels were refused: rejected=#{rejected} " \
        "last_error=#{diag['last_error'].inspect} " \
        "graphics=#{state['graphics'].inspect}" unless rejected.zero?
  held = state.fetch('graphics', []).size
  raise "scrolled-off sixels are still held: #{held} images" unless held == 1
  puts "SIXEL_SLOT_PASS held=#{held} rejected=#{rejected}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
