#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0187: presentation effects (curvature, glow) and the effect switches.
# One display split 1x2; pane 1 gets the effects, pane 2 never does.
#
# On gles:
#   * warp: pane 1 is captured plain, then with curvature. Every warped pixel
#     is predicted from the plain capture with the shared warp (ported from
#     src/blit_effects.h) and the shader's linear sampling, wherever the 2x2
#     texels it samples are one colour; outside the tube it is black
#   * scanlines under curvature are drawn once, on the glass: over a flat
#     colour, every odd screen row of the pane is darkened by exactly the
#     shared rule and every even row is not -- no moire, no flattening
#   * glow: one white cell on black brightens its surroundings, leaves a far
#     pixel black and never crosses into pane 2
#   * isolation: pane 2 is byte-identical with and without pane 1's effects
#   * pointer: a click at a screen point reaches the cell the warp shows
#     there; a click in the black margin reaches the nearest edge cell
#   * the plain path: with no presentation effects nothing is drawn by the
#     presentation pass
# On both:
#   * switches: effects.enabled false, or a pane's own enabled false, draws
#     exactly what no effects draws, and the API says so; values are kept
#   * strict switches: a switch that is not a boolean is refused
# On native (CPU): saved curvature is kept and reported unavailable, and the
# local effects still draw.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19533'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384
PANE_H = H / 2
A = [200, 120, 40].freeze
B = [40, 160, 220].freeze
GLES = GATE_BACKEND == 'gles'
CURVATURE = 100
SCANLINES = 50

abort("presentation effects gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
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
  raise "PRESENTATION_EFFECTS_FAIL #{why}" unless ok
end

def screenshot
  sleep 0.5
  GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x2', 'BLIT_AUTH_TOKEN' => '',
  'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'))
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

$sessions = {}
def run(pane, script)
  if (old = $sessions[pane])
    request('DELETE', "/v1/terminal-sessions/#{old}")
    sleep 0.3
  end
  code, session = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', arguments: ['-c', script], pane_id: pane })
  check(code == 201, "session for #{pane}: #{code} #{session}")
  $sessions[pane] = session['id']
end

def flat(pane, rgb)
  run(pane, "printf '\\033[?25l\\033[48;2;#{rgb.join(';')}m\\033[2J'; sleep 600")
end

# Rows of alternating colour: 16 px bands to warp.
def bands(pane)
  run(pane, "printf '\\033[?25l'; i=1; while [ $i -le 14 ]; do c=#{A.join(';')}; " \
            "[ $((i % 2)) = 0 ] && c=#{B.join(';')}; printf \"\\033[$i;1H\\033[48;2;${c}m\\033[2K\"; " \
            'i=$((i + 1)); done; sleep 600')
end

# Vertical stripes, three cells wide, in A and B: edges for RGB shift.
def stripes(pane)
  run(pane, "printf '\\033[?25l'; a='\\033[48;2;#{A.join(';')}m   '; b='\\033[48;2;#{B.join(';')}m   '; " \
            "r=''; i=0; while [ $i -lt 14 ]; do r=\"$r$a$b\"; i=$((i + 1)); done; " \
            'j=1; while [ $j -le 14 ]; do printf "\\033[$j;1H$r"; j=$((j + 1)); done; sleep 600')
end

def save_effects(effects)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document['effects'] = effects
  request('PUT', '/v1/settings', { document: document }, settings['revision'])
end

def saved!(effects)
  code, reply = save_effects(effects)
  check(code == 200, "saving #{effects}: #{code} #{reply}")
end

# The shared warp (src/blit_effects.h): screen q -> content s, pane-normalised.
def warp(k, qx, qy)
  f = (1.0 + k * (qx * qx + qy * qy)) / (1.0 + k)
  [qx * f, qy * f]
end

def pane1(shot)
  (0...PANE_H).map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def pane2_bytes(shot)
  (PANE_H...H).flat_map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def darken(rgb, alpha)
  rgb.map { |c| (c * (255 - alpha) + 127) / 255 }
end

def near?(actual, wanted, tolerance = 2)
  actual.zip(wanted).all? { |a, b| (a - b).abs <= tolerance }
end

Dir.mktmpdir('blit-presentation-effects') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  start(env)
  output = get('/v1/displays')['displays'].first.dig('state', 'connector')
  flat('pane-display-1-2', B)
  # The pointer is drawn over everything: park it at pane 2's last pixel for
  # every capture, so it is the same in all of them.
  park = -> { request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: W - 1, y: H - 1 }) }
  park.call
  k = CURVATURE / 400.0
  checked = 0

  # The registry says which class each effect is.
  classes = get('/v1/effects')['available'].to_h { |e| [e['name'], e['class']] }
  check(classes == { 'scanlines' => 'local', 'corners' => 'local',
                     'curvature' => 'presentation', 'glow' => 'presentation',
                     'rgb_shift' => 'presentation', 'glowing_line' => 'presentation',
                     'flicker' => 'presentation', 'horizontal_sync' => 'presentation',
                     'jitter' => 'presentation' },
        "effect classes: #{classes}")
  titles = get('/v1/effects')['available'].to_h { |e| [e['name'], e['title']] }
  check(titles['rgb_shift'] == 'RGB shift' && titles['corners'] == 'Corners', "effect titles: #{titles}")
  animated = get('/v1/effects')['available'].select { |e| e['animated'] }.map { |e| e['name'] }
  check(animated == %w[glowing_line flicker horizontal_sync jitter], "animated effects: #{animated}")
  # ADR 0190's per-frame randomness is a pure function of the frame number.
  probe = File.join(root, 'noise_probe')
  File.write("#{probe}.c", <<~C)
    #include <stdio.h>
    #include "blit_effects.h"
    int main(void) {
        unsigned long long f;
        for (f = 1; f <= 3; ++f) {
            double r, g, s, r2, g2, s2;
            blit_effect_frame_noise(f, &r, &g, &s);
            blit_effect_frame_noise(f, &r2, &g2, &s2);
            if (r != r2 || g != g2 || s != s2 || r < 0 || r >= 1 || g < 0 || g >= 1) return 1;
            printf("%.6f %.6f\n", r, g);
        }
        return 0;
    }
  C
  check(system('tcc', '-I', File.join(ROOT, 'src'), "#{probe}.c", '-o', probe), 'noise probe compile')
  values = `#{probe}`.split("\n")
  check($?.success? && values.uniq.size == 3, "frame noise is not a deterministic function of the frame: #{values}")

  if GLES
    # The plain path: no presentation effects, nothing for the pass to draw.
    bands('pane-display-1-1')
    plain = screenshot
    plain_pane1 = pane1(plain)
    plain_pane2 = pane2_bytes(plain)
    fx = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects')
    check(fx['present_panes'].zero? && fx['presentation_unavailable'] == '',
          "plain path drew a presentation pass: #{fx}")

    # Warp.
    saved!(output => { '1' => { 'curvature' => { 'amount' => CURVATURE } } })
    curved = screenshot
    fx = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects')
    check(fx['present_panes'] == 1, "presentation pass not drawn: #{fx}")
    black = 0
    (0...PANE_H).step(3) do |y|
      (0...W).step(3) do |x|
        sx, sy = warp(k, (x + 0.5) / W * 2 - 1, (y + 0.5) / PANE_H * 2 - 1)
        if sx.abs > 1 || sy.abs > 1
          check(curved.pixel(x, y) == [0, 0, 0], "outside the tube at #{x},#{y}: #{curved.pixel(x, y)}")
          black += 1
          next
        end
        px = ((sx + 1) / 2 * W).clamp(0.5, W - 0.5)
        py = ((sy + 1) / 2 * PANE_H).clamp(0.5, PANE_H - 0.5)
        x0 = (px - 0.5).floor
        y0 = (py - 0.5).floor
        texels = [[x0, y0], [x0 + 1, y0], [x0, y0 + 1], [x0 + 1, y0 + 1]].map do |tx, ty|
          plain_pane1[ty.clamp(0, PANE_H - 1)][tx.clamp(0, W - 1)]
        end
        next unless texels.uniq.size == 1
        check(near?(curved.pixel(x, y), texels[0]),
              "warped #{x},#{y} is #{curved.pixel(x, y)}, expected #{texels[0]} from #{px.round(2)},#{py.round(2)}")
        checked += 1
      end
    end
    check(black.positive? && checked > 5000, "warp coverage: #{checked} compared, #{black} black")
    check(pane2_bytes(curved) == plain_pane2, 'pane 2 changed when pane 1 curved')

    # Pointer: a session reporting any-motion (SGR 1006) into a file.
    mouse = File.join(root, 'mouse.txt')
    run('pane-display-1-1', "stty raw -echo; printf '\\033[?25l\\033[?1003h\\033[?1006h'; exec cat > #{mouse}")
    sleep 0.5
    grid = get('/v1/topology')['panes'].find { |p| p['id'] == 'pane-display-1-1' }['cell_grid']
    [[W * 0.8, PANE_H * 0.75], [W * 0.15, PANE_H * 0.2], [2, 2]].each do |x, y|
      File.write(mouse, '')
      request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: x, y: y })
      report = nil
      Timeout.timeout(5) { sleep 0.05 until (report = File.read(mouse)[/\e\[<\d+;(\d+);(\d+)[Mm]\z/]) }
      col, row = File.read(mouse).scan(/\e\[<\d+;(\d+);(\d+)[Mm]/).last.map(&:to_i)
      sx, sy = warp(k, x / W * 2 - 1, y / PANE_H * 2 - 1)
      hx = (sx.clamp(-1, 1) + 1) / 2 * W
      hy = (sy.clamp(-1, 1) + 1) / 2 * PANE_H
      want_col = ((hx - grid['origin_x']) / grid['cell_width']).floor + 1
      want_row = ((hy - grid['origin_y']) / grid['cell_height']).floor + 1
      want_col = want_col.clamp(1, grid['columns'])
      want_row = want_row.clamp(1, grid['rows'])
      check([col, row] == [want_col, want_row],
            "pointer at #{x},#{y} reached #{col},#{row}, the warp shows #{want_col},#{want_row}")
    end
    park.call

    # RGB shift: vertical stripes three cells wide, so every stripe edge
    # fringes. At amount 50 on 8 px cells the samples are 2 px apart: whole
    # pixels, so the three-tap rule predicts every pixel from the plain
    # capture (flat colour is unchanged: the weights sum to 1).
    stripes('pane-display-1-1')
    saved!(output => { '1' => {} })
    striped = pane1(screenshot)
    saved!(output => { '1' => { 'rgb_shift' => { 'amount' => 50 } } })
    shifted = screenshot
    weights = [[0.1, 0.6, 0.3], [0.2, 0.6, 0.2], [0.3, 0.6, 0.1]]
    fringed = 0
    shifted_checked = 0
    (0...PANE_H).step(2) do |y|
      (0...W).each do |x|
        l = striped[y][(x - 2).clamp(0, W - 1)]
        c = striped[y][x]
        r = striped[y][(x + 2).clamp(0, W - 1)]
        want = (0..2).map { |ch| (weights[ch][0] * l[ch] + weights[ch][1] * c[ch] + weights[ch][2] * r[ch]).round }
        check(near?(shifted.pixel(x, y), want),
              "RGB shift #{x},#{y} is #{shifted.pixel(x, y)}, expected #{want} from #{l} #{c} #{r}")
        fringed += 1 if want != c
        shifted_checked += 1
      end
    end
    check(fringed > 1000, "RGB shift changed too few pixels to prove anything: #{fringed}")
    check(pane2_bytes(shifted) == plain_pane2, 'RGB shift reached pane 2')
    # With curvature too: inside one stripe away from its edges the colour is
    # the stripe's, and outside the tube it is black.
    saved!(output => { '1' => { 'rgb_shift' => { 'amount' => 50 }, 'curvature' => { 'amount' => CURVATURE } } })
    both = screenshot
    sx, sy = warp(k, (2 + 0.5) / W * 2 - 1, (2 + 0.5) / PANE_H * 2 - 1)
    check(sx.abs > 1 || sy.abs > 1, 'corner probe is inside the tube')
    check(both.pixel(2, 2) == [0, 0, 0], "curved RGB shift outside the tube: #{both.pixel(2, 2)}")
    check(both.pixel(W / 2, PANE_H / 2) != [0, 0, 0], 'curved RGB shift drew nothing at the centre')

    # Scanlines under curvature: on the glass, once.
    flat('pane-display-1-1', A)
    saved!(output => { '1' => { 'curvature' => { 'amount' => CURVATURE },
                                'scanlines' => { 'intensity' => SCANLINES } } })
    glass = screenshot
    alpha = SCANLINES * 255 / 100
    rows = 0
    (20...PANE_H - 20).each do |y|
      x = W / 2
      sx, sy = warp(k, (x + 0.5) / W * 2 - 1, (y + 0.5) / PANE_H * 2 - 1)
      next if sx.abs > 0.9 || sy.abs > 0.9
      want = y.odd? ? darken(A, alpha) : A
      check(near?(glass.pixel(x, y), want), "glass row #{y}: #{glass.pixel(x, y)}, expected #{want}")
      rows += 1
    end
    check(rows > 100, "too few glass rows checked: #{rows}")

    # Glow: one white cell on black.
    run('pane-display-1-1', "printf '\\033[?25l\\033[48;2;0;0;0m\\033[2J\\033[6;40H\\033[48;2;255;255;255m  '; sleep 600")
    saved!(output => { '1' => {} })
    dark = screenshot
    saved!(output => { '1' => { 'glow' => { 'intensity' => 100 } } })
    glowing = screenshot
    cell = get('/v1/topology')['panes'].find { |p| p['id'] == 'pane-display-1-1' }['cell_grid']
    cx = cell['origin_x'] + 39 * cell['cell_width']
    cy = cell['origin_y'] + 5 * cell['cell_height']
    near_x = cx - 4
    near_y = cy + cell['cell_height'] / 2
    check(dark.pixel(near_x, near_y) == [0, 0, 0], "no-glow neighbour not black: #{dark.pixel(near_x, near_y)}")
    check(glowing.pixel(near_x, near_y).sum > 30, "glow did not brighten the neighbour: #{glowing.pixel(near_x, near_y)}")
    check(glowing.pixel(10, PANE_H - 10) == [0, 0, 0], "glow reached a far pixel: #{glowing.pixel(10, PANE_H - 10)}")
    diff = pane2_bytes(glowing).each_with_index.reject { |px, i| px == plain_pane2[i] }
    check(diff.empty?, "glow crossed into pane 2: #{diff.size} pixels, first " \
                       "#{diff.first(3).map { |px, i| [i % W, PANE_H + i / W, px, plain_pane2[i]] }}")
    rebuilds = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects', 'glow_rebuilds')
    check(rebuilds.positive?, "glow rebuilds not reported: #{rebuilds}")
    # A glow copy follows its own pane: two seconds of drawing in pane 2
    # rebuild nothing for the idle, glowing pane 1.
    run('pane-display-1-2', "printf '\\033[?25l'; i=0; while :; do i=$((i + 1)); printf '\\033[1;1H%d' $i; sleep 0.05; done")
    sleep 0.6
    settled = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects', 'glow_rebuilds')
    sleep 2
    later = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects', 'glow_rebuilds')
    check(later == settled, "drawing in pane 2 rebuilt pane 1's glow #{later - settled} times")
    flat('pane-display-1-2', B)
    sleep 0.5
    # The glowing line (ADR 0188). Over a flat colour, the brightened rows of
    # pane 1 are one run ending at the band's leading edge, rising toward it
    # by the shared smoothstep, nothing below it; a moment later the edge is
    # lower. It is animated by the frame clock, which re-presents without
    # redrawing, at the configured rate, and stops when the line is off.
    flat('pane-display-1-1', A)
    run('pane-display-1-2', "printf '\\033[?25l\\033[48;2;#{B.join(';')}m\\033[2J'; sleep 600")
    saved!(output => { '1' => { 'glowing_line' => { 'intensity' => 100 } } })
    lift = ->(shot, y) { shot.pixel(W / 2, y)[0] - A[0] }
    edge_of = lambda do |shot|
      rows = (0...PANE_H).select { |y| lift.call(shot, y) > 1 }
      next nil if rows.empty? || rows.last >= PANE_H - 2 || rows.first <= 1
      check(rows == (rows.first..rows.last).to_a, "the band is not one run: #{rows}")
      rows
    end
    # The band and every pixel read about it come from one screenshot. It
    # moves between screenshots, so reading a later shot at an earlier shot's
    # band found the light where the band had gone ("light below the leading
    # edge") whenever the band had left the pane in between.
    band = shot = nil
    Timeout.timeout(15) do
      until band
        shot = screenshot
        band = edge_of.call(shot)
        sleep 0.2 unless band
      end
    end
    length = PANE_H / 3
    check(band.size <= length + 2 && band.size >= length / 2, "band length #{band.size}, expected about #{length}")
    lifts = band.map { |y| lift.call(shot, y) }
    check(lifts.each_cons(2).all? { |a, b| b >= a - 1 }, "the band does not rise toward its edge: #{lifts}")
    check(lifts.last >= 30 && lifts.last <= 52, "the band's brightest row adds #{lifts.last}, expected up to 51")
    check(lift.call(shot, band.last + 2).abs <= 1, 'light below the leading edge')
    sleep 0.6
    later = nil
    Timeout.timeout(10) { sleep 0.1 until (later = edge_of.call(screenshot)) }
    check(later.last > band.last || later.last < band.first, "the band did not move down: #{band.last} -> #{later.last}")
    # The clock: frames at the configured rate, nothing redrawn, no glow work.
    fx = -> { get('/v1/diagnostics').dig('render', 'outputs', 0) }
    first = fx.call
    check(first.dig('effects', 'animation_hz') == 30, "animation_hz #{first.dig('effects', 'animation_hz')}")
    drawn = []
    10.times { sleep 0.2; drawn << fx.call.dig('kms_phases', 'drawn_cells') }
    last = fx.call
    ticks = last.dig('effects', 'animation_frames') - first.dig('effects', 'animation_frames')
    elapsed = 2.0
    check(ticks >= 30 && ticks <= 75, "#{ticks} animation frames in about #{elapsed} s at 30 Hz")
    check(drawn.all?(&:zero?), "animation frames redrew cells: #{drawn}")
    check(last.dig('effects', 'glow_rebuilds') == first.dig('effects', 'glow_rebuilds'), 'animation rebuilt glow')
    # An animation frame is due when its tick says so: the gap since the
    # output's last frame is not a missed deadline (a 30 Hz effect on a
    # 60 Hz output counted one a frame before 0.3.13).
    missed = last['missed_deadlines'] - first['missed_deadlines']
    check(missed <= 3, "#{missed} missed deadlines in #{ticks} animation-only frames")
    # Real frames carry the animation; ticks only fill gaps. With pane 2
    # drawing continuously (as video does), the present rate is what it is
    # without animation, and almost no frame is animation-only. (On the
    # owner's machine, with video playing, ticks beside real frames made 42
    # presents a second instead of 14 and multiplied the late flips.)
    # Both runs keep a static presentation effect (curvature), so the only
    # difference is the animation: the presentation pass itself costs time
    # on a software GPU, and is measured elsewhere.
    saved!(output => { '1' => { 'glowing_line' => { 'intensity' => 100 }, 'curvature' => { 'amount' => 10 } } })
    run('pane-display-1-2', "printf '\\033[?25l'; i=0; while :; do i=$((i + 1)); printf '\\033[1;1H%d' $i; sleep 0.004; done")
    sleep 0.8
    rate = lambda do
      a0 = fx.call; sleep 2; a1 = fx.call
      [(a1['present_calls'] - a0['present_calls']) / 2.0,
       (a1.dig('effects', 'animation_frames') - a0.dig('effects', 'animation_frames')) / 2.0]
    end
    with_line, gap_ticks = rate.call
    saved!(output => { '1' => { 'curvature' => { 'amount' => 10 } } })
    sleep 0.5
    without_line, = rate.call
    check(with_line >= without_line * 0.85,
          "animation held real frames back: #{with_line}/s with the line, #{without_line}/s without")
    check(gap_ticks <= 3, "#{gap_ticks} animation-only frames/s while real frames were flowing")
    saved!(output => { '1' => { 'glowing_line' => { 'intensity' => 100 } } })
    flat('pane-display-1-2', B)
    sleep 0.6
    # The rate setting.
    settings = get('/v1/settings')
    document = settings['document']
    document['server'] = (document['server'] || {}).merge('effects_animation_hz' => 60)
    code, = request('PUT', '/v1/settings', { document: document }, settings['revision'])
    check(code == 400, "effects_animation_hz 60 -> #{code}")
    document['server']['effects_animation_hz'] = 10
    code, = request('PUT', '/v1/settings', { document: document }, settings['revision'])
    check(code == 200, "effects_animation_hz 10 -> #{code}")
    a = fx.call.dig('effects', 'animation_frames')
    sleep 2
    slow = fx.call.dig('effects', 'animation_frames') - a
    check(slow >= 10 && slow <= 28, "#{slow} animation frames in 2 s at 10 Hz")
    document = get('/v1/settings')['document']
    document['server'].delete('effects_animation_hz')
    request('PUT', '/v1/settings', { document: document }, get('/v1/settings')['revision'])
    # No clock while it is off: an idle screen presents nothing.
    [{ output => { '1' => {} } },
     { output => { '1' => { 'enabled' => false, 'glowing_line' => { 'intensity' => 100 } } } },
     { 'enabled' => false, output => { '1' => { 'glowing_line' => { 'intensity' => 100 } } } }].each do |off|
      saved!(off)
      sleep 0.6
      p0 = fx.call['present_calls']
      sleep 1.5
      p1 = fx.call['present_calls']
      check(p1 - p0 <= 2, "an idle screen presented #{p1 - p0} frames with the line off (#{off})")
      check(fx.call.dig('effects', 'animation_hz').zero?, "animation_hz not 0 with the line off (#{off})")
    end
    flat('pane-display-1-2', B)

    # ADR 0190. Flicker: one brightness for the whole pane per frame,
    # within 1 +- intensity/200, varying between frames. A dark grey, so
    # nothing clips at +-50%.
    grey = [60, 60, 60]
    flat('pane-display-1-1', grey)
    saved!(output => { '1' => { 'flicker' => { 'intensity' => 100 } } })
    factors = []
    8.times do
      shot = screenshot
      samples = [[W / 4, PANE_H / 4], [W / 2, PANE_H / 2], [3 * W / 4, 3 * PANE_H / 4]].map { |x, y| shot.pixel(x, y) }
      check(samples.uniq.size == 1, "flicker is not one brightness across the pane: #{samples}")
      v = samples[0][0]
      check(v >= 29 && v <= 91, "flicker took #{grey[0]} to #{v}, outside +-50%")
      factors << v
      check(pane2_bytes(shot) == plain_pane2, 'flicker reached pane 2')
    end
    check(factors.uniq.size >= 3, "flicker did not vary: #{factors}")

    # Horizontal sync at 100: over stripes, some frames sway (rows displaced
    # by a sine), some are plain; the corners do not move.
    stripes('pane-display-1-1')
    saved!(output => { '1' => { 'corners' => { 'radius_px' => 24 } } })
    still = pane1(screenshot)
    saved!(output => { '1' => { 'corners' => { 'radius_px' => 24 }, 'horizontal_sync' => { 'amount' => 100 } } })
    swayed = plain = 0
    # The pass draws these corners in float, the scene in integers: within 2.
    worst = ->(img) { img.each_with_index.map { |row, y| row.each_with_index.map { |px, x| px.zip(still[y][x]).map { |p, q| (p - q).abs }.max }.max }.max }
    20.times do
      shot = pane1(screenshot)
      if worst.call(shot) <= 2 then plain += 1 else swayed += 1 end
      check(shot[0][0] == [0, 0, 0] && shot[1][2] == [0, 0, 0] && shot[PANE_H - 1][W - 1] == [0, 0, 0],
            'a corner moved during horizontal sync')
    end
    check(swayed >= 2 && plain >= 2, "horizontal sync: #{swayed} swaying frames, #{plain} plain, of 20")

    # Jitter at 100: stripe edges shimmer between frames; the pane's mean
    # colour holds.
    saved!(output => { '1' => { 'jitter' => { 'amount' => 100 } } })
    a = pane1(screenshot)
    b = pane1(screenshot)
    edge = (0...PANE_H).count { |y| a[y] != b[y] }
    check(edge > PANE_H / 4, "jitter changed only #{edge} rows between frames")
    mean = ->(img) { img.flatten(1).transpose.map { |ch| ch.sum.to_f / ch.size } }
    m0 = mean.call(striped)
    m1 = mean.call(a)
    check(m0.zip(m1).all? { |x, y| (x - y).abs < 6 }, "jitter moved the mean colour: #{m0} -> #{m1}")
    saved!(output => { '1' => {} })
  else
    # CPU: a saved glowing line is kept, reported unavailable, and costs no
    # frames.
    flat('pane-display-1-1', A)
    saved!(output => { '1' => { 'glowing_line' => { 'intensity' => 100 } } })
    pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
    check(pane['unavailable'] == { 'glowing_line' => 'cpu compositor' }, "CPU glowing line: #{pane}")
    sleep 0.6
    p0 = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    sleep 1.5
    p1 = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    check(p1 - p0 <= 2, "a CPU-composed idle screen presented #{p1 - p0} frames for a glowing line")
    # CPU: kept, reported unavailable, local effects still drawn.
    flat('pane-display-1-1', A)
    saved!(output => { '1' => { 'curvature' => { 'amount' => 50 }, 'corners' => { 'radius_px' => 20 } } })
    pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
    check(pane['effects'].key?('curvature') && pane['unavailable'] == { 'curvature' => 'cpu compositor' },
          "CPU curvature not reported unavailable: #{pane}")
    shot = screenshot
    check(shot.pixel(0, 0) == [0, 0, 0] && shot.pixel(W / 2, PANE_H / 2) == A,
          "CPU local effects with curvature saved: #{shot.pixel(0, 0)} #{shot.pixel(W / 2, PANE_H / 2)}")
  end

  # Switches: off draws exactly what no effects draws, and keeps the values.
  flat('pane-display-1-1', A)
  saved!(output => { '1' => {} })
  none = screenshot
  on = { 'corners' => { 'radius_px' => 30 }, 'scanlines' => { 'intensity' => 40 } }
  on['curvature'] = { 'amount' => 60 } if GLES
  saved!(output => { '1' => on })
  check(screenshot.pixel(0, 0) == [0, 0, 0], 'effects not drawn while on')
  saved!('enabled' => false, output => { '1' => on })
  check(pane1(screenshot) == pane1(none), 'master switch off still draws effects')
  api = get('/v1/effects')
  check(api['enabled'] == false && api['panes'][0]['effects'] == {}, "master off in the API: #{api}")
  fx = get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects')
  check(fx['gpu_quads'].zero? && fx['present_panes'].zero?, "master off still costs: #{fx}")
  saved!(output => { '1' => on.merge('enabled' => false) })
  check(pane1(screenshot) == pane1(none), 'pane switch off still draws effects')
  pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
  check(pane['enabled'] == false && pane['effects'] == {}, "pane off in the API: #{pane}")
  saved = get('/v1/settings').dig('document', 'effects', output, '1')
  check(saved['corners'] == { 'radius_px' => 30 }, "values not kept while off: #{saved}")
  code, = save_effects('enabled' => 'no', output => { '1' => on })
  check(code == 400, "a master switch that is not a boolean -> #{code}")
  code, = save_effects(output => { '1' => on.merge('enabled' => 1) })
  check(code == 400, "a pane switch that is not a boolean -> #{code}")

  # A settings write that leaves the font alone does not touch it: no pane
  # is resized, so a session sees no SIGWINCH. Writing the whole document
  # used to reset to the boot font and back -- two reflows of every pane
  # and two glyph atlas rebuilds per write, on every live effects change.
  # A saved font profile other than the boot font, as on a real desktop.
  code, = request('PUT', '/v1/fonts', { font_id: 'hack', size_px: 20 })
  check(code == 200, "font profile: #{code}")
  Timeout.timeout(10) { sleep 0.1 until get('/v1/settings').dig('document', 'font', 'size_px') == 20 }
  before = get('/v1/topology')['panes'].map { |p| p['cell_grid'].values_at('columns', 'rows') }
  winch = File.join(root, 'winch.txt')
  run('pane-display-1-2', "trap 'echo W $(stty size) >> #{winch}' WINCH; : > #{winch}; printf '\\033[?25l'; while :; do sleep 0.1; done")
  # The session's own start-up size kick is not what is measured.
  sleep 1.5
  File.write(winch, '')
  10.times { |i| saved!(output => { '1' => { 'corners' => { 'radius_px' => 10 + i } } }) }
  sleep 0.5
  after = get('/v1/topology')['panes'].map { |p| p['cell_grid'].values_at('columns', 'rows') }
  check(after == before, "effects writes changed the grid: #{before} -> #{after}")
  check(get('/v1/fonts').dig('active', 'size_px') == 20, 'effects writes changed the font')
  # Blit's own attach kicks to a new session are not resizes; a font round
  # trip would show a second geometry in between.
  sizes = File.read(winch).split("\n").map { |l| l.sub('W ', '') }.uniq
  check(sizes.size <= 1, "effects writes passed the panes through other sizes: #{sizes}")

  puts "PRESENTATION_EFFECTS_PASS backend=#{GATE_BACKEND} warped_pixels=#{checked} " \
       "#{GLES ? 'warp=shared glass_scanlines=once glow=bounded,own_pane rgb_shift=exact glowing_line=shape,moves,clock,off_is_free,fills_gaps_only flicker=uniform,varies sync=sways,corners_still jitter=shimmers,mean_holds isolation=pane pointer=warped plain_path=untouched' : 'cpu=unavailable_kept'} " \
       'switches=bypass values=kept settings_writes=no_reflow'
end
