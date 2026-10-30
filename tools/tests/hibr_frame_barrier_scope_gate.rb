#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0082, revised. The frame barrier holds one monitor's Hibr views until
# all have drawn, and must not hold every frame of a view in use:
#
#   - a view that let a barrier time out is not waited for again while it
#     stays quiet: one timeout, then the active view presents freely (the
#     owner's machine timed out 254 times in 30 s, once a frame, before);
#   - views on different monitors never share a barrier: two monitors flip
#     at their own vblanks and could not show a frame together.
#
# hibr_join_frame_barrier_gate.rb keeps the case the barrier exists for:
# two quiet views redrawn a few milliseconds apart appear together.
require 'json'
require 'net/http'
require 'fileutils'
require 'uri'
require 'tmpdir'
require_relative 'gate_server'

PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '18998'))
ROOT = File.expand_path('../..', __dir__)
FAKE_DESKTOP = File.expand_path('../../build/blit/desktop', __dir__)
FAKE_DESKTOP_IMPL = File.expand_path('../../build/blit/desktop-frame-barrier-scope-gate.sh', __dir__)
SERVER = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))

FileUtils.mkdir_p(File.dirname(FAKE_DESKTOP))
FileUtils.rm_f(FAKE_DESKTOP)
File.write(FAKE_DESKTOP_IMPL, "#!/bin/sh\nexec /bin/sh \"$@\"\n")
FileUtils.chmod(0o755, FAKE_DESKTOP_IMPL)
FileUtils.ln_s(FAKE_DESKTOP_IMPL, FAKE_DESKTOP)

env = gate_display_env
# Two monitors.
File.write(env.fetch('BLIT_DRM_MOCK_STATE'), "2 0 0 0\n1 1024 768\n2 1024 768\n")
env = env.merge('BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                'BLIT_SETTINGS' => 'ignore', 'BLIT_LOGIN_AUTOSTART' => '0',
                'BLIT_COMMAND_ALLOWLIST' => FAKE_DESKTOP)
pid = Process.spawn(env, SERVER, PORT.to_s, out: '/tmp/blit-frame-barrier-scope-gate.log',
                                            err: %i[child out])
sessions = []
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, payload = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  if payload
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(payload)
  end
  response = Net::HTTP.start(req.uri.hostname, req.uri.port, open_timeout: 2, read_timeout: 4) do |http|
    http.request(req)
  end
  [response.code.to_i, response.body.to_s.empty? ? {} : JSON.parse(response.body)]
end

# The kind filter matches by prefix, so one query has all three kinds.
def barrier_events(after)
  request('GET', '/v1/diagnostics/trace?kind=hibr_frame_barrier&limit=128').last.fetch('events')
    .select { |e| e.fetch('sequence') > after }
end

# The newest barrier event so far; sequences are compared within one kind.
def newest_sequence
  barrier_events(-1).map { |e| e.fetch('sequence') }.max || 0
end

begin
  60.times do
    break if (request('GET', '/v1/version').first rescue 0) == 200

    sleep 0.1
  end
  topology = request('GET', '/v1/topology').last
  panes = request('PUT', '/v1/topology', { 'revision' => topology.fetch('revision'),
                                          'layout' => { 'columns' => 1, 'rows' => 2 } })
          .last.fetch('panes')
  by_display = panes.group_by { |p| p.fetch('display_id') }
  raise "expected two monitors, got #{by_display.keys}" unless by_display.size == 2

  first, second = by_display.values.map { |list| list.sort_by { |p| p.fetch('pixel_region').fetch('y') } }
  create = lambda do |arguments, pane|
    code, body = request('POST', '/v1/terminal-sessions',
                         { 'command' => FAKE_DESKTOP, 'arguments' => arguments, 'surface_mode' => 'terminal',
                           'pane_id' => pane.fetch('id'), 'surface' => { 'width' => 80, 'height' => 20 } })
    raise "session create failed: #{code} #{body}" unless code == 201

    sessions << body.fetch('id').to_i
    body
  end
  work = Dir.mktmpdir('blit-barrier-scope')

  # 1. One monitor: the join draws once and goes quiet; the primary keeps
  #    drawing for 1.5 s (75 frames' worth). At most one timeout.
  go = File.join(work, 'go-a')
  system('mkfifo', go) or raise 'mkfifo'
  create.call(['-c', "read -r s < '#{go}'; i=0; while [ $i -lt 75 ]; do printf 'x'; i=$((i+1)); sleep 0.02; done; sleep 30",
               '--session', 'quiet'], first[0])
  create.call(['-c', "printf 'JOINED'; sleep 30", '--session', 'quiet', '--join'], first[1])
  sleep 1.5
  mark = newest_sequence
  File.open(go, 'w') { |f| f.puts('go') }
  sleep 2.2
  events = barrier_events(mark)
  timeouts = events.count { |e| e['kind'] == 'hibr_frame_barrier_timeout' }
  if timeouts > 1
    events.each { |e| warn "#{e.fetch("sequence")} #{e.fetch("kind")} #{e.fetch("detail")}" }
    raise "a quiet view held the active one: #{timeouts} timeouts in 75 frames"
  end

  sessions.each { |id| request('DELETE', "/v1/terminal-sessions/#{id}") }
  sessions.clear
  sleep 0.5

  # 2. Two monitors: views redrawn 4 ms apart on different outputs never
  #    open a barrier together.
  p_go = File.join(work, 'go-p')
  j_go = File.join(work, 'go-j')
  system('mkfifo', p_go, j_go) or raise 'mkfifo'
  create.call(['-c', "read -r s < '#{p_go}'; printf '\\033[2J\\033[HPRIMARY'; sleep 30", '--session', 'split'],
              first[0])
  create.call(['-c', "read -r s < '#{j_go}'; sleep 0.004; printf '\\033[2J\\033[HJOINED'; sleep 30",
               '--session', 'split', '--join'], second[0])
  sleep 1.5
  mark = newest_sequence
  File.open(p_go, 'w') { |f| f.puts('go') }
  File.open(j_go, 'w') { |f| f.puts('go') }
  sleep 0.6
  opened = barrier_events(mark).count { |e| e['kind'] == 'hibr_frame_barrier' }
  raise "views on two monitors shared a barrier (#{opened} opened)" unless opened.zero?

  puts "HIBR_FRAME_BARRIER_SCOPE_PASS quiet_view_timeouts=#{timeouts} cross_monitor_barriers=#{opened}"
ensure
  sessions.each do |id|
    request('DELETE', "/v1/terminal-sessions/#{id}")
  rescue StandardError
    nil
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  FileUtils.rm_f(FAKE_DESKTOP)
  FileUtils.rm_f(FAKE_DESKTOP_IMPL)
  FileUtils.remove_entry(work) if work && File.directory?(work)
end
