#!/usr/bin/env ruby
# frozen_string_literal: true

# API regression gate for the Blit-owned Hibr join contract.  The primary is
# the only desktop writer; a joined pane mirrors its committed surface and
# sends hit-tested input to the primary owner.
require 'json'
require 'net/http'
require 'fileutils'
require 'uri'

PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '18998'))
ROOT = File.expand_path('../..', __dir__)
FAKE_DESKTOP = File.expand_path('../../build/blit/desktop', __dir__)
SERVER = File.join(ROOT, 'build/blit/blit-server')

FileUtils.mkdir_p(File.dirname(FAKE_DESKTOP))
FileUtils.rm_f(FAKE_DESKTOP)
File.symlink('/bin/sh', FAKE_DESKTOP)

env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms'),
  'BLIT_COMMAND_ALLOWLIST' => FAKE_DESKTOP
}
pid = Process.spawn(env, SERVER, PORT.to_s,
                    out: '/tmp/blit-hibr-join-mirror-gate.log', err: [:child, :out])
sessions = []

def request(base, method, path, payload = nil)
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'PUT' => Net::HTTP::Put,
    'DELETE' => Net::HTTP::Delete
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if payload
  req.body = JSON.generate(payload) if payload
  response = Net::HTTP.start(uri.hostname, uri.port,
                             open_timeout: 2, read_timeout: 4) do |http|
    http.request(req)
  end
  [response.code.to_i, response.body.empty? ? {} : JSON.parse(response.body)]
end

def wait_for(base, seconds = 5)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + seconds
  loop do
    value = begin
      yield
    rescue StandardError
      nil
    end
    return value if value
    raise 'timed out waiting for join mirror state' if
      Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline

    sleep 0.05
  end
end

base = URI("http://127.0.0.1:#{PORT}")
wait_for(base) do
  code, = request(base, 'GET', '/v1/version')
  code == 200
end

create = lambda do |arguments|
  code, body = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => FAKE_DESKTOP,
    'arguments' => arguments,
    'surface' => {'width' => 80, 'height' => 24}
  })
  raise "session create failed: #{code} #{body}" unless code == 201
  sessions << body.fetch('id').to_i
  body
end

begin
  primary = create.call(['-c', 'sleep 30', '--session', 'blit'])
  join = create.call(['-c', 'sleep 30', '--session', 'blit', '--join'])
  primary_surface = primary.fetch('surface_id')
  join_surface = join.fetch('surface_id')
  raise "join surface mode was #{join['surface_mode'].inspect}" unless
    join.fetch('surface_mode') == 'mirror'

  code, snapshot = request(base, 'GET', "/v1/surfaces/#{primary_surface}")
  raise "primary snapshot failed: #{code}" unless code == 200
  base_revision = snapshot.fetch('revision')
  code, = request(base, 'PUT', "/v1/surfaces/#{primary_surface}/cells", {
    'base_revision' => base_revision,
    'cells' => [{'x' => 0, 'y' => 0, 'text' => 'PRIMARY'}],
    'presentation' => 'immediate'
  })
  raise "primary cell update failed: #{code}" unless code == 200

  mirrored = wait_for(base) do
    mirror_code, mirror = request(base, 'GET', "/v1/surfaces/#{join_surface}")
    raise "join snapshot failed: #{mirror_code}" unless mirror_code == 200
    first_row = mirror.fetch('cells').select { |cell| cell['y'] == 0 }
                       .sort_by { |cell| cell['x'] }
                       .map { |cell| cell.fetch('text') }.join
    mirror if first_row.start_with?('PRIMARY')
  end
  raise "unexpected mirrored dimensions: #{mirrored['width']}x#{mirrored['height']}" unless
    mirrored['width'] == 80 && mirrored['height'] == 24

  code, problem = request(base, 'PUT', "/v1/surfaces/#{join_surface}/cells", {
    'base_revision' => mirrored.fetch('revision'),
    'cells' => [{'x' => 0, 'y' => 0, 'text' => 'ILLEGAL'}]
  })
  raise "mirror accepted a direct write: #{code} #{problem}" unless
    code == 409 && problem['code'] == 'mirror_surface_read_only'

  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_motion', 'x' => 10, 'y' => 400})
  raise "pointer motion injection failed: #{code}" unless code == 202
  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_button', 'button' => 0, 'pressed' => true})
  raise "pointer press injection failed: #{code}" unless code == 202
  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_button', 'button' => 0, 'pressed' => false})
  raise "pointer release injection failed: #{code}" unless code == 202

  code, trace = request(base, 'GET', '/v1/diagnostics/trace?kind=pointer_button&limit=16')
  raise "pointer trace failed: #{code}" unless code == 200
  detail = trace.fetch('events').reverse.map { |event| event.fetch('detail', '') }
                       .find { |item| item.include?("target_session=#{join.fetch('id')}") }
  expected = "input_session=#{primary.fetch('id')}"
  raise "join input was not routed to primary: #{detail.inspect}" unless
    detail&.include?(expected)

  puts "hibr join mirror gate: PASS primary=#{primary.fetch('id')} " \
       "join=#{join.fetch('id')} mirrored_revision=#{mirrored.fetch('revision')} " \
       "input=#{expected}"
ensure
  sessions.each do |id|
    request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  rescue StandardError
    nil
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  FileUtils.rm_f(FAKE_DESKTOP)
end
