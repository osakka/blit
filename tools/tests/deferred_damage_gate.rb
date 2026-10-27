#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #72. A deferred cell batch must not lose its damage to a frame that
# something else scheduled.
#
# ADR 0058's deferred presentation exists so an owner can write a whole screen
# across many requests and have it appear at once. `frame_open` withholds the
# surface from `render_work_remains`, so the batch does not schedule a frame of
# its own -- but the *plan* was built from every used surface regardless, so any
# frame scheduled for another reason drew the half-written surface and then
# cleared its damage. The chunks written before that frame were never repainted
# again, and on the live host that is a screensaver leaving a solid trail of
# itself: 8407 cells painted that the cell snapshot said were blank.
#
# The assertion is the one that matters and the one the API can answer: what
# the cells say and what the panel shows have to agree.
#
# **This gate has no teeth, and that is worth knowing before trusting it.** It
# passes against the pre-fix binary too. Four reproductions were built for #72
# -- this one, a 12x8 host probe, one at the screensaver's exact 240x135 with a
# 24x14 sprite, and one writing a cell per request as the live trace showed the
# owner actually does -- and not one of them failed before the fix and passed
# after. The development container has no GPU, so every local run uses SDL's
# software renderer, and the failure has only ever been observed on the host's
# opengles2 path. What carries the fix is the live measurement: 8407 stale
# cells growing at 96 a second before, a flat 5-9 after, where those few are
# the race between reading the cells and reading the screenshot.
#
# So this guards the invariant from here on rather than proving it, and a
# regression test with teeth needs a gate host with a GPU. That is recorded on
# the issue rather than left as a gap in the suite's reputation.
#
# `motion_trail_gate.rb` asserts the same property and also passes either way:
# it writes immediate, and its erase-plus-draw fits in a single chunk.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19572'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
PANEL_W = 1024
PANEL_H = 768
COLUMNS = 128
ROWS = 48
SPRITE_W = 24
SPRITE_H = 14
CHUNK = 40
BLOCK = [0x2588].pack("U")

abort("deferred damage gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "deferred-#{rand(1 << 30)}" if %w[POST PUT].include?(method)
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("deferred damage gate: #{method} #{path} -> #{response.code} #{response.body.to_s[0, 300]}")
  end
  JSON.parse(response.body) rescue {}
end

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                 },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end
Timeout.timeout(20) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

inset = json('GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')

# The surface under test: a full-width owner-authored desktop.
subject = json('POST', '/v1/terminal-sessions',
               'command' => '/bin/cat', 'display_id' => 'display-1',
               'surface' => { 'width' => COLUMNS, 'height' => ROWS })
surface = subject.fetch('surface_id')

# The interference: a second session whose output keeps scheduling frames while
# the batch above is open. Without it the deferred surface is the only thing
# that could schedule a frame, and `frame_open` already stops it doing that --
# which is exactly why this bug never showed in a gate before.
noise = json('POST', '/v1/terminal-sessions',
             'command' => '/bin/sh',
             'arguments' => ['-c', 'while :; do printf "."; sleep 0.05; done'],
             'display_id' => 'display-1',
             'surface' => { 'width' => 20, 'height' => 3 })

# Writing one owner frame as a deferred batch, with a *frame scheduled from
# somewhere else* landing in the middle of it. That is the condition, and it is
# driven here rather than raced for: injected pointer motion marks the renderer
# dirty, which is what the live host's own pointer does continuously.
def write_frame(surface, cells, interrupt: true)
  slices = cells.each_slice(CHUNK).to_a
  slices.each_with_index do |slice, index|
    last = index == slices.length - 1
    revision = json('GET', "/v1/surfaces/#{surface}/summary").fetch('revision')
    response = request('PUT', "/v1/surfaces/#{surface}/cells",
                       'base_revision' => revision,
                       'presentation' => last ? 'commit' : 'defer',
                       'cells' => slice)
    unless %w[200 204].include?(response.code)
      abort("deferred damage gate: write -> #{response.code} #{response.body.to_s[0, 200]}")
    end
    next unless interrupt && !last && (index % 8).zero?

    request('POST', '/v1/input/inject',
            'kind' => 'pointer_motion',
            'x' => 200 + (index % 17), 'y' => 200 + (index % 13))
    sleep 0.05
  end
end

# A whole screen, with the sprite drawn into it and every other cell a space.
# This is what the live host's owner does -- its surface reports all 32,400 of
# its cells as carrying a codepoint -- and it is what makes a batch long enough
# for a frame scheduled elsewhere to land in the middle of it.
def screen(x, y)
  cells = []
  (0...ROWS).each do |cy|
    (0...COLUMNS).each do |cx|
      inside = cx >= x && cx < x + SPRITE_W && cy >= y && cy < y + SPRITE_H
      cells << { 'x' => cx, 'y' => cy, 'text' => inside ? BLOCK : ' ',
                 'style' => { 'foreground' => '#d8cfb8',
                              'background' => '#000000' } }
    end
  end
  cells
end

def lit_cells(bmp, inset)
  lit = []
  (0...ROWS).each do |cy|
    (0...COLUMNS).each do |cx|
      found = false
      [4, 10].each do |dy|
        [2, 5].each do |dx|
          x = inset + cx * CELL_W + dx
          y = inset + cy * CELL_H + dy
          if gate_pixel(bmp, x, y).sum > 0
            found = true
            break
          end
        end
        break if found
      end
      lit << [cx, cy] if found
    end
  end
  lit
end

failures = []
position = [0, 0]
write_frame(surface, screen(0, 0))
sleep 0.8
6.times do
  position = [position[0] + 4, position[1] + 2]
  write_frame(surface, screen(*position))
  sleep 0.3
end
sleep 1.5

response = request('GET', '/v1/diagnostics/screenshots/display-1?cached=1')
abort("deferred damage gate: screenshot -> #{response.code}") unless response.code == '200'
lit = lit_cells(response.body, inset)
cells = json('GET', "/v1/surfaces/#{surface}/cells")
ink = cells.fetch('cells').select { |c| (c['text'] || ' ') != ' ' }
           .map { |c| [c['x'], c['y']] }

# The second session's own surface is stacked below and is not part of this
# comparison, so only the rows the subject occupies are judged.
subject_rows = (0...ROWS).to_a
cursor = cells.fetch('cursor', {})
cursor_cell = cursor['visible'] ? [cursor['x'], cursor['y']] : nil
stale = (lit.select { |cx, cy| subject_rows.include?(cy) } - ink) -
        [cursor_cell]
missing = ink - lit

puts "cell data: #{ink.length} cells carry ink; panel paints #{lit.length}"
if stale.any?
  xs = stale.map(&:first)
  ys = stale.map(&:last)
  puts "stale box x #{xs.min}..#{xs.max} y #{ys.min}..#{ys.max}"
end
failures << "#{stale.length} cells are painted that the cell data says are " \
            "blank, e.g. #{stale[0, 6].inspect}" unless stale.empty?
failures << "#{missing.length} cells carry ink that the panel is not " \
            "painting, e.g. #{missing[0, 6].inspect}" unless missing.empty?

[subject, noise].each do |created|
  request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
end

if failures.empty?
  puts "DEFERRED_DAMAGE_PASS cells=#{ink.length} painted=#{lit.length} " \
       'stale=0 missing=0'
else
  failures.each { |f| warn "deferred damage gate: FAIL #{f}" }
  exit 1
end
