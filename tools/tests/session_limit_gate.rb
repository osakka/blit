#!/usr/bin/env ruby
# frozen_string_literal: true

# Gitea #157: a full session table is refused before anything is started,
# as `503 session_limit`, and says how many of the table have ended. A
# session that has ended keeps its slot (its output stays readable) until
# it is deleted; deleting one lets the next start.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

PORT = gate_port(Integer(ENV.fetch('BLIT_SESSION_LIMIT_PORT', '19533')))
BASE = URI("http://127.0.0.1:#{PORT}")

def req(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  request = klass.new(path)
  request['Content-Type'] = 'application/json'
  request.body = JSON.generate(body) if body
  Net::HTTP.start(BASE.host, BASE.port) { |http| http.request(request) }
end

failures = []
check = lambda { |ok, what| failures << what unless ok }
detail = nil

Dir.mktmpdir('blit-session-limit') do |root|
  pid = gate_spawn({ **gate_display_env, 'BLIT_LOGIN_AUTOSTART' => '0',
                     'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'),
                     'BLIT_PSF_FONT' => File.join(GATE_ROOT, 'assets/fonts/Blit-Terminus16.psf') },
                   ENV.fetch('BLIT_GATE_BINARY', File.join(GATE_ROOT, 'build/blit/blit-server')),
                   PORT.to_s, out: File.join(root, 'server.log'), err: %i[child out])
  begin
    Timeout.timeout(10) do
      loop do
        begin
          break if req('GET', '/v1/version').code == '200'
        rescue Errno::ECONNREFUSED
          nil
        end
        sleep 0.02
      end
    end
    existing = JSON.parse(req('GET', '/v1/terminal-sessions').body).fetch('sessions', []).length
    marker = File.join(root, 'ran')
    made = []
    (16 - existing).times do
      response = req('POST', '/v1/terminal-sessions',
                     { command: '/bin/true', surface: { width: 20, height: 5 } })
      check.call(response.code == '201', "an ordinary session was refused: #{response.code} #{response.body}")
      made << JSON.parse(response.body).fetch('id') if response.code == '201'
    end
    Timeout.timeout(10) do
      loop do
        states = JSON.parse(req('GET', '/v1/terminal-sessions').body).fetch('sessions').map { |s| s['state'] }
        break if states.none?('running')
        sleep 0.05
      end
    end
    full = req('POST', '/v1/terminal-sessions',
               { command: '/bin/sh', arguments: ['-c', "touch #{marker}"], surface: { width: 20, height: 5 } })
    problem = JSON.parse(full.body) rescue {}
    detail = problem['detail']
    check.call(full.code == '503' && problem['code'] == 'session_limit',
               "a full table answered #{full.code} #{full.body}")
    check.call(detail.to_s.include?('All 16 sessions are in use') && detail.to_s.include?('of them ended'),
               "the problem does not say the table is full and how many ended: #{detail}")
    sleep 0.5
    check.call(!File.exist?(marker), 'the refused command was started anyway')
    check.call(req('DELETE', "/v1/terminal-sessions/#{made.first}").code.start_with?('2'), 'an ended session was not deleted')
    again = req('POST', '/v1/terminal-sessions',
                { command: '/bin/sh', arguments: ['-c', "touch #{marker}"], surface: { width: 20, height: 5 } })
    check.call(again.code == '201', "after a delete a session could not start: #{again.code} #{again.body}")
    Timeout.timeout(5) { sleep 0.05 until File.exist?(marker) } rescue check.call(false, 'the session after the delete never ran')
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

abort("SESSION_LIMIT_FAIL #{failures.join('; ')}") unless failures.empty?
puts "SESSION_LIMIT_PASS backend=#{GATE_BACKEND} full=503_session_limit refused=not_started detail=\"#{detail}\" delete=frees_a_slot"
