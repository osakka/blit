#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0186: pane effects. One display split 1x2; pane 1 gets corners and
# scanlines through document.effects, pane 2 none. Each pane is painted one
# solid colour by its own session, so every expected pixel is computable
# from the shared rule (src/blit_effects.h), ported below.
#
#   * placement: pane 1's four corners are black, its inside is not, and
#     pane 2 is untouched -- including the corner pixel right below pane 1's
#   * exactness: arc and scanline pixels equal the shared formula (within 1
#     on the GPU, whose blend rounds in float)
#   * idempotence: a session redrawing the corner cells for two seconds
#     leaves every effect pixel exactly as it was -- nothing is darkened
#     twice, which is what a whole-output effect does under damage
#   * the GPU composes the frame itself with effects on (no CPU fallback)
#   * strict settings: an unknown effect, a parameter out of range and a
#     write to /v1/effects are refused; a radius beyond the pane is clamped
#   * persistence: the effects come back after a restart
#
# BLIT_GATE_BACKEND=native (CPU compositor) or gles.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19531'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384
PANE_H = H / 2
RADIUS = 40
SCANLINES = 50
A = [200, 120, 40].freeze
B = [40, 160, 220].freeze
TOLERANCE = GATE_BACKEND == 'gles' ? 1 : 0

abort("pane effects gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s if revision
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "PANE_EFFECTS_FAIL #{why}" unless ok
end

# The shared rule, ported from src/blit_effects.h and the compositors' blend.
def coverage(radius, x, y)
  outside = 0
  8.times do |sy|
    8.times do |sx|
      qx = 16 * x + 2 * sx + 1
      qy = 16 * y + 2 * sy + 1
      dx = 16 * radius - qx
      dy = 16 * radius - qy
      outside += 1 if dx.positive? && dy.positive? && dx * dx + dy * dy > 256 * radius * radius
    end
  end
  (outside * 255 + 32) / 64
end

def darken(rgb, alpha)
  rgb.map { |c| (c * (255 - alpha) + 127) / 255 }
end

# Pane 1's expected pixel at (x, y): scanlines, then corners.
def expected(x, y)
  rgb = A
  rgb = darken(rgb, SCANLINES * 255 / 100) if y.odd?
  cx = x < RADIUS ? x : (x >= W - RADIUS ? W - 1 - x : nil)
  cy = y < RADIUS ? y : (y >= PANE_H - RADIUS ? PANE_H - 1 - y : nil)
  rgb = darken(rgb, coverage(RADIUS, cx, cy)) if cx && cy && coverage(RADIUS, cx, cy).positive?
  rgb
end

def near?(actual, wanted)
  actual.zip(wanted).all? { |a, b| (a - b).abs <= TOLERANCE }
end

def screenshot
  sleep 0.4
  code, = request('GET', '/v1/version')
  raise 'server gone' unless code == 200
  uri = BASE + '/v1/diagnostics/screenshots/display-1'
  GateBmp.new(Net::HTTP.get(uri))
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x2', 'BLIT_AUTH_TOKEN' => '')
$server = nil
def start(env)
  $server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
end

def stop
  return unless $server
  Process.kill('TERM', $server) rescue nil
  Process.wait($server) rescue nil
  $server = nil
end
at_exit { stop }

def paint(pane, rgb, loop_corner: false)
  # Cursor hidden: its cell would sit at the pane's top-left corner.
  colour = "\\033[?25l\\033[48;2;#{rgb.join(';')}m"
  script = if loop_corner
             # Redraw the corner cells over and over: damage at both of the
             # pane's top corners, on even and odd pixel rows.
             "printf '#{colour}\\033[2J'; while :; do printf '#{colour}\\033[1;1H  \\033[1;79H  '; sleep 0.05; done"
           else
             "printf '#{colour}\\033[2J'; sleep 600"
           end
  code, session = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', arguments: ['-c', script], pane_id: pane })
  check(code == 201, "session for #{pane}: #{code} #{session}")
  session
end

def save_effects(effects)
  # A startup migration or the previous save may still be on its way to
  # disk; its revision is the one a write must name.
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document['effects'] = effects
  request('PUT', '/v1/settings', { document: document }, settings['revision'])
end

Dir.mktmpdir('blit-pane-effects') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  start(env)
  output = get('/v1/displays')['displays'].first.dig('state', 'connector')
  check(output.is_a?(String) && !output.empty?, 'display has no connector name')
  paint('pane-display-1-1', A, loop_corner: true)
  paint('pane-display-1-2', B)
  shot = screenshot
  check(shot.pixel(0, 0) == A && shot.pixel(1, 1) == A, "no effects yet, corner #{shot.pixel(0, 0)}")

  code, reply = save_effects(output => { '1' => { 'corners' => { 'radius_px' => RADIUS },
                                                  'scanlines' => { 'intensity' => SCANLINES } } })
  check(code == 200, "saving effects: #{code} #{reply}")
  first = screenshot

  # Placement.
  [[0, 0], [W - 1, 0], [0, PANE_H - 1], [W - 1, PANE_H - 1]].each do |x, y|
    check(first.pixel(x, y) == [0, 0, 0], "pane 1 corner #{x},#{y} is #{first.pixel(x, y)}")
  end
  check(first.pixel(RADIUS, RADIUS) == A, "pane 1 inside (even row) is #{first.pixel(RADIUS, RADIUS)}")
  check(first.pixel(0, PANE_H) == B && first.pixel(W - 1, H - 1) == B,
        "pane 2 was touched: #{first.pixel(0, PANE_H)} #{first.pixel(W - 1, H - 1)}")

  # Exactness: every pixel of pane 1's corner squares and two full rows.
  checked = 0
  points = []
  [0, W - RADIUS].each { |x0| [0, PANE_H - RADIUS].each { |y0| RADIUS.times { |dy| RADIUS.times { |dx| points << [x0 + dx, y0 + dy] } } } }
  [RADIUS + 1, RADIUS + 2].each { |y| W.times { |x| points << [x, y] } }
  points.each do |x, y|
    check(near?(first.pixel(x, y), expected(x, y)),
          "pixel #{x},#{y} is #{first.pixel(x, y)}, expected #{expected(x, y)}")
    checked += 1
  end

  # Idempotence under damage.
  sleep 2
  second = screenshot
  points.each do |x, y|
    check(second.pixel(x, y) == first.pixel(x, y),
          "pixel #{x},#{y} changed under damage: #{first.pixel(x, y)} -> #{second.pixel(x, y)}")
  end

  # The GPU composes effects itself.
  compositor = get('/v1/diagnostics').dig('render', 'outputs', 0, 'compositor')
  check(compositor['active'] == GATE_BACKEND.sub('native', 'cpu'),
        "compositor with effects on: #{compositor.inspect}")

  # The cost is reported, and light: the CPU's worst frame for a full
  # repaint of a 640x192 pane with both effects stays under half a refresh
  # (8.3 ms at 60 Hz). Alone it is about 3 ms of the thread's CPU time, but
  # a loaded build host inflates even CPU time (shared caches and cores):
  # 5.2-5.6 ms was seen in full suite runs. The bound is the requirement,
  # not that machine's quiet-hour figure.
  cost = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects')
  check(cost.is_a?(Hash) && cost.key?('cpu_last_ms') && cost.key?('gpu_quads'), "effects cost missing: #{cost}")
  check(GATE_BACKEND == 'gles' || (cost['cpu_max_ms'].positive? && cost['cpu_max_ms'] < 1000.0 / 60 / 2),
        "CPU effects cost #{cost}")

  # The API reports the effective set.
  effects = get('/v1/effects')
  panes = effects['panes'].to_h { |p| [p['pane_id'], p] }
  check(panes.dig('pane-display-1-1', 'effects') ==
        { 'corners' => { 'radius_px' => RADIUS }, 'scanlines' => { 'intensity' => SCANLINES } },
        "GET /v1/effects pane 1: #{panes['pane-display-1-1']}")
  check(panes.dig('pane-display-1-2', 'effects') == {}, 'pane 2 reports effects')
  check(panes.dig('pane-display-1-1', 'output') == output &&
        panes.dig('pane-display-1-1', 'pane_ordinal') == 1, 'pane key not reported')

  # Strict settings, a read-only endpoint, and clamping.
  code, reply = save_effects(output => { '1' => { 'glow' => { 'radius_px' => 3 } } })
  check(code == 400 && reply['code'] == 'invalid_settings_effects', "unknown effect -> #{code}")
  code, reply = save_effects(output => { '1' => { 'corners' => { 'radius_px' => 2000 } } })
  check(code == 400 && reply['code'] == 'invalid_settings_effects', "radius out of range -> #{code}")
  code, reply = save_effects(output => { '0' => { 'corners' => { 'radius_px' => 4 } } })
  check(code == 400, "pane ordinal 0 -> #{code}")
  code, reply = request('PUT', '/v1/effects', { anything: 1 })
  check(code == 405 && reply['code'] == 'effects_read_only', "PUT /v1/effects -> #{code}")
  code, = save_effects(output => { '1' => { 'corners' => { 'radius_px' => 1000 } } })
  check(code == 200, 'a large radius is refused instead of clamped')
  clamped = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
  check(clamped.dig('effects', 'corners', 'radius_px') == PANE_H / 2, "radius not clamped: #{clamped}")

  # Persistence.
  code, = save_effects(output => { '1' => { 'corners' => { 'radius_px' => RADIUS },
                                            'scanlines' => { 'intensity' => SCANLINES } } })
  check(code == 200, 'restoring effects')
  Timeout.timeout(5) { sleep 0.1 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  stop
  start(env)
  check(get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }['effects'] ==
        { 'corners' => { 'radius_px' => RADIUS }, 'scanlines' => { 'intensity' => SCANLINES } },
        'effects did not survive a restart')
  paint('pane-display-1-1', A)
  check(screenshot.pixel(0, 0) == [0, 0, 0], 'restarted pane 1 corner not black')

  puts "PANE_EFFECTS_PASS backend=#{GATE_BACKEND} pixels=#{checked} exact_within=#{TOLERANCE} " \
       'placement=pane damage=idempotent gpu=composes settings=strict clamp=half_side persisted=yes'
end
