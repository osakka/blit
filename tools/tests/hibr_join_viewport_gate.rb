#!/usr/bin/env ruby
# frozen_string_literal: true

# API regression gate for the Hibr join contract.  A primary and a --join
# client belong to one logical Hibr desktop, but each client has an
# independent local terminal viewport.  The join is not a framebuffer mirror:
# its PTY output must paint its own Blit surface. Pointer input enters that
# client's own PTY in local coordinates; native hold owns union rewriting.
# This fixture tests Blit's boundary, not native hold's own mouse parser.
require 'json'
require 'net/http'
require 'fileutils'
require 'uri'
require_relative 'gate_server'

PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '18998'))
ROOT = File.expand_path('../..', __dir__)
FAKE_DESKTOP = File.expand_path('../../build/blit/desktop', __dir__)
FAKE_DESKTOP_IMPL = File.expand_path('../../build/blit/desktop-gate.sh', __dir__)
SERVER = File.join(ROOT, 'build/blit/blit-server')

FileUtils.mkdir_p(File.dirname(FAKE_DESKTOP))
FileUtils.rm_f(FAKE_DESKTOP)
FileUtils.rm_f(FAKE_DESKTOP_IMPL)
File.write(FAKE_DESKTOP_IMPL, <<~'SH')
  #!/bin/sh
  # Keep the fixture's process alive so Blit can drain and parse its PTY.
  exec /bin/sh "$@"
SH
FileUtils.chmod(0o755, FAKE_DESKTOP_IMPL)
FileUtils.ln_s(FAKE_DESKTOP_IMPL, FAKE_DESKTOP)

env = {
  **gate_display_env,
  
  'BLIT_COMMAND_ALLOWLIST' => FAKE_DESKTOP
}
pid = Process.spawn(env, SERVER, PORT.to_s,
                    out: '/tmp/blit-hibr-join-viewport-gate.log', err: [:child, :out])
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
    raise 'timed out waiting for Hibr join viewport state' if
      Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline

    sleep 0.05
  end
end

def surface_text(surface)
  surface.fetch('cells').sort_by { |cell| [cell['y'], cell['x']] }
         .map { |cell| cell.fetch('text') }.join
end

base = URI("http://127.0.0.1:#{PORT}")
wait_for(base) do
  code, = request(base, 'GET', '/v1/version')
  code == 200
end

original_topology = request(base, 'GET', '/v1/topology').last
original_layout = original_topology.fetch('layout')
split = request(base, 'PUT', '/v1/topology', {
  'revision' => original_topology.fetch('revision'),
  'layout' => {'columns' => 1, 'rows' => 2}
}).last
panes = split.fetch('panes').sort_by { |pane| pane.fetch('pixel_region').fetch('y') }
top_pane = panes.fetch(0).fetch('id')
bottom = panes.fetch(1)
bottom_pane = bottom.fetch('id')
top_grid = panes.fetch(0).fetch('cell_grid')
inset = request(base, 'GET', '/v1/diagnostics').last.fetch('render').fetch('surface_inset_px')
click_x = top_grid.fetch('origin_x') + inset + 1
click_y = top_grid.fetch('origin_y') + inset + 1

create = lambda do |arguments, pane_id, surface_mode = 'terminal'|
  code, body = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => FAKE_DESKTOP,
    'arguments' => arguments,
    'surface_mode' => surface_mode,
    'pane_id' => pane_id,
    'surface' => {'width' => 80, 'height' => 24}
  })
  raise "session create failed: #{code} #{body}" unless code == 201
  sessions << body.fetch('id').to_i
  body
end

begin
  primary = create.call(['-c', "printf '\033[2J\033[HPRIMARY'; sleep 30",
                         '--session', 'blit'], bottom_pane)
  join = create.call(['-c', "printf '\033[2J\033[HJOINED'; sleep 30",
                      '--session', 'blit', '--join'], top_pane)
  primary_surface = primary.fetch('surface_id')
  join_surface = join.fetch('surface_id')

  raise "join surface mode was #{join['surface_mode'].inspect}" unless
    join.fetch('surface_mode') == 'terminal'
  raise 'primary and join reused one surface' if primary_surface == join_surface

  joined = wait_for(base) do
    code, snapshot = request(base, 'GET', "/v1/surfaces/#{join_surface}")
    raise "join snapshot failed: #{code}" unless code == 200
    surface_text(snapshot).include?('JOINED') ? snapshot : nil
  end
  primary_snapshot = request(base, 'GET', "/v1/surfaces/#{primary_surface}").last
  raise 'join viewport output leaked into primary surface' if
    surface_text(primary_snapshot).include?('JOINED')
  raise 'primary PTY output was not parsed into primary surface' unless
    surface_text(primary_snapshot).include?('PRIMARY')
  raise 'primary viewport unexpectedly contains joined output' if
    surface_text(joined).include?('PRIMARY')

  code, problem = request(base, 'PUT', "/v1/surfaces/#{join_surface}/cells", {
    'base_revision' => joined.fetch('revision'),
    'cells' => [{'x' => 0, 'y' => 0, 'text' => 'ILLEGAL'}]
  })
  raise "join accepted a direct write: #{code} #{problem}" unless
    code == 409 && problem['code'] == 'hibr_join_surface_read_only'

  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_motion', 'x' => click_x, 'y' => click_y})
  raise "pointer motion injection failed: #{code}" unless code == 202
  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_button', 'x' => click_x, 'y' => click_y,
                   'button' => 0, 'pressed' => true})
  raise "pointer press injection failed: #{code}" unless code == 202
  code, = request(base, 'POST', '/v1/input/inject',
                  {'kind' => 'pointer_button', 'x' => click_x, 'y' => click_y,
                   'button' => 0, 'pressed' => false})
  raise "pointer release injection failed: #{code}" unless code == 202

  code, trace = request(base, 'GET',
                        '/v1/diagnostics/trace?kind=pointer_button&limit=16')
  raise "pointer trace failed: #{code}" unless code == 200
  detail = trace.fetch('events').reverse.map { |event| event.fetch('detail', '') }
                       .find { |item| item.include?("target_session=#{join.fetch('id')}") }
  expected = "input_session=#{join.fetch('id')}"
  raise "join input was not routed to its own PTY: #{detail.inspect}" unless
    detail&.include?(expected)
  expected_column = 1
  raise "join input was transformed twice: #{detail.inspect}" unless
    detail&.include?("column=#{expected_column} row=1")

  puts "hibr join viewport gate: PASS primary=#{primary.fetch('id')} " \
       "join=#{join.fetch('id')} distinct_surfaces=true " \
       "input=#{expected} local_column=#{expected_column}"
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
end
