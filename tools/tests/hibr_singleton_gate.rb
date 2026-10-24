#!/usr/bin/env ruby
# API regression gate for the one-primary Hibr desktop invariant.
require 'json'
require 'net/http'
require 'fileutils'
require_relative 'gate_server'

port = 18_997
root = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(root, 'build/blit/blit-server'))
fake_desktop = File.expand_path('../../build/blit/desktop', __dir__)
FileUtils.mkdir_p(File.dirname(fake_desktop))
FileUtils.rm_f(fake_desktop)
File.symlink('/bin/sh', fake_desktop)

env = {
  **gate_display_env,
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => File.join(root, 'build/sdl3-kms'),
  'BLIT_COMMAND_ALLOWLIST' => fake_desktop
}
pid = Process.spawn(env, BINARY, port.to_s,
                    out: '/tmp/blit-hibr-singleton-gate.log', err: [:child, :out])
sessions = []
at_exit do
  sessions.each do |session_id|
    request(URI("http://127.0.0.1:#{port}/v1/terminal-sessions/#{session_id}"),
            Net::HTTP::Delete)
  rescue StandardError
    nil
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  FileUtils.rm_f(fake_desktop)
end

base = URI("http://127.0.0.1:#{port}")
def request(uri, method = Net::HTTP::Get, payload = nil)
  req = method.new(uri)
  req['Content-Type'] = 'application/json' if payload
  req.body = JSON.generate(payload) if payload
  Net::HTTP.start(uri.hostname, uri.port) { |http| http.request(req) }
end

100.times do
  begin
    break if request(URI("#{base}/v1/version")).code.to_i == 200
  rescue StandardError
    nil
  end
  sleep 0.1
end

def create(base, command, arguments)
  response = request(URI("#{base}/v1/terminal-sessions"), Net::HTTP::Post,
                     {'command' => command, 'arguments' => arguments,
                      'surface' => {'width' => 80, 'height' => 24}})
  [response.code.to_i, JSON.parse(response.body)]
end

join_code, join_body = create(base, fake_desktop,
                              ['-c', 'sleep 30', '--session', 'blit', '--join'])
raise "orphan join was accepted: #{join_body}" unless join_code == 409 &&
  join_body['code'] == 'hibr_desktop_owner_missing'

primary_code, primary = create(base, fake_desktop,
                               ['-c', 'sleep 30', '--session', 'blit'])
raise "primary failed: #{primary}" unless primary_code == 201
sessions << primary.fetch('id').to_i
raise 'primary role missing' unless primary.dig('hibr_desktop', 'role') == 'primary'
raise 'primary session name missing' unless primary.dig('hibr_desktop', 'session') == 'blit'

inventory_response = request(URI("#{base}/v1/terminal-sessions"))
inventory = JSON.parse(inventory_response.body)
raise "session inventory failed: #{inventory}" unless
  inventory_response.code.to_i == 200
raise 'session inventory count is not authoritative' unless
  inventory.fetch('count') == inventory.fetch('sessions').length
raise 'session inventory omitted the primary' unless
  inventory.fetch('sessions').any? { |item| item.fetch('id') == primary.fetch('id') }

duplicate_code, duplicate = create(base, fake_desktop,
                                   ['-c', 'sleep 30', '--session', 'blit'])
raise "duplicate primary was accepted: #{duplicate}" unless duplicate_code == 409 &&
  duplicate['code'] == 'hibr_desktop_exists'

join_code, join = create(base, fake_desktop,
                         ['-c', 'sleep 30', '--session', 'blit', '--join'])
raise "join failed: #{join}" unless join_code == 201
sessions << join.fetch('id').to_i
raise 'join role missing' unless join.dig('hibr_desktop', 'role') == 'join'
inventory_response = request(URI("#{base}/v1/terminal-sessions"))
inventory = JSON.parse(inventory_response.body)
raise 'session inventory omitted the join' unless
  inventory.fetch('sessions').any? { |item| item.fetch('id') == join.fetch('id') }

puts 'hibr singleton gate: PASS (one primary, no orphan join, duplicate primary rejected)'
