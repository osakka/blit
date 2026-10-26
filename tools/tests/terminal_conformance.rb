#!/usr/bin/env ruby
# frozen_string_literal: true

# Gate 2 black-box conformance runner. The server is started by the caller;
# this tool asserts only the canonical PTY-to-surface contract over HTTP.

require 'json'
require 'base64'
require 'zlib'
require 'net/http'
require 'tempfile'
require 'timeout'
require 'uri'

base = URI(ENV.fetch('BLIT_TEST_SERVER', 'http://127.0.0.1:19195'))

def request(base, method, path, body = nil, headers = {})
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'PUT' => Net::HTTP::Put,
    'DELETE' => Net::HTTP::Delete,
    'PATCH' => Net::HTTP::Patch
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 5) do |http|
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless response.code.start_with?('2')
  return nil if response.code == '204'

  JSON.parse(response.body)
end

def request_with_status(base, method, path, body = nil, headers = {})
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'PUT' => Net::HTTP::Put,
    'DELETE' => Net::HTTP::Delete,
    'PATCH' => Net::HTTP::Patch
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  headers.each { |key, value| req[key] = value }
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 5) do |http|
    http.request(req)
  end
  body = response.body.to_s
  [response.code.to_i, body.empty? ? nil : JSON.parse(body)]
end

def cell_map(snapshot)
  snapshot.fetch('cells').each_with_object({}) do |cell, result|
    result[[cell.fetch('x'), cell.fetch('y')]] = cell
  end
end

def row_text(snapshot, row = 0)
  cells = cell_map(snapshot)
  cells.keys.select { |x, y| y == row }.sort.map { |x, y| cells.fetch([x, y]).fetch('text') }.join
end

def screen_text(snapshot)
  cells = cell_map(snapshot)
  cells.keys.map(&:last).uniq.sort.map { |row| row_text(snapshot, row) }.join
end

def assert_equal(expected, actual, detail)
  return if expected == actual

  raise "#{detail}: expected #{expected.inspect}, got #{actual.inspect}"
end

expected_version = ENV['BLIT_EXPECTED_VERSION']
unless expected_version.nil? || expected_version.empty?
  version = request(base, 'GET', '/v1/version')
  assert_equal(expected_version, version.fetch('server_version'),
               'server version identity')
  server = request(base, 'GET', '/v1/server')
  assert_equal(expected_version, server.fetch('server_version'),
               'server details version identity')
  raise 'server capability missing network-terminal-input' unless
    server.fetch('capabilities').include?('network-terminal-input')
  puts 'PASS network-terminal-input-capability'
  puts "PASS version-identity-#{expected_version}"
end

topology = request(base, 'GET', '/v1/topology')
revision = topology.fetch('revision')
placements = topology.fetch('outputs').map.with_index do |output, index|
  bounds = output.fetch('bounds')
  {
    'display_id' => output.fetch('display_id'),
    'x' => bounds.fetch('x') + (index.zero? ? 7 : 0),
    'y' => bounds.fetch('y') + (index.zero? ? 9 : 0)
  }
end
placed = request(base, 'PUT', '/v1/topology', {
  'revision' => revision,
  'layout' => topology.fetch('layout'),
  'placements' => placements
}, { 'If-Match' => revision.to_s })
assert_equal(placements.map { |placement| placement.values_at('display_id', 'x', 'y') },
             placed.fetch('outputs').map { |output|
               bounds = output.fetch('bounds')
               [output.fetch('display_id'), bounds.fetch('x'), bounds.fetch('y')]
             }, 'topology placement round trip')
invalid_status, invalid_response = request_with_status(
  base, 'PUT', '/v1/topology', {
    'revision' => placed.fetch('revision'),
    'layout' => placed.fetch('layout'),
    'placements' => []
  }, { 'If-Match' => placed.fetch('revision').to_s })
assert_equal(400, invalid_status, 'incomplete topology placement status')
assert_equal('invalid_topology_placements', invalid_response.fetch('code'),
             'incomplete topology placement code')
puts 'PASS topology-placement-round-trip'

pointer_diagnostics = request(base, 'GET', '/v1/diagnostics')
pointer = pointer_diagnostics.fetch('render').fetch('pointer')
%w[valid x y output previous_x previous_y previous_output dirty].each do |field|
  raise "pointer diagnostics missing #{field}" unless pointer.key?(field)
end
puts 'PASS pointer-diagnostics'

runtime_diagnostics = request(base, 'GET', '/v1/diagnostics/runtime')
performance = runtime_diagnostics.fetch('performance')
%w[input_events last_input_dispatch_ms max_input_dispatch_ms
   last_kms_present_ms max_kms_present_ms surface_commit_hz
   surface_commit_frames surface_commit_window_ms
   surface_commit_window_sequence surface_commit_age_ms
   last_surface_commit_gap_ms max_surface_commit_gap_ms].each do |field|
  raise "runtime diagnostics missing #{field}" unless performance.key?(field)
end
puts 'PASS input-diagnostics'

runtime_diagnostics.fetch('snapshot').fetch('render').fetch('outputs').each do |output|
  %w[last_present_ms max_present_ms present_calls].each do |field|
    raise "per-output diagnostics missing #{field}" unless output.key?(field)
  end
end
puts 'PASS per-output-presentation-diagnostics'

missing_surface_status, missing_surface_response = request_with_status(
  base, 'POST', '/v1/terminal-sessions', { 'command' => '/bin/true' })
assert_equal(400, missing_surface_status, 'missing surface status')
assert_equal('session_create_failed', missing_surface_response.fetch('code'),
             'missing surface code')
puts 'PASS schema-required-surface'

derived_session = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 1'],
  'display_id' => topology.fetch('outputs').fetch(0).fetch('display_id')
})
derived_summary = request(base, 'GET',
                          "/v1/surfaces/surface-#{derived_session.fetch('id')}/summary")
%w[id revision width height non_empty_cells cursor graphics_images damage].each do |field|
  raise "surface summary missing #{field}" unless derived_summary.key?(field)
end
assert_equal(derived_session.fetch('surface_id'), derived_summary.fetch('id'),
             'surface summary identity')
puts 'PASS surface-summary-diagnostics'
derived_surface = request(base, 'GET',
                          "/v1/surfaces/surface-#{derived_session.fetch('id')}/cells")
display_bounds = topology.fetch('outputs').fetch(0).fetch('bounds')
assert_equal(display_bounds.fetch('width') / 8,
             derived_surface.fetch('width'), 'display-derived surface width')
assert_equal(display_bounds.fetch('height') / 16,
             derived_surface.fetch('height'), 'display-derived surface height')
request(base, 'DELETE', "/v1/terminal-sessions/#{derived_session.fetch('id')}")
puts 'PASS display-derived-surface-geometry'

# ADR 0186: effects are per pane and live in document.effects; this endpoint
# only reports them. pane_effects_gate.rb covers drawing and settings.
effects = request(base, 'GET', '/v1/effects')
assert_equal({ 'corners' => 'local', 'curvature' => 'presentation', 'glow' => 'presentation',
               'rgb_shift' => 'presentation', 'scanlines' => 'local' },
             effects.fetch('available').to_h { |e| [e.fetch('name'), e.fetch('class')] },
             'available pane effects and their classes')
assert_equal(true, effects.fetch('enabled'), 'effects are switched on by default')
assert_equal(topology.fetch('panes').map { |p| p.fetch('id') }.sort,
             effects.fetch('panes').map { |p| p.fetch('pane_id') }.sort, 'effects list every live pane')
assert_equal(true, effects.fetch('panes').all? { |p| p.fetch('effects') == {} }, 'no effect by default')
effect_write_status, effect_write = request_with_status(base, 'PUT', '/v1/effects', {})
assert_equal(405, effect_write_status, 'effects endpoint is read-only')
assert_equal('effects_read_only', effect_write.fetch('code'), 'read-only effects code')
puts 'PASS pane-effects-read-only'

overlay_display = topology.fetch('outputs').fetch(0).fetch('display_id')
overlay = request(base, 'POST', '/v1/overlays', {
  'display_id' => overlay_display,
  'x' => 2,
  'y' => 3,
  'width' => 8,
  'height' => 4,
  'color' => '#ff0000',
  'opacity' => 75
})
assert_equal(overlay_display, overlay.fetch('display_id'),
             'overlay display target')
assert_equal(75, overlay.fetch('opacity'), 'overlay opacity')
overlays = request(base, 'GET', '/v1/overlays')
assert_equal(1, overlays.fetch('overlays').length, 'overlay listing')
request(base, 'DELETE', "/v1/overlays/#{overlay.fetch('id').sub('overlay-', '')}")
assert_equal(0, request(base, 'GET', '/v1/overlays').fetch('overlays').length,
             'overlay deletion')
puts 'PASS bounded-overlay-lifecycle'

def run_case(base, name, command, width: 20, height: 4, environment: {})
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    # Keep the PTY open long enough for the gateway's nonblocking drain loop
    # to observe the command's final bytes before lifecycle reaping closes it.
    'arguments' => ['-c', "#{command}; sleep 0.1"],
    'environment' => environment,
    'surface' => { 'width' => width, 'height' => height }
  })
  id = created.fetch('id')
  Timeout.timeout(5) do
    loop do
      state = request(base, 'GET', "/v1/terminal-sessions/#{id}")
      break unless %w[starting running].include?(state.fetch('state'))

      sleep 0.02
    end
  end
  snapshot = request(base, 'GET', "/v1/surfaces/surface-#{id}/cells")
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  puts "PASS #{name}"
  snapshot
rescue StandardError => error
  warn "FAIL #{name}: #{error.message}"
  raise
end

api_cell_created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 1'],
  'surface' => { 'width' => 8, 'height' => 2 }
})
api_cell_id = api_cell_created.fetch('id')
request(base, 'PUT', "/v1/surfaces/surface-#{api_cell_id}/cells", {
  'base_revision' => 1,
  'cells' => [{ 'x' => 0, 'y' => 0, 'text' => "\u{1F600}\u{FE0F}X" }]
})
api_cell_snapshot = request(base, 'GET',
                            "/v1/surfaces/surface-#{api_cell_id}/cells")
api_cells = cell_map(api_cell_snapshot)
assert_equal("\u{1F600}\u{FE0F}", api_cells.fetch([0, 0]).fetch('text'),
             'API wide UTF-8 cell text')
assert_equal(2, api_cells.fetch([0, 0]).fetch('width'),
             'API wide UTF-8 cell width')
assert_equal(0, api_cells.fetch([1, 0]).fetch('width'),
             'API wide continuation width')
assert_equal('X', api_cells.fetch([2, 0]).fetch('text'),
             'API wide UTF-8 following cell')
request(base, 'DELETE', "/v1/terminal-sessions/#{api_cell_id}")
puts 'PASS api-wide-utf8-cell-update'

network_input = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'stty -echo; dd bs=1 count=8 2>/dev/null'],
  'surface' => { 'width' => 32, 'height' => 2 }
})
network_input_id = network_input.fetch('id')
Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{network_input_id}")
    break if %w[starting running].include?(state.fetch('state'))

    sleep 0.02
  end
end
network_receipt = request(base, 'POST',
                          "/v1/terminal-sessions/#{network_input_id}/input",
                          { 'data' => "NETWORK\n" })
assert_equal('raw', network_receipt.fetch('mode'), 'network input mode')
assert_equal(8, network_receipt.fetch('bytes_requested'),
             'network input requested bytes')
assert_equal(8, network_receipt.fetch('bytes_written'),
             'network input written bytes')
network_snapshot = nil
Timeout.timeout(5) do
  loop do
    network_snapshot = request(base, 'GET',
                                "/v1/surfaces/surface-#{network_input_id}/cells")
    break if screen_text(network_snapshot).include?('NETWORK')

    sleep 0.02
  end
end
raise 'network input did not reach the PTY' unless
  screen_text(network_snapshot).include?('NETWORK')
request(base, 'DELETE', "/v1/terminal-sessions/#{network_input_id}")
puts 'PASS network-terminal-input-raw'

json_escape_input = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/cat',
  'surface' => { 'width' => 32, 'height' => 2 }
})
json_escape_input_id = json_escape_input.fetch('id')
json_escape_receipt = request(base, 'POST',
                              "/v1/terminal-sessions/#{json_escape_input_id}/input",
                              { 'data' => "\eX" })
assert_equal(2, json_escape_receipt.fetch('bytes_requested'),
             'JSON escaped terminal input requested bytes')
assert_equal(2, json_escape_receipt.fetch('bytes_written'),
             'JSON escaped terminal input written bytes')
json_escape_snapshot = nil
Timeout.timeout(5) do
  loop do
    json_escape_snapshot = request(base, 'GET',
                                   "/v1/surfaces/surface-#{json_escape_input_id}/cells")
    break if screen_text(json_escape_snapshot).include?('^[X')

    sleep 0.02
  end
end
raise 'JSON escaped terminal input did not decode to ESC' unless
  screen_text(json_escape_snapshot).include?('^[X')
request(base, 'DELETE', "/v1/terminal-sessions/#{json_escape_input_id}")
puts 'PASS network-terminal-input-json-escapes'

paste_input = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c',
                  "stty -echo -icanon min 0 time 5; printf '\\033[?2004h'; dd bs=1 count=13 2>/dev/null | od -An -tx1 | tr -d ' ' | tr -d '\\n'"],
  'surface' => { 'width' => 40, 'height' => 2 }
})
paste_input_id = paste_input.fetch('id')
sleep 0.1
paste_receipt = request(base, 'POST',
                        "/v1/terminal-sessions/#{paste_input_id}/input",
                        { 'data' => 'P', 'mode' => 'paste' })
assert_equal('paste', paste_receipt.fetch('mode'), 'network paste mode')
assert_equal(1, paste_receipt.fetch('bytes_requested'),
             'network paste requested bytes')
assert_equal(13, paste_receipt.fetch('bytes_written'),
             'network paste framed bytes')
paste_snapshot = nil
Timeout.timeout(5) do
  loop do
    paste_snapshot = request(base, 'GET',
                             "/v1/surfaces/surface-#{paste_input_id}/cells")
    break if screen_text(paste_snapshot).include?('1b5b3230307e501b5b3230317e')

    sleep 0.02
  end
end
raise 'network paste framing did not reach the PTY' unless
  screen_text(paste_snapshot).include?('1b5b3230307e501b5b3230317e')
request(base, 'DELETE', "/v1/terminal-sessions/#{paste_input_id}")
puts 'PASS network-terminal-input-paste'

scoped_style_created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 1'],
  'surface_mode' => 'external',
  'surface' => { 'width' => 8, 'height' => 2 }
})
scoped_style_id = scoped_style_created.fetch('id')
request(base, 'PUT', "/v1/surfaces/surface-#{scoped_style_id}/cells", {
  'base_revision' => 1,
  'cells' => [
    { 'x' => 0, 'y' => 0, 'text' => 'A',
      'style' => { 'foreground' => '#00FF00' } },
    { 'x' => 1, 'y' => 0, 'text' => 'B',
      'style' => { 'background' => '#AA0000' } },
    { 'x' => 2, 'y' => 0, 'text' => 'C',
      'style' => { 'inverse' => true } },
    { 'x' => 3, 'y' => 0, 'text' => 'D' }
  ]
})
scoped_style_cells = cell_map(request(base, 'GET',
                                      "/v1/surfaces/surface-#{scoped_style_id}/cells"))
assert_equal('#00ff00', scoped_style_cells.fetch([0, 0]).fetch('style').fetch('foreground'),
             'cell style foreground scope')
assert_equal(false, scoped_style_cells.fetch([0, 0]).fetch('style').fetch('inverse'),
             'cell style inverse does not leak')
assert_equal('#aa0000', scoped_style_cells.fetch([1, 0]).fetch('style').fetch('background'),
             'cell style background scope')
assert_equal(false, scoped_style_cells.fetch([1, 0]).fetch('style').fetch('inverse'),
             'cell style inverse remains local')
assert_equal(true, scoped_style_cells.fetch([2, 0]).fetch('style').fetch('inverse'),
             'cell style inverse applies locally')
assert_equal(nil, scoped_style_cells.fetch([3, 0])['style'],
             'unstyled cell remains unstyled')
request(base, 'DELETE', "/v1/terminal-sessions/#{scoped_style_id}")
puts 'PASS cell-style-scope'

def run_session_field_case(base, name, command, field, expected)
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c', "#{command}; sleep 0.1"],
    'surface' => { 'width' => 20, 'height' => 4 }
  })
  id = created.fetch('id')
  Timeout.timeout(5) do
    loop do
      state = request(base, 'GET', "/v1/terminal-sessions/#{id}")
      break unless %w[starting running].include?(state.fetch('state'))

      sleep 0.02
    end
  end
  state = request(base, 'GET', "/v1/terminal-sessions/#{id}")
  assert_equal(expected, state.fetch(field), name)
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  puts "PASS #{name}"
rescue StandardError => error
  warn "FAIL #{name}: #{error.message}"
  raise
end

def run_immediate_output_case(base)
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c', "printf 'FINAL-BYTE'"],
    'surface' => { 'width' => 20, 'height' => 4 }
  })
  id = created.fetch('id')
  deadline = Time.now + 5
  snapshot = nil
  loop do
    snapshot = request(base, 'GET', "/v1/surfaces/surface-#{id}/cells")
    break if screen_text(snapshot).include?('FINAL-BYTE')
    raise 'final PTY bytes were not drained before timeout' if Time.now >= deadline

    sleep 0.02
  end
  assert_equal(true, screen_text(snapshot).include?('FINAL-BYTE'),
               'immediate final PTY bytes')
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  puts 'PASS immediate-final-pty-bytes'
end

run_immediate_output_case(base)

created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', "printf 'AB'; sleep 1"],
  'surface' => { 'width' => 4, 'height' => 2 }
})
resize_id = created.fetch('id')
resize_state = Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{resize_id}")
    break state unless %w[starting].include?(state.fetch('state'))

    sleep 0.02
  end
end
request(base, 'PATCH', "/v1/terminal-sessions/#{resize_id}",
        { 'surface' => { 'width' => 2, 'height' => 2 } },
        { 'If-Match' => resize_state.fetch('revision').to_s })
resize_snapshot = request(base, 'GET', "/v1/surfaces/surface-#{resize_id}/cells")
resize_cells = cell_map(resize_snapshot)
assert_equal('A', resize_cells.fetch([0, 0]).fetch('text'),
             'resize preserves overlapping first cell')
assert_equal('B', resize_cells.fetch([1, 0]).fetch('text'),
             'resize preserves overlapping second cell')
raise 'resize exposed cells outside new bounds' if resize_cells.keys.any? do |x, y|
  x >= 2 || y >= 2
end
request(base, 'DELETE', "/v1/terminal-sessions/#{resize_id}")
puts 'PASS resize-preserves-grid'

created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 1'],
  'surface' => { 'width' => 4, 'height' => 2 }
})
idempotent_id = created.fetch('id')
idempotent_state = request(base, 'GET', "/v1/terminal-sessions/#{idempotent_id}")
idempotent_headers = {
  'If-Match' => idempotent_state.fetch('revision').to_s,
  'Idempotency-Key' => 'terminal-session-patch-replay'
}
idempotent_body = { 'surface' => { 'width' => 3, 'height' => 2 } }
first_status, first_response = request_with_status(
  base, 'PATCH', "/v1/terminal-sessions/#{idempotent_id}",
  idempotent_body, idempotent_headers)
second_status, second_response = request_with_status(
  base, 'PATCH', "/v1/terminal-sessions/#{idempotent_id}",
  idempotent_body, idempotent_headers)
assert_equal(200, first_status, 'session PATCH idempotency first status')
assert_equal(200, second_status, 'session PATCH idempotency replay status')
assert_equal(first_response, second_response, 'session PATCH idempotency replay body')
conflict_status, conflict_response = request_with_status(
  base, 'PATCH', "/v1/terminal-sessions/#{idempotent_id}",
  { 'surface' => { 'width' => 2, 'height' => 2 } }, idempotent_headers)
assert_equal(409, conflict_status, 'session PATCH idempotency conflict status')
assert_equal('idempotency_conflict', conflict_response.fetch('code'),
             'session PATCH idempotency conflict code')
request(base, 'DELETE', "/v1/terminal-sessions/#{idempotent_id}")
puts 'PASS terminal-session-patch-idempotency'

created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 1'],
  'surface' => { 'width' => 4, 'height' => 2 }
})
route_exit_id = created.fetch('id')
request(base, 'PUT', '/v1/input/routes/route-exit', {
  'device_id' => 'evdev-keyboard',
  'target_session_id' => "session-#{route_exit_id}",
  'enabled' => true
})
Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{route_exit_id}")
    break unless %w[starting running].include?(state.fetch('state'))

    sleep 0.02
  end
end
route = request(base, 'GET', '/v1/input/routes').fetch('routes').find do |item|
  item.fetch('id') == 'route-exit'
end
raise 'natural session exit removed its input route' if route.nil?
assert_equal(false, route.fetch('enabled'),
             'natural session exit disables input route')
assert_equal("session-#{route_exit_id}", route.fetch('target_session_id'),
             'disabled route retains target identity')
stopped_route_status, stopped_route = request_with_status(
  base, 'PUT', '/v1/input/routes/route-exit', {
    'device_id' => 'evdev-keyboard',
    'target_session_id' => "session-#{route_exit_id}",
    'enabled' => true
  })
assert_equal(409, stopped_route_status, 'stopped route target status')
assert_equal('input_route_target_stopped', stopped_route.fetch('code'),
             'stopped route target code')
deleted_route_status, = request_with_status(base, 'DELETE', '/v1/input/routes/route-exit')
assert_equal(204, deleted_route_status, 'input route delete status')
remaining_routes = request(base, 'GET', '/v1/input/routes').fetch('routes')
raise 'deleted input route remains visible' if remaining_routes.any? {
  |item| item.fetch('id') == 'route-exit'
}
request(base, 'DELETE', "/v1/terminal-sessions/#{route_exit_id}")
puts 'PASS input-route-target-exit'

created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', "printf 'a\\nb\\nc'; sleep 1"],
  'surface' => { 'width' => 4, 'height' => 2 }
})
scrollback_id = created.fetch('id')
Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{scrollback_id}")
    break unless %w[starting running].include?(state.fetch('state'))

    sleep 0.02
  end
end
scrollback = request(base, 'GET',
                     "/v1/surfaces/surface-#{scrollback_id}/scrollback")
assert_equal(1, scrollback.fetch('count'), 'scrollback line count')
assert_equal('a', scrollback.fetch('lines').fetch(0).fetch('text'),
             'scrollback oldest line')
request(base, 'PATCH', "/v1/surfaces/surface-#{scrollback_id}/scrollback",
        { 'view' => 1 })
historical = request(base, 'GET', "/v1/surfaces/surface-#{scrollback_id}/cells")
assert_equal('a', cell_map(historical).fetch([0, 0]).fetch('text'),
             'scrollback historical view renders retained row')
request(base, 'PATCH', "/v1/surfaces/surface-#{scrollback_id}/scrollback",
        { 'view' => 0 })
live = request(base, 'GET', "/v1/surfaces/surface-#{scrollback_id}/cells")
live_cells = cell_map(live)
assert_equal('b', live_cells.fetch([0, 0]).fetch('text'),
             'scrollback view returns to live first row')
assert_equal('c', live_cells.fetch([0, 1]).fetch('text'),
             'scrollback view returns to live second row')
clear_scrollback = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', "printf 'a\\nb\\nc\\033[3J'; sleep 1"],
  'surface' => { 'width' => 4, 'height' => 2 }
})
clear_scrollback_id = clear_scrollback.fetch('id')
Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{clear_scrollback_id}")
    break unless %w[starting running].include?(state.fetch('state'))

    sleep 0.02
  end
end
cleared = request(base, 'GET',
                  "/v1/surfaces/surface-#{clear_scrollback_id}/scrollback")
assert_equal(0, cleared.fetch('count'), 'CSI 3 J clears scrollback')
request(base, 'DELETE', "/v1/terminal-sessions/#{clear_scrollback_id}")
puts 'PASS csi-3J-clears-scrollback'
request(base, 'DELETE', "/v1/terminal-sessions/#{scrollback_id}")
puts 'PASS scrollback-retains-main-screen-lines'

snapshot = run_case(base, 'printable-and-newline', "printf 'hello\\r\\nworld'")
cells = cell_map(snapshot)
assert_equal('h', cells.fetch([0, 0]).fetch('text'), 'hello[0,0]')
assert_equal('w', cells.fetch([0, 1]).fetch('text'), 'world[0,1]')

snapshot = run_case(base, 'automatic-wrap', "printf 'ABCX'", width: 3, height: 2)
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([2, 0]).fetch('text'), 'wrap final column')
assert_equal('X', cells.fetch([0, 1]).fetch('text'), 'wrap next row')

snapshot = run_case(base, 'deferred-wrap-cursor-report',
                    "stty -icanon -echo min 0 time 5; printf 'ABC\\033[6n'; r=$(dd if=/dev/tty bs=1 count=6 2>/dev/null); printf '\\r'; printf '%s' \"$r\" | od -An -tx1 | tr -d ' \\n'",
                    width: 3)
assert_equal('1b5b313b3352', screen_text(snapshot),
             'deferred wrap keeps CPR on the final column')

snapshot = run_case(base, 'autowrap-disabled',
                    "printf '\\033[?7lABCX'", width: 3, height: 2)
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'no-wrap prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'no-wrap middle')
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'no-wrap overwrite')
raise 'autowrap-disabled advanced to next row' if cells.keys.any? { |x, y| y > 0 }

snapshot = run_case(base, 'newline-mode', "printf 'A\\033[20h\\nB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'newline mode prefix')
assert_equal('B', cells.fetch([0, 1]).fetch('text'), 'newline mode carriage return')

snapshot = run_case(base, 'cursor-addressing', "printf '\\033[2;3HX'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([2, 1]).fetch('text'), 'cursor address')

snapshot = run_case(base, 'style-reset', "printf '\\033[31mR\\033[0mN'")
cells = cell_map(snapshot)
assert_equal('#cd0000', cells.fetch([0, 0]).fetch('style').fetch('foreground'), 'red foreground')
raise 'style reset left N styled' if cells.fetch([1, 0]).key?('style')

snapshot = run_case(base, 'erase-to-end-of-line', "printf 'abc\\033[2D\\033[K'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'erase retained prefix')
raise 'erase left trailing cells' if cells.keys.any? { |x, y| y == 0 && x > 0 }

snapshot = run_case(base, 'erase-display-from-cursor', "printf 'abc\\033[2J'")
cells = cell_map(snapshot)
raise 'erase display left cells' if cells.keys.any? { |x, y| x >= 0 && y >= 0 }

snapshot = run_case(base, 'erase-display-from-cursor', "printf 'abc\\033[1D\\033[0J'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'erase display retained prefix')
assert_equal('b', cells.fetch([1, 0]).fetch('text'), 'erase display retained prior')
raise 'erase display left suffix' if cells.keys.any? { |x, y| y == 0 && x > 1 }

snapshot = run_case(base, 'erase-line-to-cursor', "printf 'abc\\033[1D\\033[1K'")
cells = cell_map(snapshot)
raise 'erase line left cells' if cells.keys.any? { |x, y| y == 0 }

snapshot = run_case(base, 'erase-pending-wrap',
                    "printf '1234\\033[KZ'", width: 4, height: 2)
assert_equal('123Z', row_text(snapshot),
             'erase consumes deferred wrap before printing')

snapshot = run_case(base, 'insert-character', "printf 'abcd\\033[2D\\033[2@X'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'insert prefix')
assert_equal('b', cells.fetch([1, 0]).fetch('text'), 'insert preserved')
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'insert target')

snapshot = run_case(base, 'insert-mode', "printf 'abcd\\033[2D\\033[4hX'", width: 6)
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'insert-mode prefix')
assert_equal('b', cells.fetch([1, 0]).fetch('text'), 'insert-mode preserved')
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'insert-mode target')
assert_equal('c', cells.fetch([3, 0]).fetch('text'), 'insert-mode shifted')
assert_equal('d', cells.fetch([4, 0]).fetch('text'), 'insert-mode tail')

snapshot = run_case(base, 'delete-character', "printf 'abcd\\033[2D\\033[2P'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'delete prefix')
assert_equal('b', cells.fetch([1, 0]).fetch('text'), 'delete preserved')
raise 'delete left trailing cells' if cells.keys.any? { |x, y| y == 0 && x > 1 }

snapshot = run_case(base, 'insert-line', "printf 'a\\nb\\033[2;1H\\033[LX'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'insert line prefix')
assert_equal('X', cells.fetch([0, 1]).fetch('text'), 'insert line target')
assert_equal('b', cells.fetch([0, 2]).fetch('text'), 'insert line shifted')

snapshot = run_case(base, 'delete-line', "printf 'a\\nb\\nc\\033[2;1H\\033[M'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'delete line prefix')
assert_equal('c', cells.fetch([0, 1]).fetch('text'), 'delete line shifted')

snapshot = run_case(base, 'cursor-line-movement', "printf 'a\\nb\\033[1A\\033[1CX'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'cursor line movement')

snapshot = run_case(base, 'relative-cursor-movement',
                    "printf 'A\\033[2aX\\033[2eY'", width: 8, height: 6)
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'HPR prefix')
assert_equal('X', cells.fetch([3, 0]).fetch('text'), 'HPR target')
assert_equal('Y', cells.fetch([4, 2]).fetch('text'), 'VPR target')

snapshot = run_case(base, 'cursor-forward-tab-stops',
                    "printf 'a\\033[2Ib'", width: 30)
cells = cell_map(snapshot)
assert_equal('b', cells.fetch([16, 0]).fetch('text'), 'CHT target')

snapshot = run_case(base, 'repeat-wraps-like-printing',
                    "printf 'x\\033[9b'", width: 6, height: 4)
cells = cell_map(snapshot)
assert_equal('x', cells.fetch([5, 0]).fetch('text'), 'REP fills first row')
assert_equal('x', cells.fetch([0, 1]).fetch('text'), 'REP wraps to next row')
assert_equal('x', cells.fetch([3, 1]).fetch('text'), 'REP wraps expected count')

snapshot = run_case(base, 'combining-character-cell',
                    "printf 'e\\314\\201X'")
cells = cell_map(snapshot)
assert_equal("e\u0301", cells.fetch([0, 0]).fetch('text'),
             'combining cell retains extension')
assert_equal('X', cells.fetch([1, 0]).fetch('text'), 'combining cursor advance')

snapshot = run_case(base, 'wide-character-overwrite',
                    "printf '\\360\\237\\230\\200\\033[1;2Hx'")
cells = cell_map(snapshot)
raise 'wide overwrite left original cell' if cells.key?([0, 0])
assert_equal('x', cells.fetch([1, 0]).fetch('text'),
             'wide overwrite target')

snapshot = run_case(base, 'delete-wide-character',
                    "printf 'a\\346\\274\\242b\\033[1;2H\\033[P|'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'DCH wide prefix')
assert_equal('|', cells.fetch([1, 0]).fetch('text'), 'DCH wide target')
assert_equal('b', cells.fetch([2, 0]).fetch('text'), 'DCH wide shift')

snapshot = run_case(base, 'insert-inside-wide-character',
                    "printf '\\346\\274\\242ab\\033[1;2H\\033[@|'")
cells = cell_map(snapshot)
raise 'ICH wide left a stale base cell' if cells.key?([0, 0])
assert_equal('|', cells.fetch([1, 0]).fetch('text'), 'ICH wide target')
assert_equal('a', cells.fetch([2, 0]).fetch('text'), 'ICH wide shift')
assert_equal('b', cells.fetch([3, 0]).fetch('text'), 'ICH wide tail')

snapshot = run_case(base, 'insert-line-preserves-column',
                    "printf 'a\\033[2;5HAB\\033[2;5H\\033[LX'", height: 6)
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 1]).fetch('text'), 'IL line target')
assert_equal('A', cells.fetch([4, 2]).fetch('text'), 'IL column preserved')

snapshot = run_case(base, 'delete-line-resets-column',
                    "printf 'one\\r\\ntwo\\r\\nthree\\033[2;3H\\033[MX'", height: 6)
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 1]).fetch('text'), 'DL line target')
assert_equal('h', cells.fetch([1, 1]).fetch('text'), 'DL shifted text')

snapshot = run_case(base, 'origin-mode-scroll-region',
                    "printf 'A\\033[2;4r\\033[?6h\\033[1;1HB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'origin mode prefix')
assert_equal('B', cells.fetch([0, 1]).fetch('text'), 'origin mode home')

snapshot = run_case(base, 'escape-next-line', "printf 'A\\033EB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'NEL prefix')
assert_equal('B', cells.fetch([0, 1]).fetch('text'), 'NEL target')

snapshot = run_case(base, 'reverse-index',
                    "printf 'A\\nB\\033[1;2r\\033[1;1H\\033MZ'")
cells = cell_map(snapshot)
assert_equal('Z', cells.fetch([0, 0]).fetch('text'), 'RI inserted row')
assert_equal('A', cells.fetch([0, 1]).fetch('text'), 'RI shifted row')

snapshot = run_case(base, 'c1-ind', "printf 'A\\204B'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'C1 IND prefix')
assert_equal('B', cells.fetch([1, 1]).fetch('text'), 'C1 IND target')

snapshot = run_case(base, 'c1-nel', "printf 'A\\205B'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'C1 NEL prefix')
assert_equal('B', cells.fetch([0, 1]).fetch('text'), 'C1 NEL target')

snapshot = run_case(base, 'c1-hts', "printf 'A\\210\\033[1;1H\\tB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'C1 HTS prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'C1 HTS target')

snapshot = run_case(base, 'c1-ri',
                    "printf 'A\\nB\\033[1;2r\\033[1;1H\\215Z'")
cells = cell_map(snapshot)
assert_equal('Z', cells.fetch([0, 0]).fetch('text'), 'C1 RI inserted row')
assert_equal('A', cells.fetch([0, 1]).fetch('text'), 'C1 RI shifted row')

snapshot = run_case(base, 'full-terminal-reset', "printf 'A\\033[31mB\\033cC'")
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([0, 0]).fetch('text'), 'RIS reset text')
raise 'RIS retained style' if cells.fetch([0, 0]).key?('style')

snapshot = run_case(base, 'dec-screen-alignment', "printf 'x\\033#8'", width: 4, height: 2)
cells = cell_map(snapshot)
8.times do |index|
  assert_equal('E', cells.fetch([index % 4, index / 4]).fetch('text'),
               'DECALN fill')
end

snapshot = run_case(base, 'tab-forward-and-backward', "printf 'A\\tB\\033[ZC'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'tab prefix')
assert_equal('C', cells.fetch([8, 0]).fetch('text'), 'back-tab target')

snapshot = run_case(base, 'custom-tab-stop', "printf 'A\\033H\\033[1;1H\\tB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'custom tab prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'custom tab stop')

snapshot = run_case(base, 'scroll', "printf 'a\\nb\\nc'", height: 2)
cells = cell_map(snapshot)
assert_equal('b', cells.fetch([0, 0]).fetch('text'), 'scroll first row')
assert_equal('c', cells.fetch([0, 1]).fetch('text'), 'scroll second row')

snapshot = run_case(base, 'scroll-up-down',
                    "printf '1\\r\\n2\\r\\n3\\r\\n4\\033[2S\\033[1T'",
                    height: 6)
cells = cell_map(snapshot)
assert_equal('3', cells.fetch([0, 1]).fetch('text'),
             'scroll-down restores scrolled row')
assert_equal('4', cells.fetch([0, 2]).fetch('text'),
             'scroll-down preserves row order')

snapshot = run_case(base, 'alternate-screen', "printf 'main\\033[?1049h\\033[Halt\\033[?1049l'")
cells = cell_map(snapshot)
assert_equal('m', cells.fetch([0, 0]).fetch('text'), 'alternate screen restore')

snapshot = run_case(base, 'alternate-screen-state-restore',
                    "printf '\\033[31mA\\033[?1049h\\033[32mB\\033[?1049lC'")
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([1, 0]).fetch('text'), '1049 cursor restore')
assert_equal('#cd0000', cells.fetch([1, 0]).fetch('style').fetch('foreground'),
             '1049 pen restore')

snapshot = run_case(base, 'legacy-alternate-screen',
                    "printf 'main\\033[?47h\\033[Halt\\033[?47l'")
cells = cell_map(snapshot)
assert_equal('m', cells.fetch([0, 0]).fetch('text'), 'legacy alternate restore')

snapshot = run_case(base, 'legacy-alternate-screen-persists',
                    "printf '\\033[?47hA\\033[?47l\\033[?47hB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'),
             'legacy alternate preserved first cell')
assert_equal('B', cells.fetch([1, 0]).fetch('text'),
             'legacy alternate preserved cursor state')

snapshot = run_case(base, '1047-alternate-screen-clears-on-leave',
                    "printf '\\033[?1047hA\\033[?1047l\\033[?1047hB'")
cells = cell_map(snapshot)
assert_equal('', cells.fetch([0, 0], { 'text' => '' }).fetch('text'),
             '1047 alternate was cleared on leave')
assert_equal('B', cells.fetch([1, 0]).fetch('text'),
             '1047 preserved the shared cursor position')

snapshot = run_case(base, 'per-screen-saved-cursor',
                    "printf '\\033[31mA\\0337\\033[?1049h\\033[32mB\\0337\\033[?1049l\\0338C'")
cells = cell_map(snapshot)
assert_equal('#cd0000', cells.fetch([1, 0]).fetch('style').fetch('foreground'),
             'main saved cursor survived alternate save')

snapshot = run_case(base, 'soft-reset', "printf '\\033[31mA\\033[!pB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'soft reset prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'soft reset target')
raise 'soft reset left style' if cells.fetch([1, 0]).key?('style')

snapshot = run_case(base, 'saved-cursor', "printf 'A\\0337\\033[2;3HX\\0338Y'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'saved cursor prefix')
assert_equal('Y', cells.fetch([1, 0]).fetch('text'), 'saved cursor restore')
assert_equal('X', cells.fetch([2, 1]).fetch('text'), 'saved cursor moved text')

snapshot = run_case(base, 'saved-cursor-pen',
                    "printf '\\033[31mA\\0337\\033[0mB\\0338C'")
cells = cell_map(snapshot)
assert_equal('#cd0000', cells.fetch([1, 0]).fetch('style').fetch('foreground'),
             'DECSC restored pen')

snapshot = run_case(base, 'dec-saved-cursor-mode',
                    "printf 'A\\033[?1048h\\033[2;3HX\\033[?1048lY'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'DEC save prefix')
assert_equal('Y', cells.fetch([1, 0]).fetch('text'), 'DEC save restore')
assert_equal('X', cells.fetch([2, 1]).fetch('text'), 'DEC save moved text')

snapshot = run_case(base, 'utf8-cell', "printf 'caf\\303\\251'")
cells = cell_map(snapshot)
assert_equal("é", cells.fetch([3, 0]).fetch('text'), 'UTF-8 cell')

snapshot = run_case(base, 'fragmented-utf8',
                    "printf '\\342'; sleep 0.1; printf '\\224'; sleep 0.1; printf '\\200X'")
cells = cell_map(snapshot)
assert_equal('─', cells.fetch([0, 0]).fetch('text'),
             'fragmented UTF-8 scalar')
assert_equal('X', cells.fetch([1, 0]).fetch('text'),
             'fragmented UTF-8 following cell')

snapshot = run_case(base, 'invalid-utf8-replacement',
                    "printf 'a\\377\\376b'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'),
             'invalid UTF-8 prefix')
assert_equal("\uFFFD", cells.fetch([1, 0]).fetch('text'),
             'invalid UTF-8 first byte replacement')
assert_equal("\uFFFD", cells.fetch([2, 0]).fetch('text'),
             'invalid UTF-8 second byte replacement')
assert_equal('b', cells.fetch([3, 0]).fetch('text'),
             'invalid UTF-8 following cell')

snapshot = run_case(base, 'wide-utf8-cell', "printf '\\360\\237\\230\\200X'")
cells = cell_map(snapshot)
assert_equal("😀", cells.fetch([0, 0]).fetch('text'), 'wide UTF-8 cell')
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'wide UTF-8 cursor advance')

snapshot = run_case(base, 'utf8-variation-selector',
                    "printf '\\360\\237\\230\\200\\357\\270\\217X'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([2, 0]).fetch('text'),
             'variation selector does not advance cursor')

snapshot = run_case(base, 'utf8-zero-width-joiner',
                    "printf '\\360\\237\\221\\251\\342\\200\\215\\360\\237\\222\\273X'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([4, 0]).fetch('text'),
             'zero-width joiner does not advance cursor')

snapshot = run_case(base, 'utf8-cell-preserves-full-extension',
                    "printf 'e\\314\\201\\314\\210\\314\\243\\315\\202\\357\\270\\217X'")
cells = cell_map(snapshot)
assert_equal("e\u0301\u0308\u0323\u0342\uFE0F",
             cells.fetch([0, 0]).fetch('text'),
             'surface cell retains all zero-width UTF-8 extensions')
assert_equal('X', cells.fetch([1, 0]).fetch('text'),
             'full extension sequence remains one cell')

snapshot = run_case(base, 'hibr-integration-defaults',
                    "printf '%s/%s' \"$DT_BIDI\" \"$HIBR_GFX\"")
assert_equal('on/kitty', row_text(snapshot).strip,
             'Hibr bidi and graphics defaults')

snapshot = run_case(base, 'hibr-integration-overrides',
                    "printf '%s/%s' \"$DT_BIDI\" \"$HIBR_GFX\"",
                    environment: { 'DT_BIDI' => 'off', 'HIBR_GFX' => 'sixel' })
assert_equal('off/sixel', row_text(snapshot).strip,
             'Hibr integration environment overrides')

snapshot = run_case(base, 'box-drawing-utf8', "printf '\\342\\224\\200\\342\\224\\202'")
cells = cell_map(snapshot)
assert_equal('─', cells.fetch([0, 0]).fetch('text'), 'box drawing horizontal')
assert_equal('│', cells.fetch([1, 0]).fetch('text'), 'box drawing vertical')

snapshot = run_case(base, 'dec-special-graphics', "printf '\\033(0lqk\\033(B'")
cells = cell_map(snapshot)
assert_equal('┌', cells.fetch([0, 0]).fetch('text'), 'DEC upper-left corner')
assert_equal('─', cells.fetch([1, 0]).fetch('text'), 'DEC horizontal line')
assert_equal('┐', cells.fetch([2, 0]).fetch('text'), 'DEC upper-right corner')

snapshot = run_case(base, 'g1-dec-special-with-shift',
                    "printf 'A\\033)0\\016lqk\\017'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'G1 graphics prefix')
assert_equal('┌', cells.fetch([1, 0]).fetch('text'), 'G1 graphics upper-left')
assert_equal('─', cells.fetch([2, 0]).fetch('text'), 'G1 graphics horizontal')
assert_equal('┐', cells.fetch([3, 0]).fetch('text'), 'G1 graphics upper-right')

snapshot = run_case(base, 'repeat-and-erase-character',
                    "printf 'A\\033[3bB\\033[2D\\033[1X'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'REP base')
assert_equal('A', cells.fetch([1, 0]).fetch('text'), 'REP first')
assert_equal('A', cells.fetch([2, 0]).fetch('text'), 'REP second')
assert_equal(false, cells.key?([3, 0]), 'ECH clears REP third')
assert_equal('B', cells.fetch([4, 0]).fetch('text'), 'ECH preserves following cell')

snapshot = run_case(base, 'truecolor-sgr', "printf '\\033[38;2;1;2;3mC'")
cells = cell_map(snapshot)
assert_equal('#010203', cells.fetch([0, 0]).fetch('style').fetch('foreground'), 'truecolor foreground')

snapshot = run_case(base, 'hibr-combined-truecolor-sgr',
                    "printf '\\033[0;1;38;2;203;213;224;48;2;16;24;32mX'")
cells = cell_map(snapshot)
style = cells.fetch([0, 0]).fetch('style')
assert_equal('#cbd5e0', style.fetch('foreground'),
             'Hibr combined truecolor foreground')
assert_equal('#101820', style.fetch('background'),
             'Hibr combined truecolor background')
assert_equal(true, style.fetch('bold'), 'Hibr combined truecolor bold')

snapshot = run_case(base, 'fragmented-hibr-combined-truecolor-sgr',
                    "printf '\\033[0;38;2;215;101;43'; " \
                    "sleep 0.02; " \
                    "printf ';48;2;216;102;43m▀\\033[0m'")
cells = cell_map(snapshot)
style = cells.fetch([0, 0]).fetch('style')
assert_equal('#d7652b', style.fetch('foreground'),
             'fragmented Hibr truecolor foreground')
assert_equal('#d8662b', style.fetch('background'),
             'fragmented Hibr truecolor background')
assert_equal('▀', cells.fetch([0, 0]).fetch('text'),
             'fragmented Hibr truecolor glyph')

snapshot = run_case(base, 'indexed-color-sgr',
                    "printf '\\033[38;5;196;48;5;232mC'")
cells = cell_map(snapshot)
assert_equal('#ff0000', cells.fetch([0, 0]).fetch('style').fetch('foreground'),
             'indexed foreground')
assert_equal('#080808', cells.fetch([0, 0]).fetch('style').fetch('background'),
             'indexed background')

snapshot = run_case(base, 'attribute-reset-sgr',
                    "printf '\\033[1;2;4;7mA\\033[22;24;27mB'")
cells = cell_map(snapshot)
raise 'attribute reset left B styled' if cells.fetch([1, 0]).key?('style')

snapshot = run_case(base, 'extended-sgr-attributes',
                    "printf '\\033[3mI\\033[5mB\\033[9mS\\033[23;25;29mN'")
cells = cell_map(snapshot)
assert_equal(true, cells.fetch([0, 0]).fetch('style').fetch('italic'),
             'italic set')
assert_equal(true, cells.fetch([1, 0]).fetch('style').fetch('blink'),
             'blink set')
assert_equal(true, cells.fetch([2, 0]).fetch('style').fetch('strike'),
             'strike set')
raise 'extended SGR reset left N styled' if cells.fetch([3, 0]).key?('style')

snapshot = run_case(base, 'concealed-sgr',
                    "printf 'a\\033[8mb\\033[28mc'")
cells = cell_map(snapshot)
assert_equal(true, cells.fetch([1, 0]).fetch('style').fetch('concealed'),
             'SGR conceal set')
raise 'SGR conceal reset left c styled' if cells.fetch([2, 0]).key?('style')

snapshot = run_case(base, 'synchronized-output',
                    "printf '\\033[?2026hA\\033[?2026lB'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'sync prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'sync release')

snapshot = run_case(base, 'decrqm-alternate-screen-mode',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?47h\\033[?47$p'; dd if=/dev/tty bs=1 count=9 2>/dev/null | od -An -tx1 | tr -d ' \\n'",
                    width: 40)
assert_equal('1b5b3f34373b312479', row_text(snapshot),
             'DECRQM alternate-screen mode')

snapshot = run_case(base, 'decrqm-x10-mouse-mode',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?9h\\033[?9$p'; dd if=/dev/tty bs=1 count=8 2>/dev/null | od -An -tx1 | tr -d ' \\n'",
                    width: 40)
assert_equal('1b5b3f393b312479', row_text(snapshot),
             'DECRQM X10 mouse mode')

# Issue #11: DECSDM is mode 80 and its polarity is load-bearing. DEC and
# current xterm agree that *set* is display mode and that the power-up
# default is reset -- sixel scrolling -- but xterm carried the inverse for
# years, so a client has to be able to ask. The default answer is 2.
snapshot = run_case(base, 'decrqm-sixel-display-mode-default',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?80$p'; dd if=/dev/tty bs=1 count=9 2>/dev/null | od -An -tx1 | tr -d ' \\n'",
                    width: 40)
assert_equal('1b5b3f38303b322479', row_text(snapshot),
             'DECRQM reports sixel scrolling, not display mode, by default')

snapshot = run_case(base, 'decrqm-sixel-display-mode-set',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?80h\\033[?80$p'; dd if=/dev/tty bs=1 count=9 2>/dev/null | od -An -tx1 | tr -d ' \\n'",
                    width: 40)
assert_equal('1b5b3f38303b312479', row_text(snapshot),
             'DECRQM reports DECSDM once it is set')

snapshot = run_case(base, 'decrqm-sixel-cursor-right',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?8452h\\033[?8452$p'; dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d ' \\n'",
                    width: 40)
assert_equal('1b5b3f383435323b312479', row_text(snapshot),
             'DECRQM reports 8452')

snapshot = run_case(base, 'background-color-erase',
                    "printf '\\033[44m\\033[2J\\033[0mx'")
cells = cell_map(snapshot)
assert_equal('#0000ee', cells.fetch([1, 0]).fetch('style').fetch('background'),
             'BCE erase background')

snapshot = run_case(base, 'da2-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[>c'; dd if=/dev/tty bs=1 count=10 2>/dev/null | od -An -tx1 | tr -d ' \\n'")
assert_equal('1b5b3e313b31303b3063', row_text(snapshot), 'DA2 reply')

snapshot = run_case(base, 'da1-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[c'; dd if=/dev/tty bs=1 count=14 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 80)
assert_equal('1b5b3f36323b343b32323b323863', row_text(snapshot), 'DA1 advertises sixel and rectangular editing')

snapshot = run_case(base, 'xtversion-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[>q'; dd if=/dev/tty bs=1 count=19 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 80)
assert_equal('1b503e7c626c69742d7465726d20302e311b5c', row_text(snapshot),
             'XTVERSION reply')

snapshot = run_case(base, 'window-size-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[18t'; dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d ' \\n'")
assert_equal('1b5b383b343b323074', row_text(snapshot), 'window size reply')

snapshot = run_case(base, 'decrqss-scroll-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033P$qr\\033\\\\'; dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 80)
assert_equal('1b50312472313b34721b5c', row_text(snapshot), 'DECRQSS reply')

# Issue #1: XTSMGRAPHICS is CSI ? Pi ; Pa ; Pv S. It used to fall through to
# SU and scroll the screen, which means the first thing a sixel-capable
# application did to Blit was destroy a line of output and then get no answer.
# Echo is off in every case here: the reply Blit writes to the PTY would
# otherwise come back through the line discipline and land on the grid, which
# would make the no-scroll assertion test the wrong thing.
xtsm_baseline = run_case(base, 'xtsmgraphics-baseline',
                         "stty -echo; printf 'AAAA1\\nBBBB2\\nCCCC3\\nDDDD4'")
assert_equal('AAAA1', row_text(xtsm_baseline), 'XTSMGRAPHICS fixture first row')
%w[?1;1;0S ?2;1;0S ?3;1;0S ?1;4;0S ?2;2;0T].each do |sequence|
  after = run_case(base, "xtsmgraphics-no-scroll-#{sequence.gsub(/[^a-zA-Z0-9]/, '')}",
                   "stty -echo; printf 'AAAA1\\nBBBB2\\nCCCC3\\nDDDD4'; " \
                   "printf '\\033[#{sequence}'")
  assert_equal(screen_text(xtsm_baseline), screen_text(after),
               "CSI #{sequence} leaves the grid alone")
end
puts 'PASS xtsmgraphics-does-not-scroll'

# ...and SU/SD themselves must still scroll, which is what the prefix guard
# could plausibly have broken.
scrolled = run_case(base, 'xtsmgraphics-su-still-scrolls',
                    "stty -echo; printf 'AAAA1\\nBBBB2\\nCCCC3\\nDDDD4'; printf '\\033[1S'")
assert_equal('BBBB2', row_text(scrolled), 'CSI 1 S still scrolls up')

# The reply reports the limits this build actually enforces: 256 colour
# registers and 4096x4096 sixel geometry, both named in src/blit_graphics.h so
# the answer cannot drift from the enforcement. The geometry was 512x512 until
# issue #8; a client that sizes its image to what it was told would have been
# held to a tenth of one output's width.
snapshot = run_case(base, 'xtsmgraphics-colour-registers',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?1;1;0S'; " \
                    'dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 80)
assert_equal('1b5b3f313b303b32353653', row_text(snapshot),
             'XTSMGRAPHICS colour-register reply')

snapshot = run_case(base, 'xtsmgraphics-sixel-geometry',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?2;1;0S'; " \
                    'dd if=/dev/tty bs=1 count=17 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 80)
assert_equal('1b5b3f323b303b343039363b3430393653', row_text(snapshot),
             'XTSMGRAPHICS sixel-geometry reply')

# An unrecognised item is Ps=1 (error) rather than silence; ReGIS is item 3
# and this build has no ReGIS decoder.
snapshot = run_case(base, 'xtsmgraphics-regis-unsupported',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?3;1;0S'; " \
                    'dd if=/dev/tty bs=1 count=10 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 80)
assert_equal('1b5b3f333b313b3053', row_text(snapshot),
             'XTSMGRAPHICS reports ReGIS as an error rather than not answering')
puts 'PASS xtsmgraphics-reply'

# Issue #6 / ADR 0059: CSI > u, CSI < u, CSI = ... u and CSI ? u are the Kitty
# keyboard protocol's flag sequences. They used to be cursor save/restore, so a
# client performing the standard handshake had its cursor dragged back to
# wherever it was when it enabled the protocol.
snapshot = run_case(base, 'kitty-keyboard-push-pop-cursor',
                    "stty -echo; printf '\\033[10;20H'; printf '\\033[>1u'; " \
                    "printf '\\033[2;2H'; printf '\\033[<u'", width: 40, height: 12)
assert_equal(1, snapshot.fetch('cursor').fetch('x'),
             'a keyboard-flag pop does not move the cursor')
assert_equal(1, snapshot.fetch('cursor').fetch('y'),
             'a keyboard-flag pop does not move the cursor vertically')

snapshot = run_case(base, 'kitty-keyboard-set-cursor',
                    "stty -echo; printf '\\033[5;7H'; printf '\\033[=9;3u'; " \
                    "printf '\\033[<3u'", width: 40, height: 12)
assert_equal(6, snapshot.fetch('cursor').fetch('x'),
             'a keyboard-flag set and a deep pop do not move the cursor')
assert_equal(4, snapshot.fetch('cursor').fetch('y'),
             'a keyboard-flag set and a deep pop do not move the cursor vertically')

# ...and none of them print their parameters into the grid.
assert_equal('', screen_text(snapshot).strip,
             'keyboard-flag sequences leave no text behind')
puts 'PASS kitty-keyboard-flags-do-not-move-the-cursor'

# The query reports current supported enhancements; unsupported bits are
# masked rather than advertised. 5 requests disambiguation plus unsupported
# alternate-key reporting, so only bit1 is active.
snapshot = run_case(base, 'kitty-keyboard-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[?u'; " \
                    'dd if=/dev/tty bs=1 count=5 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 80)
assert_equal('1b5b3f3075', row_text(snapshot), 'CSI ? u reports no flags in effect')

snapshot = run_case(base, 'kitty-keyboard-query-after-push',
                    "stty -icanon -echo min 0 time 5; printf '\\033[>5u'; printf '\\033[?u'; " \
                    'dd if=/dev/tty bs=1 count=5 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 80)
assert_equal('1b5b3f3175', row_text(snapshot),
             'CSI ? u reports what is honoured, not what was requested')
puts 'PASS kitty-keyboard-query'

# Issue #5. DECRQSS answered only DECSTBM, DECSCUSR and SGR and returned the
# invalid-request form for everything else, so a client that saves and restores
# terminal settings around a mode switch -- the one workflow DECRQSS exists for
# -- could read back nothing but the scroll region, the cursor shape and the
# pen. Each setting below is one Blit owns the state for; several only became
# answerable once #2 and #3 created that state.
# The reply is captured before it is printed, and DECLRMM is reset in between:
# the DECSLRM case leaves margins at 3..9, and printing the hex dump inside
# them wraps it at column 9 -- which is #3's autowrap working, and would read
# as this vector failing.
def decrqss(base, name, setup, query, count)
  run_case(base, name,
           "stty -icanon -echo min 0 time 5; #{setup}" \
           "printf '\\033P$q#{query}\\033\\\\'; " \
           "reply=$(dd if=/dev/tty bs=1 count=#{count} 2>/dev/null | " \
           "od -An -tx1 | tr -d ' \\n'); printf '\\033[?69l'; " \
           "printf '%s' \"$reply\"",
           width: 100, height: 4)
end

snapshot = decrqss(base, 'decrqss-decslrm',
                   "printf '\\033[?69h\\033[3;9s'; ", 's', 11)
assert_equal('1b50312472333b39731b5c', row_text(snapshot),
             'DECRQSS reports the left/right margins DECSLRM set')

snapshot = decrqss(base, 'decrqss-decsca',
                   "printf '\\033[1\"q'; ", '"q', 10)
assert_equal('1b503124723122711b5c', row_text(snapshot),
             'DECRQSS reports the character protection DECSCA set')

# DECSCL: Blit does not select a conformance level, there is one and it is
# fixed, so this reports the level DA1 reports. Reporting something DA1
# contradicts would be worse than refusing.
snapshot = decrqss(base, 'decrqss-decscl', '', '"p', 13)
assert_equal('1b5031247236323b3122701b5c', row_text(snapshot),
             'DECRQSS reports the conformance level DA1 reports')

snapshot = decrqss(base, 'decrqss-decslpp', '', 't', 9)
assert_equal('1b5031247234741b5c', row_text(snapshot),
             'DECRQSS reports the page height')

snapshot = decrqss(base, 'decrqss-decscpp', '', '$|', 13)
assert_equal('1b50312472313030247c1b5c', row_text(snapshot),
             'DECRQSS reports the page width')

snapshot = decrqss(base, 'decrqss-decsnls', '', '*|', 10)
assert_equal('1b50312472342a7c1b5c', row_text(snapshot),
             'DECRQSS reports the lines per screen')

snapshot = decrqss(base, 'decrqss-decsace',
                   "printf '\\033[2*x'; ", '*x', 10)
assert_equal('1b50312472322a781b5c', row_text(snapshot),
             'DECRQSS reports the extent DECSACE selected')

# A setting Blit genuinely does not implement still returns the
# invalid-request form, which is what makes the answers above mean something.
snapshot = decrqss(base, 'decrqss-unknown-setting', '', 'ZZ', 7)
assert_equal('1b503024721b5c', row_text(snapshot),
             'DECRQSS refuses a setting Blit does not own')

# Issue #4. XTGETTCAP used to answer the failure status with the names echoed
# back, for every request -- so TN, colors and RGB all reported *unsupported*
# on a server that advertises TERM=xterm-kitty and supports all three. A
# client that trusts the TERM string and then asks here was pushed onto its
# most conservative path.
snapshot = run_case(base, 'xtgettcap-terminal-name',
                    "stty -icanon -echo min 0 time 5; printf '\\033P+q544e\\033\\\\'; dd if=/dev/tty bs=1 count=34 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 110)
# ESC P 1 + r 544E = 787465726D2D6B69747479 ESC \   ->  TN=xterm-kitty
assert_equal('1b50312b72353434453d373837343635373236443244364236393734373437391b5c',
             row_text(snapshot), 'XTGETTCAP TN answers the terminal name')

snapshot = run_case(base, 'xtgettcap-unknown-name',
                    "stty -icanon -echo min 0 time 5; printf '\\033P+q7a7a7a\\033\\\\'; dd if=/dev/tty bs=1 count=13 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 110)
# ESC P 0 + r 7A7A7A ESC \  -- a name Blit does not own still fails, which is
# what makes the success above mean something.
assert_equal('1b50302b723741374137411b5c', row_text(snapshot),
             'XTGETTCAP refuses a capability it does not own')

snapshot = run_case(base, 'xtgettcap-boolean',
                    "stty -icanon -echo min 0 time 5; printf '\\033P+q5463\\033\\\\'; dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 110)
# ESC P 1 + r 5463 ESC \ -- a boolean capability is reported by its presence,
# with no value, which is how xterm answers one.
assert_equal('1b50312b72353436331b5c', row_text(snapshot),
             'XTGETTCAP reports a boolean capability by presence')

snapshot = run_case(base, 'xtgettcap-multiple-names',
                    "stty -icanon -echo min 0 time 5; printf '\\033P+q436f;524742\\033\\\\'; dd if=/dev/tty bs=1 count=42 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 110)
# Two DCS replies, one per name: Co=256 then RGB=8/8/8. One reply per name is
# what xterm and kitty both do, so a mixed request is answered rather than
# failing whole.
assert_equal('1b50312b72343336463d3332333533361b5c' \
             '1b50312b723532343734323d333832463338324633381b5c',
             row_text(snapshot), 'XTGETTCAP answers every name in a request')

def margin_rows(snapshot)
  grid = {}
  snapshot.fetch('cells').each do |cell|
    grid[[cell.fetch('x', 0), cell.fetch('y', 0)]] = cell['text'] || ' '
  end
  (0...snapshot.fetch('height')).map do |y|
    (0...snapshot.fetch('width')).map { |x| grid[[x, y]] || ' ' }.join
  end
end

# Issue #2: the VT500 rectangular area operations. All seven were parsed to
# nothing, and two were worse than ignored: the CSI parser took the final byte
# alone, so CSI 1;4$r (DECCARA) dispatched to the CSI 1;4r branch and
# redefined the scroll region instead. The parser records the intermediate
# byte now and dispatches on the pair.
def rect_flags(snapshot, flag)
  grid = {}
  snapshot.fetch('cells').each do |cell|
    grid[[cell.fetch('x', 0), cell.fetch('y', 0)]] = cell['style'] || {}
  end
  (0...snapshot.fetch('height')).map do |y|
    (0...snapshot.fetch('width')).map do |x|
      (grid[[x, y]] || {})[flag] ? 'B' : '.'
    end.join
  end
end

RECT_FILL = "printf 'AAAA1\\r\\nBBBB2\\r\\nCCCC3\\r\\nDDDD4'"

snapshot = run_case(base, 'decfra-fill-rectangle',
                    "#{RECT_FILL}; printf '\\033[88;1;1;3;4$x'", width: 10, height: 6)
assert_equal(['XXXX1     ', 'XXXX2     ', 'XXXX3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECFRA fills the rectangle with Pc')

# Pc outside the printable ranges is not a character to fill with, so the
# operation is ignored rather than filling with something invented.
snapshot = run_case(base, 'decfra-rejects-a-control-code',
                    "#{RECT_FILL}; printf '\\033[7;1;1;3;4$x'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'BBBB2     ', 'CCCC3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECFRA ignores a non-printable Pc')

snapshot = run_case(base, 'decera-erase-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;1;2;4$z'", width: 10, height: 6)
assert_equal(['    1     ', '    2     ', 'CCCC3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECERA erases the rectangle')

# DECSERA leaves the cells DECSCA protected. This is the only test of the
# protected attribute, which DECSCA (CSI 1"q) is the only way to set.
snapshot = run_case(base, 'decsera-leaves-protected-cells',
                    "printf 'ab\\033[1\"qcd\\033[0\"qef'; printf '\\033[1;1;1;6${'",
                    width: 10, height: 4)
assert_equal(['  cd      ', '          ', '          ', '          '],
             margin_rows(snapshot),
             'DECSERA erases only the cells DECSCA did not protect')

snapshot = run_case(base, 'deccra-copy-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;1;2;4;1;3;1$v'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'BBBB2     ', 'AAAA3     ', 'BBBB4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECCRA copies the rectangle')

# Overlapping source and destination. DEC requires the result to be as if the
# source were read whole before the destination was written, so a direct copy
# -- which would read cells it had already overwritten -- is wrong here.
snapshot = run_case(base, 'deccra-overlapping-copy',
                    "#{RECT_FILL}; printf '\\033[1;1;3;4;1;2;1$v'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'AAAA2     ', 'BBBB3     ', 'CCCC4     ',
              '          ', '          '],
             margin_rows(snapshot),
             'DECCRA copies through a buffer, so an overlap is not smeared')

snapshot = run_case(base, 'deccra-preserves-complete-cell',
                    "printf '\\033]8;;https://example.test/copy\\033\\\\\\033[4;38;2;10;20;30;48;2;40;50;60;58;2;70;80;90mÁ\\033]8;;\\033\\\\\\033[0m\\033[1;1;1;1;1;2;3;1$v'",
                    width: 10, height: 6)
cells = cell_map(snapshot)
assert_equal(cells.fetch([0,0]).reject { |k,_| %w[x y].include?(k) },
             cells.fetch([2,1]).reject { |k,_| %w[x y].include?(k) },
             'DECCRA preserves marks, underline colour, link and all style')

snapshot = run_case(base, 'deccra-preserves-wide-pair',
                    "printf '界\\033[2;1H界\\033[1;1;1;2;1;2;1;1$v'",
                    width: 10, height: 6)
cells = cell_map(snapshot)
assert_equal('界', cells.fetch([0,1]).fetch('text'), 'DECCRA wide owner survives replacing a wide pair')
assert_equal(2, cells.fetch([0,1]).fetch('width'), 'DECCRA retains wide allocation')
assert_equal(0, cells.fetch([1,1]).fetch('width'), 'DECCRA retains continuation')

snapshot = run_case(base, 'deccara-set-attributes',
                    "#{RECT_FILL}; printf '\\033[1;1;2;3;1$r'", width: 10, height: 6)
assert_equal(['BBB.......', 'BBB.......', '..........', '..........',
              '..........', '..........'],
             rect_flags(snapshot, 'bold'),
             'DECCARA sets the attribute inside the rectangle only')

snapshot = run_case(base, 'decrara-toggles-attributes',
                    "#{RECT_FILL}; printf '\\033[1;1;3;5;1$r\\033[1;2;1;4;1$t'",
                    width: 10, height: 6)
assert_equal(['B...B.....', 'BBBBB.....', 'BBBBB.....', '..........',
              '..........', '..........'],
             rect_flags(snapshot, 'bold'),
             'DECRARA toggles the attribute rather than setting it')

# An inverted column pair is ignored as a rectangle...
snapshot = run_case(base, 'deccara-ignores-an-inverted-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;4;3;2;1$r'", width: 6, height: 4)
assert_equal(['......', '......', '......', '......'],
             rect_flags(snapshot, 'bold'),
             'DECCARA ignores a rectangle whose left is past its right')

# ...and is the whole point of the stream extent, which runs from the first
# corner to the last in reading order and so has no column ordering to break.
snapshot = run_case(base, 'decsace-stream-extent',
                    "#{RECT_FILL}; printf '\\033[2*x\\033[1;4;3;2;1$r'",
                    width: 6, height: 4)
assert_equal(['...BBB', 'BBBBBB', 'BB....', '......'],
             rect_flags(snapshot, 'bold'),
             'DECSACE 2 makes the extent the stream between the corners')

# DECSED and DECSEL: the same collision arriving through the private prefix
# instead of the intermediate. CSI ?2J used to be executed as CSI 2J, erasing
# the cells DECSCA had protected.
snapshot = run_case(base, 'decsed-leaves-protected-cells',
                    "printf 'ab\\033[1\"qcd\\033[0\"qef'; printf '\\033[?2J'",
                    width: 10, height: 4)
assert_equal(['  cd      ', '          ', '          ', '          '],
             margin_rows(snapshot),
             'DECSED erases only the cells DECSCA did not protect')

snapshot = run_case(base, 'ed-erases-protected-cells',
                    "printf 'ab\\033[1\"qcd\\033[0\"qef'; printf '\\033[2J'",
                    width: 10, height: 4)
assert_equal(['          ', '          ', '          ', '          '],
             margin_rows(snapshot),
             'plain ED erases everything, protection included')

snapshot = run_case(base, 'decsel-leaves-protected-cells',
                    "printf 'ab\\033[1\"qcd\\033[0\"qef'; printf '\\033[1;1H\\033[?0K'",
                    width: 10, height: 4)
assert_equal(['  cd      ', '          ', '          ', '          '],
             margin_rows(snapshot),
             'DECSEL erases only the cells DECSCA did not protect')

# The collision this ticket measured: CSI 1;4$r was executed as CSI 1;4r, so
# an application changing attributes in a rectangle silently got its scroll
# region redefined. The difference is visible through what IL does afterwards:
# with the region at 1..4, an insert at line 5 is outside it and does nothing.
snapshot = run_case(base, 'decstbm-restricts-insert-line',
                    "printf 'L1\\r\\nL2\\r\\nL3\\r\\nL4\\r\\nL5'; " \
                    "printf '\\033[1;4r\\033[5;1H\\033[2L'", width: 10, height: 6)
assert_equal(['L1        ', 'L2        ', 'L3        ', 'L4        ',
              'L5        ', '          '],
             margin_rows(snapshot),
             'DECSTBM 1;4 leaves line 5 outside the region, so IL does nothing')

snapshot = run_case(base, 'deccara-is-not-decstbm',
                    "printf 'L1\\r\\nL2\\r\\nL3\\r\\nL4\\r\\nL5'; " \
                    "printf '\\033[1;4$r\\033[5;1H\\033[2L'", width: 10, height: 6)
assert_equal(['L1        ', 'L2        ', 'L3        ', 'L4        ',
              '          ', '          '],
             margin_rows(snapshot),
             'CSI 1;4$r is DECCARA and leaves the scroll region alone')


snapshot = run_case(base, 'decfra-fill-rectangle',
                    "#{RECT_FILL}; printf '\\033[88;1;1;3;4$x'", width: 10, height: 6)
assert_equal(['XXXX1     ', 'XXXX2     ', 'XXXX3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECFRA fills the rectangle with Pc')

# Pc outside the printable ranges is not a character to fill with, so the
# operation is ignored rather than filling with something invented.
snapshot = run_case(base, 'decfra-rejects-a-control-code',
                    "#{RECT_FILL}; printf '\\033[7;1;1;3;4$x'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'BBBB2     ', 'CCCC3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECFRA ignores a non-printable Pc')

snapshot = run_case(base, 'decera-erase-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;1;2;4$z'", width: 10, height: 6)
assert_equal(['    1     ', '    2     ', 'CCCC3     ', 'DDDD4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECERA erases the rectangle')

# DECSERA leaves the cells DECSCA protected. This is the only test of the
# protected attribute, which DECSCA (CSI 1"q) is the only way to set.
snapshot = run_case(base, 'decsera-leaves-protected-cells',
                    "printf 'ab\\033[1\"qcd\\033[0\"qef'; printf '\\033[1;1;1;6${'",
                    width: 10, height: 4)
assert_equal(['  cd      ', '          ', '          ', '          '],
             margin_rows(snapshot),
             'DECSERA erases only the cells DECSCA did not protect')

snapshot = run_case(base, 'deccra-copy-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;1;2;4;1;3;1$v'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'BBBB2     ', 'AAAA3     ', 'BBBB4     ',
              '          ', '          '],
             margin_rows(snapshot), 'DECCRA copies the rectangle')

# Overlapping source and destination. DEC requires the result to be as if the
# source were read whole before the destination was written, so a direct copy
# -- which would read cells it had already overwritten -- is wrong here.
snapshot = run_case(base, 'deccra-overlapping-copy',
                    "#{RECT_FILL}; printf '\\033[1;1;3;4;1;2;1$v'", width: 10, height: 6)
assert_equal(['AAAA1     ', 'AAAA2     ', 'BBBB3     ', 'CCCC4     ',
              '          ', '          '],
             margin_rows(snapshot),
             'DECCRA copies through a buffer, so an overlap is not smeared')

snapshot = run_case(base, 'deccara-set-attributes',
                    "#{RECT_FILL}; printf '\\033[1;1;2;3;1$r'", width: 10, height: 6)
assert_equal(['BBB.......', 'BBB.......', '..........', '..........',
              '..........', '..........'],
             rect_flags(snapshot, 'bold'),
             'DECCARA sets the attribute inside the rectangle only')

snapshot = run_case(base, 'decrara-toggles-attributes',
                    "#{RECT_FILL}; printf '\\033[1;1;3;5;1$r\\033[1;2;1;4;1$t'",
                    width: 10, height: 6)
assert_equal(['B...B.....', 'BBBBB.....', 'BBBBB.....', '..........',
              '..........', '..........'],
             rect_flags(snapshot, 'bold'),
             'DECRARA toggles the attribute rather than setting it')

# An inverted column pair is ignored as a rectangle...
snapshot = run_case(base, 'deccara-ignores-an-inverted-rectangle',
                    "#{RECT_FILL}; printf '\\033[1;4;3;2;1$r'", width: 6, height: 4)
assert_equal(['......', '......', '......', '......'],
             rect_flags(snapshot, 'bold'),
             'DECCARA ignores a rectangle whose left is past its right')

# ...and is the whole point of the stream extent, which runs from the first
# corner to the last in reading order and so has no column ordering to break.
snapshot = run_case(base, 'decsace-stream-extent',
                    "#{RECT_FILL}; printf '\\033[2*x\\033[1;4;3;2;1$r'",
                    width: 6, height: 4)
assert_equal(['...BBB', 'BBBBBB', 'BB....', '......'],
             rect_flags(snapshot, 'bold'),
             'DECSACE 2 makes the extent the stream between the corners')

# The collision this ticket measured: CSI 1;4$r was executed as CSI 1;4r, so
# an application changing attributes in a rectangle silently got its scroll
# region redefined. The difference is visible through what IL does afterwards:
# with the region at 1..4, an insert at line 5 is outside it and does nothing.
snapshot = run_case(base, 'decstbm-restricts-insert-line',
                    "printf 'L1\\r\\nL2\\r\\nL3\\r\\nL4\\r\\nL5'; " \
                    "printf '\\033[1;4r\\033[5;1H\\033[2L'", width: 10, height: 6)
assert_equal(['L1        ', 'L2        ', 'L3        ', 'L4        ',
              'L5        ', '          '],
             margin_rows(snapshot),
             'DECSTBM 1;4 leaves line 5 outside the region, so IL does nothing')

snapshot = run_case(base, 'deccara-is-not-decstbm',
                    "printf 'L1\\r\\nL2\\r\\nL3\\r\\nL4\\r\\nL5'; " \
                    "printf '\\033[1;4$r\\033[5;1H\\033[2L'", width: 10, height: 6)
assert_equal(['L1        ', 'L2        ', 'L3        ', 'L4        ',
              '          ', '          '],
             margin_rows(snapshot),
             'CSI 1;4$r is DECCARA and leaves the scroll region alone')

# Issue #3: the VT400/VT500 left/right margin model. DECLRMM (CSI ?69h) did
# nothing and the DECSLRM that follows it was swallowed by the SCOSC branch, so
# margin-aware editors got full-width behaviour.
#
# Every vector here sets margins at columns 3..6 of a twelve-column surface, so
# the two columns to the left and the six to the right are the evidence: an
# operation that respects the margins leaves them alone, and one that does not
# moves them.

FILL = "printf 'AAAA1\\r\\nBBBB2\\r\\nCCCC3\\r\\nDDDD4'"
MARGINS = "printf '\\033[?69h\\033[3;6s'"

snapshot = run_case(base, 'decslrm-insert-line',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;3H\\033[2L'",
                    width: 12, height: 5)
assert_equal(['AA          ', 'BB          ', 'CCAA1       ',
              'DDBB2       ', '  CC3       '],
             margin_rows(snapshot),
             'IL shifts only the columns inside the horizontal margins')

snapshot = run_case(base, 'decslrm-delete-line',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;3H\\033[1M'",
                    width: 12, height: 5)
assert_equal(['AABB2       ', 'BBCC3       ', 'CCDD4       ',
              'DD          ', '            '],
             margin_rows(snapshot),
             'DL shifts only the columns inside the horizontal margins')

snapshot = run_case(base, 'decslrm-insert-character',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;3H\\033[2@'",
                    width: 12, height: 5)
# AAAA1 occupies columns 1-5; the margins are 3-6. Inserting two at column 3
# shifts columns 3-6 right by two, so the A's at 3 and 4 land on 5 and 6 and
# the 1 at column 5 is pushed past the right margin and dropped -- which is
# the behaviour that distinguishes a margin-respecting ICH from a full-width
# one, where the 1 would have survived at column 7.
assert_equal(['AA  AA      ', 'BBBB2       ', 'CCCC3       ',
              'DDDD4       ', '            '],
             margin_rows(snapshot),
             'ICH shifts within the right margin and drops what passes it')

snapshot = run_case(base, 'decslrm-delete-character',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;3H\\033[2P'",
                    width: 12, height: 5)
assert_equal(['AA1         ', 'BBBB2       ', 'CCCC3       ',
              'DDDD4       ', '            '],
             margin_rows(snapshot),
             'DCH shifts within the right margin')

snapshot = run_case(base, 'decslrm-scroll-up',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;3H\\033[1S'",
                    width: 12, height: 5)
assert_equal(['AABB2       ', 'BBCC3       ', 'CCDD4       ',
              'DD          ', '            '],
             margin_rows(snapshot),
             'SU scrolls only the columns inside the horizontal margins')

# A cursor outside the horizontal margins makes the operation a no-op, which
# is the rule DEC states for the scroll margins. The ticket's own repro put the
# cursor at column 1 and expected a confined shift; this is the behaviour
# chosen instead, and it is the same rule ICH/DCH and the vertical margins
# already follow.
snapshot = run_case(base, 'decslrm-outside-margins-is-a-no-op',
                    "#{FILL}; #{MARGINS}; printf '\\033[1;1H\\033[2L'",
                    width: 12, height: 5)
assert_equal(['AAAA1       ', 'BBBB2       ', 'CCCC3       ',
              'DDDD4       ', '            '],
             margin_rows(snapshot),
             'IL with the cursor outside the margins changes nothing')

# Autowrap at the right margin lands on the left one, not on column zero.
snapshot = run_case(base, 'decslrm-autowrap',
                    "#{MARGINS}; printf '\\033[1;3Hxyzw1234'",
                    width: 12, height: 5)
assert_equal(['  xyzw      ', '  1234      ', '            ',
              '            ', '            '],
             margin_rows(snapshot),
             'autowrap at the right margin continues at the left margin')

# CR inside the margins returns to the left margin.
snapshot = run_case(base, 'decslrm-carriage-return',
                    "#{MARGINS}; printf '\\033[1;3Habcd\\rZ'",
                    width: 12, height: 5)
assert_equal(['  Zbcd      ', '            ', '            ',
              '            ', '            '],
             margin_rows(snapshot),
             'CR inside the margins returns to the left margin')

# And the switch the ticket names: CSI s is DECSLRM while DECLRMM is set and
# SCOSC while it is not. With DECLRMM reset, the same bytes save the cursor,
# so CSI u restores it and the text lands back where it was saved.
snapshot = run_case(base, 'scosc-without-declrmm',
                    "printf '\\033[2;5HX\\033[s\\033[4;1HY\\033[uZ'",
                    width: 12, height: 5)
assert_equal(['            ', '    XZ      ', '            ',
              'Y           ', '            '],
             margin_rows(snapshot),
             'CSI s is SCOSC while DECLRMM is reset')

# With DECLRMM set, the same CSI s sets margins instead of saving, so CSI u
# restores whatever SCOSC last saved -- the home position -- rather than the
# position before the CSI s.
snapshot = run_case(base, 'decslrm-not-scosc',
                    "printf '\\033[?69h\\033[2;5HX\\033[3;6s\\033[4;1HY\\033[uZ'",
                    width: 12, height: 5)
rows = margin_rows(snapshot)
assert_equal('    X       ', rows[1],
             'the X landed before CSI s was interpreted as DECSLRM')
assert_equal('Y           ', rows[3], 'the Y landed where it was placed')
assert_equal('Z           ', rows[0],
             'CSI s set margins rather than saving the cursor, so CSI u went home')

# Turning DECLRMM off restores the full width rather than remembering the
# columns, which is what makes CSI s unambiguous again.
snapshot = run_case(base, 'declrmm-reset-restores-full-width',
                    "#{FILL}; #{MARGINS}; printf '\\033[?69l\\033[1;1H\\033[2L'",
                    width: 12, height: 5)
assert_equal(['            ', '            ', 'AAAA1       ',
              'BBBB2       ', 'CCCC3       '],
             margin_rows(snapshot),
             'with DECLRMM reset, IL is full width again')

# bce is reported, so it has to be true: an erase applies the pen background.
# Reading the code said so; this is the assertion that it does.
snapshot = run_case(base, 'back-colour-erase',
                    "printf '\\033[48;5;4m\\033[2J'", width: 20, height: 4)
erased = cell_map(snapshot)
assert_equal('#0000ee', erased.fetch([3, 1]).fetch('style').fetch('background'),
             'an erase applies the pen background, which is what bce reports')

snapshot = run_case(base, 'modify-other-keys-prefix',
                    "printf 'a\\033[>4;2mb\\033[>4mc'")
cells = cell_map(snapshot)
raise 'modifyOtherKeys changed cell style' if cells.fetch([1, 0]).key?('style')

snapshot = run_case(base, 'cursor-push-pop-prefix',
                    "printf 'a\\033[>1ub\\033[1;1HX\\033[<uY'")
cells = cell_map(snapshot)
assert_equal('Y', cells.fetch([1, 0]).fetch('text'), 'cursor pop target')

snapshot = run_case(base, 'colon-sgr-and-underline-color',
                    "printf '\\033[38:5:196mA\\033[38:2::10:20:30mB\\033[58;5;196;4mC\\033[59mD\\033[0m'")
cells = cell_map(snapshot)
assert_equal('#ff0000', cells.fetch([0, 0]).fetch('style').fetch('foreground'),
             'colon indexed foreground')
assert_equal('#0a141e', cells.fetch([1, 0]).fetch('style').fetch('foreground'),
             'colon rgb foreground')
assert_equal('#ff0000', cells.fetch([2, 0]).fetch('style').fetch('underline_color'),
             'underline colour')

snapshot = run_case(base, 'fragmented-csi', "printf '\\033['; sleep 0.02; printf '2;3'; sleep 0.02; printf 'H'; printf 'F'")
cells = cell_map(snapshot)
assert_equal('F', cells.fetch([2, 1]).fetch('text'), 'fragmented CSI')

snapshot = run_case(base, 'c1-csi', "printf '\\2332;3HX'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([2, 1]).fetch('text'), '8-bit CSI')

snapshot = run_case(base, 'utf8-c1-csi', "printf '\\302\\2332;3HX'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([2, 1]).fetch('text'), 'UTF-8 encoded C1 CSI')

snapshot = run_case(base, 'c1-string-control',
                    "printf '\\2350;ignored title\\007X'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 0]).fetch('text'), '8-bit OSC isolation')

snapshot = run_case(base, 'utf8-c1-string-control',
                    "printf '\\302\\2350;utf8 title\\007X'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 0]).fetch('text'),
             'UTF-8 encoded OSC isolation')

snapshot = run_case(base, 'vt500-vertical-form-feed',
                    "printf 'A\\vB\\fC'", width: 8, height: 4)
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'vertical-tab origin')
assert_equal('B', cells.fetch([1, 1]).fetch('text'), 'vertical-tab line feed')
assert_equal('C', cells.fetch([2, 2]).fetch('text'), 'form-feed line feed')

snapshot = run_case(base, 'legacy-decid-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033Z'; dd if=/dev/tty bs=1 count=14 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 80)
assert_equal('1b5b3f36323b343b32323b323863', row_text(snapshot), 'legacy DECID advertises rectangular editing')

run_session_field_case(base, 'osc-title-bel', "printf '\\033]0;bel title\\007'",
                       'title', 'bel title')
run_session_field_case(base, 'osc-title-st',
               "printf '\\033]2;st title\\033'; printf '\\\\'",
               'title', 'st title')
run_session_field_case(base, 'osc-clipboard-set',
                       "printf '\\033]52;c;SGVsbG8=\\007'",
                       'clipboard', 'c;SGVsbG8=')
run_session_field_case(base, 'osc-notification-9',
                       "printf '\\033]9;hello notice\\007'",
                       'notification', 'hello notice')
run_session_field_case(base, 'osc-notification-777',
                       "printf '\\033]777;notify;title;body\\007'",
                       'notification', 'title;body')
snapshot = run_case(base, 'osc-hyperlink',
                    "printf '\\033]8;;https://example.test\\033\\\\linked\\033]8;;\\033\\\\'")
cells = cell_map(snapshot)
assert_equal('https://example.test', cells.fetch([0, 0]).fetch('link'),
             'OSC 8 hyperlink URI')

snapshot = run_case(base, 'osc-theme-colour-query',
                    "stty raw -echo min 0 time 5; printf '\\033]11;?\\033\\\\'; r=$(dd bs=1 count=25 2>/dev/null); printf 'got:%s' \"$(printf %s \"${r}\" | tr '\\033\\\\' EB)\"",
                    width: 70,
                    environment: { 'DT_INK' => '#cbd5e0',
                                    'DT_FACE' => '#101820' })
assert_equal('got:E]11;rgb:1010/1818/2020EB', row_text(snapshot),
             'OSC 11 theme background query')

snapshot = run_case(base, 'scroll-region', "printf 'A\\nB\\n\\033[2;3r\\033[2;1H1\\n2\\n3'", height: 4)
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'scroll region outside top')
assert_equal('2', cells.fetch([0, 1]).fetch('text'), 'scroll region first row')
assert_equal('3', cells.fetch([0, 2]).fetch('text'), 'scroll region second row')

snapshot = run_case(base, 'cursor-visibility', "printf '\\033[?25lX\\033[?25h'")
assert_equal(true, snapshot.fetch('cursor').fetch('visible'), 'cursor visibility restore')
snapshot = run_case(base, 'multi-private-mode',
                    "printf '\\033[?25l\\033[?1;25hX'")
assert_equal(true, snapshot.fetch('cursor').fetch('visible'),
             'multi-private-mode cursor visibility')
snapshot = run_case(base, 'cursor-hidden', "printf '\\033[?25lX'")
assert_equal(false, snapshot.fetch('cursor').fetch('visible'), 'cursor hidden')

snapshot = run_case(base, 'cursor-style', "printf '\\033[2 qX\\033[4 q'")
assert_equal(4, snapshot.fetch('cursor').fetch('style'), 'DECSCUSR steady underline')

# ADR 0059 replaced CSI ? u as cursor-restore with the Kitty keyboard query,
# so this vector now asserts the opposite of what it used to: the sequence
# must NOT restore the DECSC slot, and printing continues where it was.
snapshot = run_case(base, 'kitty-keyboard-query-does-not-restore',
                    "stty -echo; printf '\\033[3;4H\\0337\\033[1;1HAB\\033[?uCD'",
                    width: 8, height: 4)
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([2, 0]).fetch('text'),
             'CSI ? u does not restore the saved cursor')
assert_equal('D', cells.fetch([3, 0]).fetch('text'),
             'printing continues from where the query found the cursor')
# The snapshot is sparse, so an untouched cell is absent rather than a space.
assert_equal(false, cells.key?([3, 2]),
             'nothing is printed at the DECSC slot')
# ...and DECRC itself still restores it, which is the sequence that should.
snapshot = run_case(base, 'decsc-still-restores',
                    "stty -echo; printf '\\033[3;4H\\0337\\033[1;1HAB\\0338CD'",
                    width: 8, height: 4)
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([3, 2]).fetch('text'), 'ESC 8 restores the cursor')
puts 'PASS kitty-keyboard-query-is-not-cursor-restore'

# The reply is CSI ? 0 u, which carries a parameter, while the query carries
# none. With the tty echoing, a reply written to the PTY comes back as input,
# so a terminal that answered the parameterised form would answer the echo of
# its own answer without end. Echo is deliberately left ON here.
snapshot = run_case(base, 'kitty-keyboard-query-echo-loop',
                    "printf '\\033[?u'; sleep 0.3", width: 20, height: 4)
assert_equal(true, screen_text(snapshot).strip.length < 40,
             'an echoed keyboard-flag reply is not answered again')
puts 'PASS kitty-keyboard-query-does-not-loop'

kitty_data = Base64.strict_encode64("\xFF\x00\x00" * 4)
run_session_field_case(base, 'normal-session-has-no-graphics',
                       "printf 'ordinary terminal'", 'graphics', [])
kitty_format = "\\033_Ga=T,f=24,s=2,v=2,c=2,r=1,i=77,m=1,q=2;#{kitty_data[0, 4]}" \
               "\\033\\\\"
kitty_format += "\\033_Gq=2,i=77,m=0;#{kitty_data[4..]}\\033\\\\"
kitty_created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', "printf '%b' '#{kitty_format}'; sleep 0.2"],
  'surface' => { 'width' => 20, 'height' => 4 }
})
kitty_id = kitty_created.fetch('id')
kitty_state = Timeout.timeout(5) do
  loop do
    state = request(base, 'GET', "/v1/terminal-sessions/#{kitty_id}")
    break state unless %w[starting running].include?(state.fetch('state'))

    sleep 0.02
  end
end
kitty_image = kitty_state.fetch('graphics').fetch(0)
kitty_capabilities = kitty_state.fetch('terminal_capabilities')
assert_equal('xterm-kitty', kitty_capabilities.fetch('term'),
             'default terminal TERM advertisement')
assert_equal(8, kitty_capabilities.fetch('cell_pixels').fetch('width'),
             'terminal cell pixel width')
assert_equal(16, kitty_capabilities.fetch('cell_pixels').fetch('height'),
             'terminal cell pixel height')
assert_equal(%w[kitty sixel], kitty_capabilities.fetch('graphics'),
             'terminal graphics capabilities')
assert_equal('kitty', kitty_image.fetch('protocol'), 'Kitty protocol')
assert_equal(true, kitty_image.fetch('complete'), 'Kitty continuation completion')
assert_equal(true, kitty_image.fetch('visible'), 'Kitty transmit-and-display')
assert_equal(16, kitty_image.fetch('placement_width'), 'Kitty c cell conversion')
assert_equal(16, kitty_image.fetch('placement_height'), 'Kitty r cell conversion')
request(base, 'DELETE', "/v1/terminal-sessions/#{kitty_id}")
puts 'PASS kitty-image-transmit-placement'

# ADR 0055/0056 graphics decoding. A helper because each vector is the same
# shape: run one escape through a short-lived session and read back the
# graphics array the API publishes.
def graphics_case(base, name, escape, width: 20, height: 4)
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c', "printf '%b' '#{escape}'; sleep 0.3"],
    'surface' => { 'width' => width, 'height' => height }
  })
  id = created.fetch('id')
  state = Timeout.timeout(5) do
    loop do
      current = request(base, 'GET', "/v1/terminal-sessions/#{id}")
      break current unless %w[starting running].include?(current.fetch('state'))

      sleep 0.02
    end
  end
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  state.fetch('graphics')
end

# Issue #7: the sixel introducer is DCS P1;P2;P3 q and its parameters are
# optional. Every shape must reach the decoder; before this fix only the
# bare form did, and a parameterised introducer was dropped in silence.
sixel_body = '#0;2;100;0;0#0~~~~~~~~'
sixel_forms = {
  'bare' => "\\033Pq#{sixel_body}\\033\\\\",
  'p1-p2-p3' => "\\033P0;1;0q#{sixel_body}\\033\\\\",
  'p1-only' => "\\033P1q#{sixel_body}\\033\\\\",
  'empty-params' => "\\033P;;q#{sixel_body}\\033\\\\"
}
sixel_sizes = sixel_forms.map do |label, escape|
  images = graphics_case(base, "sixel-#{label}", escape)
  assert_equal(1, images.length, "sixel introducer #{label} produces one image")
  assert_equal('sixel', images.fetch(0).fetch('protocol'), "sixel protocol #{label}")
  [images.fetch(0).fetch('width'), images.fetch(0).fetch('height')]
end
assert_equal(1, sixel_sizes.uniq.length,
             'every sixel introducer form decodes to the same image')
puts 'PASS sixel-introducer-parameter-forms'

# A DCS that is not sixel must still be left alone rather than treated as a
# malformed one; DECRQSS shares the 'q' final byte behind a '$' intermediate.
assert_equal(0, graphics_case(base, 'decrqss-is-not-sixel',
                              "\\033P$qm\\033\\\\").length,
             'DECRQSS is not decoded as sixel')
puts 'PASS sixel-does-not-capture-other-dcs'

# Issue #12 / ADR 0056: f=100 is PNG, the protocol's only self-describing
# format. The same 2x2 image is transmitted as raw RGBA and as PNG; both
# must yield an image of identical dimensions.
rgba_pixels = [255, 0, 0, 255, 0, 255, 0, 255,
               0, 0, 255, 255, 255, 255, 255, 255].pack('C*')

def png_chunk(type, body)
  [body.bytesize].pack('N') + type + body +
    [Zlib.crc32(type + body)].pack('N')
end

png_raw = (0...2).map { |row| "\x00".b + rgba_pixels[row * 8, 8] }.join
png_bytes = "\x89PNG\r\n\x1a\n".b +
            png_chunk('IHDR'.b, [2, 2, 8, 6, 0, 0, 0].pack('NNC5')) +
            png_chunk('IDAT'.b, Zlib::Deflate.deflate(png_raw)) +
            png_chunk('IEND'.b, ''.b)

raw_images = graphics_case(base, 'kitty-raw-rgba',
  "\\033_Ga=T,f=32,s=2,v=2;#{Base64.strict_encode64(rgba_pixels)}\\033\\\\")
png_images = graphics_case(base, 'kitty-png',
  "\\033_Ga=T,f=100;#{Base64.strict_encode64(png_bytes)}\\033\\\\")
assert_equal(1, png_images.length, 'f=100 PNG produces one image')
assert_equal(raw_images.fetch(0).fetch('width'), png_images.fetch(0).fetch('width'),
             'PNG width matches the equivalent raw transmission')
assert_equal(raw_images.fetch(0).fetch('height'), png_images.fetch(0).fetch('height'),
             'PNG height matches the equivalent raw transmission')
assert_equal(32, png_images.fetch(0).fetch('format'), 'PNG decodes to RGBA')
assert_equal(true, png_images.fetch(0).fetch('complete'), 'PNG transmission completes')
puts 'PASS kitty-png-transmission'

# Issue #13 / ADR 0056: o=z is the protocol's answer to a bounded transport,
# which is what a PTY is. The same 2x2 image goes out three ways -- raw, zlib
# of the raw samples, and zlib of the PNG container -- and all three must
# produce the same picture. pixel_crc32 is what makes that assertable:
# dimensions and byte counts agree for images that differ.
zlib_images = graphics_case(base, 'kitty-zlib-rgba',
  "\\033_Ga=T,f=32,s=2,v=2,o=z;" \
  "#{Base64.strict_encode64(Zlib::Deflate.deflate(rgba_pixels))}\\033\\\\")
assert_equal(1, zlib_images.length, 'o=z with f=32 produces one image')
assert_equal(true, zlib_images.fetch(0).fetch('complete'), 'o=z completes')
assert_equal(raw_images.fetch(0).fetch('pixel_crc32'),
             zlib_images.fetch(0).fetch('pixel_crc32'),
             'o=z inflates to the same pixels as the raw transmission')

zlib_png_images = graphics_case(base, 'kitty-zlib-png',
  "\\033_Ga=T,f=100,o=z;" \
  "#{Base64.strict_encode64(Zlib::Deflate.deflate(png_bytes))}\\033\\\\")
assert_equal(1, zlib_png_images.length, 'o=z with f=100 produces one image')
assert_equal(raw_images.fetch(0).fetch('pixel_crc32'),
             zlib_png_images.fetch(0).fetch('pixel_crc32'),
             'a deflated PNG decodes to the same pixels')
puts 'PASS kitty-zlib-transmission'

# A stream that is not inflatable, and one that inflates to the wrong size for
# the declared s and v, must both be refused rather than half-accepted. The
# second is the one that matters: the size is an assertion, not a hint.
assert_equal(0, graphics_case(base, 'kitty-zlib-garbage',
  "\\033_Ga=T,f=32,s=2,v=2,o=z;#{Base64.strict_encode64('not a zlib stream')}" \
  "\\033\\\\").length,
             'an uninflatable o=z payload creates no image')
assert_equal(0, graphics_case(base, 'kitty-zlib-wrong-size',
  "\\033_Ga=T,f=32,s=4,v=4,o=z;" \
  "#{Base64.strict_encode64(Zlib::Deflate.deflate(rgba_pixels))}\\033\\\\").length,
             'an o=z payload inflating to the wrong size creates no image')
# An unknown compression must say so rather than be treated as raw bytes,
# which is the failure this issue was about.
assert_equal(0, graphics_case(base, 'kitty-unknown-compression',
  "\\033_Ga=T,f=32,s=2,v=2,o=q;#{Base64.strict_encode64(rgba_pixels)}\\033\\\\").length,
             'an unknown o value creates no image')
puts 'PASS kitty-zlib-rejects-malformed'

# A container that is not a decodable PNG must be refused, not half-accepted.
assert_equal(0, graphics_case(base, 'kitty-png-malformed',
  "\\033_Ga=T,f=100;#{Base64.strict_encode64('this is not a png')}\\033\\\\").length,
             'a malformed PNG container creates no image')
assert_equal(0, graphics_case(base, 'kitty-png-truncated',
  "\\033_Ga=T,f=100;#{Base64.strict_encode64(png_bytes[0, 20])}\\033\\\\").length,
             'a truncated PNG creates no image')
puts 'PASS kitty-png-rejects-malformed'

# Issue #15 / ADR 0060: placement ids, the source rectangle, and the delete
# targets. The pixel behaviour is in kitty_placement_gate.rb; this is the
# state and the refusals, which need no renderer.
def graphics_state_case(base, _name, escape, width: 40, height: 10)
  # Long argument strings are refused at session create, so the bytes go in a file.
  file = Tempfile.new('kitty-escape')
  file.write(escape)
  file.flush
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c', "printf '%b' \"$(cat #{file.path})\"; sleep 0.3"],
    'surface' => { 'width' => width, 'height' => height }
  })
  id = created.fetch('id')
  state = Timeout.timeout(5) do
    loop do
      current = request(base, 'GET', "/v1/terminal-sessions/#{id}")
      break current unless %w[starting running].include?(current.fetch('state'))

      sleep 0.02
    end
  end
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  state
end

gp = Base64.strict_encode64([1, 2, 3, 255].pack('C4') * 16)
gk = "f=32,s=4,v=4,q=2"
def gesc(keys, data = '') = "\\033_G#{keys};#{data}\\033\\\\"
def gcup(row, col) = "\\033[#{row};#{col}H"

# x and y are the source origin, not a destination offset.
images = graphics_state_case(base, 'kitty-source-rect',
  gcup(3, 4) + gesc("a=T,#{gk},i=1,x=1,y=2,w=2,h=1,C=1", gp)).fetch('graphics')
placement = images.fetch(0).fetch('placements').fetch(0)
assert_equal([24, 32], [placement.fetch('x'), placement.fetch('y')],
             'destination is the cursor cell, not x and y')
assert_equal({ 'x' => 1, 'y' => 2, 'width' => 2, 'height' => 1 },
             placement.fetch('source'), 'source rectangle is recorded')
assert_equal([2, 1], [placement.fetch('width'), placement.fetch('height')],
             'an unscaled cropped placement is the size of its crop')

# Width and height beyond the image clamp; an origin outside it is refused.
images = graphics_state_case(base, 'kitty-source-clamp',
  gesc("a=T,#{gk},i=1,x=3,y=3,w=9,h=9,C=1", gp)).fetch('graphics')
assert_equal({ 'x' => 3, 'y' => 3, 'width' => 1, 'height' => 1 },
             images.fetch(0).fetch('placements').fetch(0).fetch('source'),
             'oversized w and h clamp to the image')
state = graphics_state_case(base, 'kitty-source-outside',
  gesc("a=T,#{gk},i=1,x=4,y=0,w=1,h=1,C=1", gp))
assert_equal(0, state.fetch('graphics').length, 'a source origin outside the image shows nothing')
assert_equal('placement-bounds', state.fetch('graphics_diagnostics').fetch('last_error'),
             'a source origin outside the image is refused with a reason')
state = graphics_state_case(base, 'kitty-offset-outside-cell',
  gesc("a=T,#{gk},i=1,X=8,C=1", gp))
assert_equal(0, state.fetch('graphics').length, 'X must be smaller than the cell')

# Keys the protocol defines and this does not implement are refused.
%w[t=f t=t t=s I=3 U=1 P=1 Q=1 H=1 V=1].each do |key|
  state = graphics_state_case(base, "kitty-unsupported-#{key}",
    gesc("a=T,#{gk},i=1,#{key},C=1", gp))
  assert_equal(0, state.fetch('graphics').length, "unsupported key #{key} creates no image")
  assert_equal('unsupported-key', state.fetch('graphics_diagnostics').fetch('last_error'),
               "unsupported key #{key} is refused with a reason")
end
state = graphics_state_case(base, 'kitty-supported-t-d',
  gesc("a=T,#{gk},i=1,t=d,U=0,C=1", gp))
assert_equal(1, state.fetch('graphics').length, 't=d and U=0 are the supported defaults')

# ADR 0066 / issue #14. The vectors above refuse t=f with a payload that is
# not a path. This one hands it a real, readable path -- the regression guard
# for the only failure mode that matters, which is a future implementation
# that helpfully opens what it was handed. A terminal escape sequence is not
# an authenticated request: anything that can write to a PTY can emit one, so
# a path that arrives this way must never be opened whatever it names.
real_path = Base64.strict_encode64('/etc/hostname')
state = graphics_state_case(base, 'kitty-t-f-real-path',
  gesc("a=T,#{gk},i=1,t=f,C=1", real_path))
assert_equal(0, state.fetch('graphics').length,
             't=f naming a readable file creates no image')
assert_equal('unsupported-key', state.fetch('graphics_diagnostics').fetch('last_error'),
             't=f naming a readable file is refused with a reason')
%w[t=t t=s].each do |medium|
  state = graphics_state_case(base, "kitty-#{medium}-real-path",
    gesc("a=T,#{gk},i=1,#{medium},C=1", real_path))
  assert_equal(0, state.fetch('graphics').length,
               "#{medium} naming a readable file creates no image")
  assert_equal('unsupported-key',
               state.fetch('graphics_diagnostics').fetch('last_error'),
               "#{medium} naming a readable file is refused with a reason")
end

# The client-facing half: a refusal is answered rather than silent, so a
# client falls back at once instead of waiting for a reply that never came.
snapshot = run_case(base, 'kitty-t-f-reply',
                    "stty -icanon -echo min 0 time 5; " \
                    "printf '\\033_Ga=T,f=32,s=4,v=4,i=5,t=f;#{real_path}\\033\\\\'; " \
                    'dd if=/dev/tty bs=1 count=15 2>/dev/null | od -An -tx1 | tr -d \' \\n\'',
                    width: 60)
assert_equal('1b5f47693d353b45494e56414c1b5c', row_text(snapshot).strip,
             't=f is answered with EINVAL rather than dropped')

# Numbered placements: the same (image, placement) pair replaces, a different
# one adds, and an unnumbered display stays the single default placement.
state = graphics_state_case(base, 'kitty-placement-ids',
  gcup(1, 1) + gesc("a=T,#{gk},i=1,p=7,C=1", gp) +
  gcup(2, 1) + gesc('a=p,i=1,p=9,C=1') +
  gcup(3, 1) + gesc('a=p,i=1,p=7,C=1'))
placements = state.fetch('graphics').fetch(0).fetch('placements')
by_id = placements.to_h { |q| [q.fetch('id'), [q.fetch('x'), q.fetch('y')]] }
assert_equal({ 7 => [0, 32], 9 => [0, 16] }, by_id,
             'redisplaying p=7 moved it; p=9 is a separate placement')

# Delete targets. Each case places three 4x4 images at cells (0,0), (5,2) and
# (10,4) (z 0, 1, 2) and deletes by one target.
def delete_case(base, name, delete_keys, clear: false)
  escape = gcup(1, 1) + gesc("a=T,#{$gk},i=1,z=0,C=1", $gp) +
           gcup(3, 6) + gesc("a=T,#{$gk},i=2,z=1,C=1", $gp) +
           gcup(5, 11) + gesc("a=T,#{$gk},i=3,z=2,C=1", $gp) +
           gcup(3, 6) + gesc(delete_keys)
  graphics_state_case(base, name, escape).fetch('graphics')
end
$gk = gk
$gp = gp
def shown(images)
  images.select { |g| g.fetch('placements').any? }.map { |g| g.fetch('id') }.sort
end

assert_equal([1, 3], shown(delete_case(base, 'del-p', 'a=d,d=p,x=6,y=3')),
             'd=p removes the placements intersecting a 1-based cell')
assert_equal([1, 3], shown(delete_case(base, 'del-c', 'a=d,d=c')),
             'd=c removes the placements under the cursor')
assert_equal([1, 2], shown(delete_case(base, 'del-q', 'a=d,d=q,x=11,y=5,z=2')),
             'd=q matches the cell and the z-index together')
assert_equal([1, 2, 3], shown(delete_case(base, 'del-q-z', 'a=d,d=q,x=6,y=3,z=2')),
             'd=q does not remove a placement with another z-index')
assert_equal([1, 3], shown(delete_case(base, 'del-x', 'a=d,d=x,x=6')),
             'd=x removes the placements crossing a column')
assert_equal([1, 2], shown(delete_case(base, 'del-y', 'a=d,d=y,y=5')),
             'd=y removes the placements crossing a row')
assert_equal([1, 3], shown(delete_case(base, 'del-z', 'a=d,d=z,z=1')),
             'd=z removes the placements with that z-index')
assert_equal([], shown(delete_case(base, 'del-default', 'a=d')),
             'a=d with no target removes every visible placement')
kept = delete_case(base, 'del-a-keeps-data', 'a=d,d=a')
assert_equal([1, 2, 3], kept.map { |g| g.fetch('id') }.sort,
             'lowercase d=a keeps the image data')
assert_equal([], delete_case(base, 'del-A-frees-data', 'a=d,d=A'),
             'uppercase d=A frees the image data')
assert_equal([2, 3], delete_case(base, 'del-I-frees-one', 'a=d,d=I,i=1')
                       .map { |g| g.fetch('id') }.sort,
             'uppercase d=I frees one image')
assert_equal([1, 2, 3], delete_case(base, 'del-i-keeps-one', 'a=d,d=i,i=1')
                          .map { |g| g.fetch('id') }.sort,
             'lowercase d=i keeps the image data')
assert_equal([1, 2, 3], shown(delete_case(base, 'del-miss', 'a=d,d=p,x=30,y=9')),
             'a delete that matches nothing removes nothing')
%w[n N r R f F].each do |target|
  state = graphics_state_case(base, "del-unsupported-#{target}",
    gesc("a=T,#{gk},i=1,C=1", gp) + gesc("a=d,d=#{target}"))
  assert_equal(1, state.fetch('graphics').length, "d=#{target} deletes nothing")
  assert_equal('unsupported-delete-target',
               state.fetch('graphics_diagnostics').fetch('last_error'),
               "d=#{target} is refused with a reason")
end
puts 'PASS kitty-placement-keys'
# Issue #46. A conforming JSON encoder writes a control character as \uXXXX,
# and the argument parser decoded only \n, \r and \t -- keeping the escape's
# letter and dropping the backslash for everything else, so \u001b arrived as
# the literal text `u001b` and a session argument could not carry an escape
# sequence at all. Ruby's JSON.dump produces exactly that encoding for the ESC
# below, so this vector fails on the wire format rather than on a contrivance.
escaped_argument = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', "printf '%s' \"$1\"; sleep 0.3", 'sh',
                  "\u001b[31mR\u001b[0m"],
  'surface' => { 'width' => 20, 'height' => 4 }
})
escaped_state = Timeout.timeout(5) do
  loop do
    current = request(base, 'GET', "/v1/terminal-sessions/#{escaped_argument.fetch('id')}")
    break current unless %w[starting running].include?(current.fetch('state'))

    sleep 0.02
  end
end
escaped_cells = request(base, 'GET',
                        "/v1/surfaces/#{escaped_state.fetch('surface_id')}/cells")
                .fetch('cells')
first = escaped_cells.find { |c| c.fetch('x', 0) == 0 && c.fetch('y', 0) == 0 }
assert_equal('R', first && first['text'],
             'a \u001b in an argument reached the child as ESC, so the SGR was ' \
             'parsed and the cell holds the letter rather than the escape as text')
assert_equal('#cd0000', first && first.dig('style', 'foreground'),
             'the SGR carried by that escape was applied')
request(base, 'DELETE', "/v1/terminal-sessions/#{escaped_argument.fetch('id')}")
puts 'PASS argument-carries-escaped-control-character'
# Issue #48. A cell's colours belong under `style`, and the cell parser finds
# keys by search, so a cell carrying them at the top level was accepted with
# 200 and no style applied. The schema says additionalProperties is false for
# a cell, so that spelling was never valid -- it was silently ignored, which
# is how render_state_agreement_gate asserted blank cells were black on a
# screen that was already black for its whole life.
misplaced_session = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/cat', 'surface' => { 'width' => 10, 'height' => 3 }
})
misplaced_surface = misplaced_session.fetch('surface_id')
misplaced_revision = request(base, 'GET', "/v1/surfaces/#{misplaced_surface}/summary")
                     .fetch('revision')
misplaced_code, misplaced_body = request_with_status(
  base, 'PUT', "/v1/surfaces/#{misplaced_surface}/cells", {
    'base_revision' => misplaced_revision,
    'cells' => [{ 'x' => 0, 'y' => 0, 'text' => 'M',
                  'foreground' => '#ffffff', 'background' => '#0000ff' }]
  })
assert_equal(400, misplaced_code,
             'a cell carrying style properties at its top level is refused')
assert_equal('invalid_cell_property', misplaced_body && misplaced_body['code'],
             'the refusal names the shape of the mistake')
assert_equal(true,
             (misplaced_body && misplaced_body['detail']).to_s.include?('foreground'),
             'the refusal names the property it refused')
# The documented spelling still applies, which is the other half: a refusal
# that also broke the valid form would be worse than the silence.
nested_code, = request_with_status(
  base, 'PUT', "/v1/surfaces/#{misplaced_surface}/cells", {
    'base_revision' => misplaced_revision,
    'cells' => [{ 'x' => 0, 'y' => 0, 'text' => 'M',
                  'style' => { 'foreground' => '#ffffff',
                               'background' => '#0000ff' } }]
  })
assert_equal(true, [200, 204].include?(nested_code),
             "the documented nested form is accepted: got #{nested_code}")
nested_cell = request(base, 'GET', "/v1/surfaces/#{misplaced_surface}/cells")
              .fetch('cells').find { |c| c.fetch('x', 0) == 0 && c.fetch('y', 0) == 0 }
assert_equal('#0000ff', nested_cell && nested_cell.dig('style', 'background'),
             'and it applies the background it carries')
request(base, 'DELETE', "/v1/terminal-sessions/#{misplaced_session.fetch('id')}")
puts 'PASS cell-style-must-be-nested'



override_created = request(base, 'POST', '/v1/terminal-sessions', {
  'command' => '/bin/sh',
  'arguments' => ['-c', 'sleep 0.2'],
  'environment' => { 'TERM' => 'xterm-256color' },
  'surface' => { 'width' => 20, 'height' => 4 }
})
override_id = override_created.fetch('id')
override_state = request(base, 'GET', "/v1/terminal-sessions/#{override_id}")
assert_equal('xterm-256color',
             override_state.fetch('terminal_capabilities').fetch('term'),
             'explicit terminal TERM override')
request(base, 'DELETE', "/v1/terminal-sessions/#{override_id}")
puts 'PASS terminal-capability-term-override'

snapshot = run_case(base, 'c0-inside-csi', "printf '\\033[2\\n;5HX'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([4, 1]).fetch('text'), 'C0 control inside CSI')

snapshot = run_case(base, 'cancel-csi', "printf '\\033[31\\030X'")
assert_equal('X', cell_map(snapshot).fetch([0, 0]).fetch('text'), 'CAN cancels CSI')

snapshot = run_case(base, 'substitute-csi', "printf '\\033[31\\032X'")
assert_equal('X', cell_map(snapshot).fetch([0, 0]).fetch('text'), 'SUB cancels CSI')

snapshot = run_case(base, 'malformed-csi-recovery', "printf 'A\\033[12;X B'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'malformed CSI prefix')
assert_equal(' ', cells.fetch([1, 0]).fetch('text'), 'malformed CSI recovery space')
assert_equal('B', cells.fetch([2, 0]).fetch('text'), 'malformed CSI recovery')

snapshot = run_case(base, 'erase-pending-wrap-hibr',
                    "printf '0123456789012345678901234567890123456789\\033[KZ'",
                    width: 40, height: 6)
assert_equal('012345678901234567890123456789012345678Z',
             row_text(snapshot).rstrip,
             'EL clears the pending-wrap cell')

snapshot = run_case(base, 'repeat-wrap-hibr', "printf 'x\\033[9b'",
                    width: 6, height: 4)
assert_equal('xxxx', row_text(snapshot, 1).rstrip,
             'REP wraps like printable output')

snapshot = run_case(base, 'dec-special-graphics-hibr',
                    "printf '\\033(0lqqk\\033(B ok'")
assert_equal('┌──┐ ok', row_text(snapshot).rstrip,
             'DEC Special Graphics')

snapshot = run_case(base, 'dec-special-shift-hibr',
                    "printf '\\033)0\\016lqk\\017 plain'")
assert_equal('┌─┐ plain', row_text(snapshot).rstrip,
             'SO/SI DEC Special Graphics')

snapshot = run_case(base, 'cursor-forward-tab-hibr',
                    "printf 'a\\033[2Ib'")
cells = cell_map(snapshot)
assert_equal('a', cells.fetch([0, 0]).fetch('text'), 'CHT prefix')
assert_equal('b', cells.fetch([16, 0]).fetch('text'),
             'CHT advances by tab stops')

snapshot = run_case(base, 'insert-line-column-hibr',
                    "printf '\\033[2;5HAB\\033[2;5H\\033[LX'",
                    width: 20, height: 6)
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 1]).fetch('text'),
             'IL returns cursor to line home')
assert_equal('A', cells.fetch([4, 2]).fetch('text'),
             'IL preserves the column for shifted content')

snapshot = run_case(base, 'delete-line-column-hibr',
                    "printf 'one\\r\\ntwo\\r\\nthree\\033[2;3H\\033[MX'",
                    width: 20, height: 6)
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 1]).fetch('text'),
             'DL returns cursor to line home')
assert_equal('h', cells.fetch([1, 1]).fetch('text'),
             'DL preserves shifted content')

snapshot = run_case(base, 'wide-half-overwrite-hibr',
                    "printf '\\360\\237\\230\\200\\033[1;2Hx|'")
cells = cell_map(snapshot)
raise 'wide overwrite left original cell' if cells.key?([0, 0])
assert_equal('x', cells.fetch([1, 0]).fetch('text'),
             'wide overwrite target')
assert_equal('|', cells.fetch([2, 0]).fetch('text'),
             'wide overwrite tail')

snapshot = run_case(base, 'wide-half-insert-hibr',
                    "printf '\\360\\237\\230\\200ab\\033[1;2H\\033[@|'")
cells = cell_map(snapshot)
raise 'wide insert left original cell' if cells.key?([0, 0])
assert_equal('|', cells.fetch([1, 0]).fetch('text'),
             'wide insert target')
assert_equal('a', cells.fetch([2, 0]).fetch('text'),
             'wide insert shift')
assert_equal('b', cells.fetch([3, 0]).fetch('text'),
             'wide insert tail')

snapshot = run_case(base, 'string-controls-do-not-leak',
                    "printf 'A\\033]0;desktop title\\007B\\033P1;ignored\\033\\\\C'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'OSC prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'OSC recovery')
assert_equal('C', cells.fetch([2, 0]).fetch('text'), 'DCS recovery')

snapshot = run_case(base, 'charset-designator-does-not-leak',
                    "printf 'A\\033(0B'")
cells = cell_map(snapshot)
assert_equal('A', cells.fetch([0, 0]).fetch('text'), 'charset prefix')
assert_equal('B', cells.fetch([1, 0]).fetch('text'), 'charset recovery')

puts 'terminal conformance: PASS'
