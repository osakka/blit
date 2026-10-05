#!/usr/bin/env ruby
# frozen_string_literal: true

# Gate 2 black-box conformance runner. The server is started by the caller;
# this tool asserts only the canonical PTY-to-surface contract over HTTP.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

base = URI(ENV.fetch('BLIT_TEST_SERVER', 'http://127.0.0.1:19195'))

def request(base, method, path, body = nil)
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get,
    'POST' => Net::HTTP::Post,
    'DELETE' => Net::HTTP::Delete
  }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 5) do |http|
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless response.code.start_with?('2')
  return nil if response.code == '204'

  JSON.parse(response.body)
end

def cell_map(snapshot)
  snapshot.fetch('cells').each_with_object({}) do |cell, result|
    result[[cell.fetch('x'), cell.fetch('y')]] = cell
  end
end

def assert_equal(expected, actual, detail)
  return if expected == actual

  raise "#{detail}: expected #{expected.inspect}, got #{actual.inspect}"
end

def run_case(base, name, command, width: 20, height: 4)
  created = request(base, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    # Keep the PTY open long enough for the gateway's nonblocking drain loop
    # to observe the command's final bytes before lifecycle reaping closes it.
    'arguments' => ['-c', "#{command}; sleep 0.1"],
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

def run_title_case(base, name, command, expected)
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
  assert_equal(expected, state.fetch('title'), name)
  request(base, 'DELETE', "/v1/terminal-sessions/#{id}")
  puts "PASS #{name}"
rescue StandardError => error
  warn "FAIL #{name}: #{error.message}"
  raise
end

snapshot = run_case(base, 'printable-and-newline', "printf 'hello\\r\\nworld'")
cells = cell_map(snapshot)
assert_equal('h', cells.fetch([0, 0]).fetch('text'), 'hello[0,0]')
assert_equal('w', cells.fetch([0, 1]).fetch('text'), 'world[0,1]')

snapshot = run_case(base, 'automatic-wrap', "printf 'ABCX'", width: 3, height: 2)
cells = cell_map(snapshot)
assert_equal('C', cells.fetch([2, 0]).fetch('text'), 'wrap final column')
assert_equal('X', cells.fetch([0, 1]).fetch('text'), 'wrap next row')

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

snapshot = run_case(base, 'legacy-alternate-screen',
                    "printf 'main\\033[?47h\\033[Halt\\033[?47l'")
cells = cell_map(snapshot)
assert_equal('m', cells.fetch([0, 0]).fetch('text'), 'legacy alternate restore')

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

run_title_case(base, 'osc-title-bel', "printf '\\033]0;bel title\\007'",
               'bel title')
run_title_case(base, 'osc-title-st',
               "printf '\\033]2;st title\\033'; printf '\\\\'",
               'st title')

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
