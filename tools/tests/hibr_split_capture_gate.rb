#!/usr/bin/env ruby
# frozen_string_literal: true

# Proves that the read-only split diagnostic consumes the authoritative
# session inventory and identifies a valid bottom-primary/top-join layout.
# The fixture is terminal-only by design; Kitty/SDL pixel evidence remains a
# separate renderer gate, while this gate proves the correlation transaction.
require 'fileutils'
require 'json'
require 'net/http'
require 'open3'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19291'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, body = nil)
  klass = {'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
           'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete}.fetch(method)
  req = klass.new(BASE + path)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  Net::HTTP.start(BASE.hostname, BASE.port, open_timeout: 2, read_timeout: 30) do |http|
    response = http.request(req)
    [response.code.to_i, response.body.empty? ? {} : JSON.parse(response.body)]
  end
end

def wait_for_server
  100.times do
    return if request('GET', '/v1/version').first == 200
    sleep 0.1
  rescue StandardError
    sleep 0.1
  end
  raise 'split capture gate: server did not start'
end

Dir.mktmpdir('blit-split-capture-gate') do |work|
  launcher = File.join(work, 'blit-session.hibr')
  File.write(launcher, <<~'SH')
    #!/bin/sh
    case " $* " in
      *' --join '*) text=JOINED ;;
      *) text=PRIMARY ;;
    esac
    printf '\033[2J\033[H%s\r\n' "$text"
    sleep 30
  SH
  FileUtils.chmod(0o755, launcher)

  env = {
    'SDL_VIDEODRIVER' => 'dummy',
    'SDL_RENDERER' => 'software',
    'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms'),
    'BLIT_COMMAND_ALLOWLIST' => launcher
  }
  server = Process.spawn(env, BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  sessions = []
  original = nil
  begin
    wait_for_server
    original = request('GET', '/v1/topology').last
    split = request('PUT', '/v1/topology', {
      'revision' => original.fetch('revision'),
      'layout' => {'columns' => 1, 'rows' => 2}
    }).last
    panes = split.fetch('panes').sort_by { |pane| pane.fetch('bounds').fetch('y') }
    raise 'split capture gate: topology did not create two panes' unless panes.length == 2

    create = lambda do |arguments, pane_id|
      code, body = request('POST', '/v1/terminal-sessions', {
        'command' => launcher,
        'arguments' => arguments,
        'surface_mode' => 'terminal',
        'pane_id' => pane_id
      })
      raise "split capture gate: session create #{code} #{body}" unless code == 201
      sessions << body.fetch('id').to_i
      body
    end
    create.call(['--session', 'blit'], panes.fetch(1).fetch('id'))
    create.call(['--session', 'blit', '--join'], panes.fetch(0).fetch('id'))

    output = Dir.mktmpdir('blit-split-capture-result')
    stdout, stderr, status = Open3.capture3(
      {'BLIT_TRACE_LIMIT' => '128'},
      File.join(ROOT, 'tools/diagnostics/hibr-split-capture.sh'),
      BASE.to_s, '1', output,
      chdir: ROOT
    )
    raise "split capture gate: capture failed #{status}\n#{stdout}\n#{stderr}" unless status.success?
    verdict = JSON.parse(File.read(File.join(output, 'hibr-split-verdict.json')))
    raise "split capture gate: verdict was #{verdict}" unless verdict.fetch('result') == 'pass'
    inventory = JSON.parse(File.read(File.join(output, 'sessions.json')))
    raise 'split capture gate: inventory count mismatch' unless
      inventory.fetch('count') == inventory.fetch('sessions').length
    puts "HIBR_SPLIT_CAPTURE_PASS sessions=#{inventory.fetch('count')} " \
         "output=#{output}"
  ensure
    sessions.each { |id| request('DELETE', "/v1/terminal-sessions/#{id}") rescue nil }
    if original
      current = request('GET', '/v1/topology').last
      request('PUT', '/v1/topology', {
        'revision' => current.fetch('revision'), 'layout' => original.fetch('layout')
      }) if current.fetch('layout') != original.fetch('layout')
    end
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end
