#!/usr/bin/env ruby
# frozen_string_literal: true

# A native Hibr desktop is one logical scene delivered through independent
# primary/join PTYs. This gate makes the two PTYs arrive in separate reads,
# then proves Blit holds presentation until both joined surfaces are dirty.
require 'json'
require 'net/http'
require 'fileutils'
require 'uri'
require 'tmpdir'
require_relative 'gate_server'

PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '18999'))
ROOT = File.expand_path('../..', __dir__)
FAKE_DESKTOP = File.expand_path('../../build/blit/desktop', __dir__)
FAKE_DESKTOP_IMPL = File.expand_path('../../build/blit/desktop-frame-barrier-gate.sh', __dir__)
SERVER = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))

FileUtils.mkdir_p(File.dirname(FAKE_DESKTOP))
FileUtils.rm_f(FAKE_DESKTOP)
FileUtils.rm_f(FAKE_DESKTOP_IMPL)
File.write(FAKE_DESKTOP_IMPL, <<~'SH')
  #!/bin/sh
  exec /bin/sh "$@"
SH
FileUtils.chmod(0o755, FAKE_DESKTOP_IMPL)
FileUtils.ln_s(FAKE_DESKTOP_IMPL, FAKE_DESKTOP)

env = {
  **gate_display_env,
  
  'BLIT_PSF_FONT' => ENV.fetch('BLIT_GATE_FONT', File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')),
  'BLIT_SETTINGS' => 'ignore',
  'BLIT_COMMAND_ALLOWLIST' => FAKE_DESKTOP
}
pid = Process.spawn(env, SERVER, PORT.to_s,
                    out: '/tmp/blit-hibr-join-frame-barrier-gate.log',
                    err: [:child, :out])
sessions = []

def request(base, method, path, payload = nil)
  klass = {'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
           'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete}.fetch(method)
  req = klass.new(base + path)
  if payload
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(payload)
  end
  response = Net::HTTP.start(req.uri.hostname, req.uri.port,
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
    raise 'timed out waiting for frame-barrier evidence' if
      Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline

    sleep 0.02
  end
end

base = URI("http://127.0.0.1:#{PORT}")
wait_for(base) { request(base, 'GET', '/v1/version').first == 200 }
original_topology = request(base, 'GET', '/v1/topology').last
original_layout = original_topology.fetch('layout')
panes = request(base, 'PUT', '/v1/topology', {
  'revision' => original_topology.fetch('revision'),
  'layout' => {'columns' => 1, 'rows' => 2}
}).last.fetch('panes').sort_by { |pane| pane.fetch('pixel_region').fetch('y') }
top_pane = panes.fetch(0).fetch('id')
bottom_pane = panes.fetch(1).fetch('id')

create = lambda do |arguments, pane_id|
  code, body = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => FAKE_DESKTOP,
    'arguments' => arguments,
    'surface_mode' => 'terminal',
    'pane_id' => pane_id,
    'surface' => {'width' => 80, 'height' => 24}
  })
  raise "session create failed: #{code} #{body}" unless code == 201
  sessions << body.fetch('id').to_i
  body
end

begin
  # Synchronize producers after both clients exist: the old fixture could
  # present the primary before its peer existed, then correctly time out.
  work = Dir.mktmpdir('blit-barrier')
  p_fifo = File.join(work, 'primary')
  j_fifo = File.join(work, 'joined')
  raise 'cannot create synchronization FIFOs' unless system('mkfifo', p_fifo, j_fifo)
  primary = create.call(['-c', "read -r start < '#{p_fifo}'; printf '\033[2J\033[HPRIMARY'; sleep 30",
                         '--session', 'blit'], bottom_pane)
  join = create.call(['-c', "read -r start < '#{j_fifo}'; sleep 0.004; printf '\033[2J\033[HJOINED'; sleep 30",
                      '--session', 'blit', '--join'], top_pane)
  p_writer = File.open(p_fifo, 'w')
  j_writer = File.open(j_fifo, 'w')
  p_writer.puts('go'); p_writer.close
  j_writer.puts('go'); j_writer.close

  trace = wait_for(base) do
    code, body = request(base, 'GET',
                         '/v1/diagnostics/trace?kind=hibr_frame_barrier&limit=128')
    raise "trace failed: #{code}" unless code == 200
    events = body.fetch('events')
    open_event = events.find { |event| event.fetch('kind') == 'hibr_frame_barrier' }
    release_event = events.find do |event|
      %w[hibr_frame_barrier_release hibr_frame_barrier_timeout].include?(
        event.fetch('kind')) && event.fetch('sequence') > open_event.fetch('sequence')
    end if open_event
    open_event && release_event ? [events, open_event, release_event] : nil
  end
  events, open_event, release_event = trace
  between = events.select do |event|
    event.fetch('sequence') > open_event.fetch('sequence') &&
      event.fetch('sequence') < release_event.fetch('sequence')
  end
  if between.any? { |event| event.fetch('kind') == 'render_path' }
    raise 'joined Hibr frame rendered before the barrier released'
  end
  raise 'barrier did not release after both members arrived' unless
    release_event.fetch('kind') == 'hibr_frame_barrier_release'

  puts "HIBR_JOIN_FRAME_BARRIER_PASS primary=#{primary.fetch('id')} " \
       "join=#{join.fetch('id')} release=#{release_event.fetch('kind')}"
ensure
  sessions.each do |id|
    request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  rescue StandardError
    nil
  end
  begin
    current = request(base, 'GET', '/v1/topology').last
    request(base, 'PUT', '/v1/topology', {
      'revision' => current.fetch('revision'),
      'layout' => original_layout
    }) if current.fetch('layout') != original_layout
  rescue StandardError
    nil
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  FileUtils.rm_f(FAKE_DESKTOP)
  FileUtils.rm_f(FAKE_DESKTOP_IMPL)
  FileUtils.remove_entry(work) if work && File.directory?(work)
end
