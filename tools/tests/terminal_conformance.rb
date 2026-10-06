#!/usr/bin/env ruby
# frozen_string_literal: true

# Gate 2 black-box conformance runner. The server is started by the caller;
# this tool asserts only the canonical PTY-to-surface contract over HTTP.

require 'json'
require 'net/http'
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
  [response.code.to_i, response.body.empty? ? nil : JSON.parse(response.body)]
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

missing_surface_status, missing_surface_response = request_with_status(
  base, 'POST', '/v1/terminal-sessions', { 'command' => '/bin/true' })
assert_equal(400, missing_surface_status, 'missing surface status')
assert_equal('session_create_failed', missing_surface_response.fetch('code'),
             'missing surface code')
puts 'PASS schema-required-surface'

effects = request(base, 'GET', '/v1/effects')
assert_equal('none', effects.fetch('preset'), 'initial effect preset')
effect_update = request(base, 'PUT', '/v1/effects', {
  'revision' => effects.fetch('revision'),
  'enabled' => true,
  'preset' => 'scanlines',
  'intensity' => 40
})
assert_equal('scanlines', effect_update.fetch('preset'), 'effect preset update')
assert_equal(true, effect_update.fetch('enabled'), 'effect enable update')
stale_effect_status, stale_effect = request_with_status(base, 'PUT', '/v1/effects', {
  'revision' => effects.fetch('revision'),
  'enabled' => false,
  'preset' => 'none',
  'intensity' => 0
})
assert_equal(409, stale_effect_status, 'stale effect revision status')
assert_equal('revision_conflict', stale_effect.fetch('code'),
             'stale effect revision code')
request(base, 'PUT', '/v1/effects', {
  'revision' => effect_update.fetch('revision'),
  'enabled' => false,
  'preset' => 'none',
  'intensity' => 0
})
puts 'PASS bounded-effect-state-and-revision'

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
assert_equal('e', cells.fetch([0, 0]).fetch('text'), 'combining base cell')
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

snapshot = run_case(base, 'wide-utf8-cell', "printf '\\360\\237\\230\\200X'")
cells = cell_map(snapshot)
assert_equal("😀", cells.fetch([0, 0]).fetch('text'), 'wide UTF-8 cell')
assert_equal('X', cells.fetch([2, 0]).fetch('text'), 'wide UTF-8 cursor advance')

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

snapshot = run_case(base, 'background-color-erase',
                    "printf '\\033[44m\\033[2J\\033[0mx'")
cells = cell_map(snapshot)
assert_equal('#0000ee', cells.fetch([1, 0]).fetch('style').fetch('background'),
             'BCE erase background')

snapshot = run_case(base, 'da2-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033[>c'; dd if=/dev/tty bs=1 count=10 2>/dev/null | od -An -tx1 | tr -d ' \\n'")
assert_equal('1b5b3e313b31303b3063', row_text(snapshot), 'DA2 reply')

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

snapshot = run_case(base, 'xtgettcap-query',
                    "stty -icanon -echo min 0 time 5; printf '\\033P+q544e\\033\\\\'; dd if=/dev/tty bs=1 count=11 2>/dev/null | od -An -tx1 | tr -d ' \\n'", width: 80)
assert_equal('1b50302b72353434651b5c', row_text(snapshot), 'XTGETTCAP reply')

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

snapshot = run_case(base, 'c1-string-control',
                    "printf '\\2350;ignored title\\007X'")
cells = cell_map(snapshot)
assert_equal('X', cells.fetch([0, 0]).fetch('text'), '8-bit OSC isolation')

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

snapshot = run_case(base, 'kitty-cursor-restore',
                    "printf '\\033[3;4H\\0337\\033[1;1HAB\\033[?uCD'",
                    width: 8, height: 4)
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([3, 2]).fetch('text'), 'kitty restore prefix')
assert_equal('D', cells.fetch([4, 2]).fetch('text'), 'kitty restore target')

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
