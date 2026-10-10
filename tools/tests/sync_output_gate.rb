#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #33. Synchronized output (DEC 2026) must hold the surface's last
# presented frame, and must hold it against both failure directions:
#
#   - it must not go black, which is what happens when dropping the
#     synchronized surface from the frame moves the renderer's target count
#     and turns the next pass into a full redraw; and
#   - it must not show the batch's own writes, which is what a fix that
#     simply draws the current cells would do, passing a
#     "the screen is not black" check while removing the feature.
#
# Both are asserted here, in both directions, because only the pair
# distinguishes a held frame from either failure.
#
# Issue #45 rides along here, because a held frame is the one case where a
# capture's own honesty is checkable: the pixels correspond to the state
# before the batch, so the identity the capture reports must name that older
# revision and not the live one. Both request forms are exercised, because
# the non-cached form used to force a rebuild of the output it was inspecting
# and so showed the batch the renderer was withholding.
#
# docs/terminal-engine.md states the contract this gate measures.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19243'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
ROWS = 10
COLUMNS = 40
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)

abort("sync-output gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "sync-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def capture(form = :cached)
  path = '/v1/diagnostics/screenshots/display-1'
  path += '?cached=1' if form == :cached
  response = request('GET', path)
  raise "screenshot failed: #{response.code}" unless response.code == '200'

  revisions = {}
  (response['X-Blit-Capture-Revisions'] || '').split(',').each do |pair|
    id, revision = pair.split('=')
    revisions[id] = Integer(revision, 10) if id && revision
  end
  { body: response.body, revisions: revisions,
    presented: response['X-Blit-Capture-Presented'],
    frame: response['X-Blit-Capture-Frame'] }
end

def screenshot(form = :cached)
  capture(form).fetch(:body)
end

# 24-bit BMP, bottom-up, rows padded to four bytes.
def pixel(bmp, x, y)
  stride = (FB_WIDTH * 3 + 3) & ~3
  offset = 54 + (FB_HEIGHT - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

# The lit pixels of the first four cells of a surface's first row, as a
# comparable signature. Position within the row matters, so the offsets are
# part of it: BBBB and CCCC light a different number of pixels AND different
# ones.
def row_signature(bmp, origin_x, origin_y)
  lit = []
  (0...CELL_H).each do |dy|
    (0...(CELL_W * 4)).each do |dx|
      value = pixel(bmp, origin_x + dx, origin_y + dy)
      lit << [dx, dy, value] if value != 0
    end
  end
  lit
end

def session(script)
  body = { 'command' => '/bin/sh', 'arguments' => ['-c', script],
           'display_id' => 'display-1',
           'surface' => { 'width' => COLUMNS, 'height' => ROWS } }
  response = request('POST', '/v1/terminal-sessions', body)
  unless response.code.start_with?('2')
    abort("sync-output gate: session create failed #{response.code} #{response.body}")
  end
  JSON.parse(response.body).fetch('id')
end

# Hide the cursor so cursor damage cannot be mistaken for held content, draw
# BBBB, then open a batch that overwrites it with CCCC, then release.
BATCH = "printf '\\033[?25lBBBB'; sleep 2.5; " \
        "printf '\\033[?2026h\\rCCCC'; sleep 4; " \
        "printf '\\033[?2026l'; sleep 60"
# A second session that keeps forcing frames on the same output.
DRIVER = "printf '\\033[?25lAAAA'; " \
         "while :; do sleep 0.25; printf '\\033[2;1Hx'; done"

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

inset = JSON.parse(request('GET', '/v1/diagnostics').body)
             .fetch('render').fetch('surface_inset_px')

failures = []

# A surface with no pane is drawn at the inset, and each further pane-less
# surface on the output is stacked below the previous one.
def run_case(name, driver_script, origin_y, failures, inset, form = :cached)
  driver = driver_script ? session(driver_script) : nil
  batch = session(BATCH)
  surface = nil
  sleep 1.5
  before = row_signature(screenshot(form), inset, origin_y)
  sleep 3.0
  held = capture(form)
  during = row_signature(held.fetch(:body), inset, origin_y)
  # #45. The capture must say it reached a screen, and must name the state its
  # pixels actually show -- which, mid-batch, is behind the live revision.
  if held.fetch(:presented) != 'true'
    failures << "#{name}/#{form}: the capture does not report being presented " \
                "(#{held.fetch(:presented).inspect})"
  end
  surface = JSON.parse(request('GET', "/v1/terminal-sessions/#{batch}").body)
                .fetch('surface_id')
  live = JSON.parse(request('GET', "/v1/surfaces/#{surface}/summary").body)
             .fetch('revision')
  reported = held.fetch(:revisions)[surface]
  if reported.nil?
    failures << "#{name}/#{form}: the capture reported no revision for #{surface}"
  elsif reported >= live
    failures << "#{name}/#{form}: the capture reports revision #{reported} " \
                "for #{surface}, which is not behind the live revision " \
                "#{live} even though that surface's frame is being held -- " \
                'so the identity names a state these pixels do not show'
  end
  sleep 3.5
  after = row_signature(screenshot(form), inset, origin_y)
  request('DELETE', "/v1/terminal-sessions/#{batch}")
  request('DELETE', "/v1/terminal-sessions/#{driver}") if driver

  if before.empty?
    failures << "#{name}: nothing was drawn before the batch at y=#{origin_y}; " \
                'the gate is reading the wrong pixels'
    return
  end
  if during.empty?
    failures << "#{name}: the surface went black during the batch " \
                "(#{before.length} lit pixels before, 0 during)"
  elsif during != before
    failures << "#{name}: the held frame changed during the batch " \
                "(#{before.length} lit before, #{during.length} during)"
  end
  if after.empty?
    failures << "#{name}: nothing is drawn after the batch was released"
  elsif after == before
    failures << "#{name}: the batch's writes never appeared after release"
  end
  if during == after && during != before
    failures << "#{name}: the batch's own writes were visible before release"
  end
end

# Two surfaces on one output: the driver keeps the output presenting while
# the other surface is synchronized. This is the case the renderer gets
# wrong, because the target count moves.
run_case('two-surfaces', DRIVER, inset + ROWS * CELL_H + inset, failures, inset)
# One surface on the output, which must keep behaving as it already did, and
# must behave the same whichever capture form is used to look at it (#45).
run_case('one-surface', nil, inset, failures, inset, :cached)
run_case('one-surface', nil, inset, failures, inset, :fresh)

unless failures.empty?
  failures.each { |f| warn "sync-output gate: #{f}" }
  exit 1
end

puts 'SYNC_OUTPUT_PASS held_last_frame=3_cases batch_writes_hidden=yes ' \
     'capture_presented=true capture_identity=behind_live_revision'
