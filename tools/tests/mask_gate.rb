#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0209: the phosphor mask -- aperture grille, slot mask, shadow mask --
# laid on the display's physical pixels. One display split 1x2; pane 1 gets
# the effects, pane 2 never does. Pane 1 shows stripes in two colours, a
# white block (where the compensation clips) and flat grey 60 (where it
# never does, even at the slot mask's gain of 4).
#
# On both:
#   * the registry: `mask {amount 0..100}` and `mask_type {kind 0..2}`,
#     presentation, not animated, as GET /v1/effects reports them
#   * strict settings: a kind or amount out of range is refused
# On gles:
#   * the rule: for each type at 100 and 40, every pixel of pane 1 is the
#     plain capture's pixel through the shared C rule
#     (blit_effect_mask_rgb, compiled into a probe here), within 1; and
#     with brightness and contrast, through blit_effect_tube_colour first
#   * the pattern, read from the capture alone over the flat grey: its
#     period (3 x 1, 6 x 4, 6 x 4), one channel lit per pixel (none in the
#     slot mask's gaps), the lit channel's column, and the other two at
#     1 - s of it
#   * the compensation: over the grey, each channel's mean over the region
#     is the grey's own, within 1, at every type and strength
#   * strength: 40 and 100 draw different pixels, each by the rule
#   * the unchanged path: amount 0 draws no pass and the pane is
#     byte-identical; a type alone takes the pass and is byte-identical
#   * zoom 200%: the mask is physical -- every pixel is the plain 200%
#     capture's pixel through the rule at its physical (x, y), and the
#     frame is not 2x2 blocks
#   * isolation: pane 2 is byte-identical throughout
# On native (CPU): saved mask settings are kept, reported unavailable
# ("cpu compositor"), draw nothing and cost no frames.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19629'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384
PANE_H = H / 2
A = [200, 120, 40].freeze
B = [40, 160, 220].freeze
GREY = [60, 60, 60].freeze
WHITE = [255, 255, 255].freeze
# Pane 1's flat grey at 100%: rows 9..12 of cells, clear of the cursor row.
FLAT = { x: 0...600, y: 136...184 }.freeze
KINDS = { 0 => [3, 1], 1 => [6, 4], 2 => [6, 4] }.freeze
GLES = GATE_BACKEND == 'gles'

abort("mask gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'PATCH' => Net::HTTP::Patch,
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
  raise "MASK_FAIL #{why}" unless ok
end

# A screenshot once two in a row agree.
def settled
  previous = nil
  Timeout.timeout(10) do
    loop do
      sleep 0.15
      bytes = Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1')
      return GateBmp.new(bytes) if bytes == previous
      previous = bytes
    end
  end
rescue Timeout::Error
  check(false, 'the screenshots never settled')
end

def pane1(shot)
  (0...PANE_H).map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def pane2(shot)
  (PANE_H...H).flat_map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def near?(actual, wanted, tolerance = 1)
  actual.zip(wanted).all? { |a, b| (a - b).abs <= tolerance }
end

def fx
  get('/v1/diagnostics').dig('render', 'outputs', 0)
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x2', 'BLIT_AUTH_TOKEN' => '',
  'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'))
$server = nil
at_exit do
  if $server
    Process.kill('TERM', $server) rescue nil
    Process.wait($server) rescue nil
  end
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

def session(pane, argv)
  code, reply = request('POST', '/v1/terminal-sessions',
                        { command: argv[0], arguments: argv[1..], pane_id: pane })
  check(code == 201, "session for #{pane}: #{code} #{reply}")
end

Dir.mktmpdir('blit-mask') do |root|
  # The shared rule, from src/blit_effects.h: one line per pixel,
  # "amount kind brightness contrast x y r g b" -> the drawn "r g b".
  probe = File.join(root, 'mask_probe')
  File.write("#{probe}.c", <<~C)
    #include <stdio.h>
    #include <string.h>
    #include "blit_effects.h"
    int main(void) {
        int amount, kind, brightness, contrast, x, y, r, g, b, i;
        while (scanf("%d %d %d %d %d %d %d %d %d", &amount, &kind, &brightness, &contrast,
                     &x, &y, &r, &g, &b) == 9) {
            BlitEffectSet set;
            double c[3], f[3];
            memset(&set, 0, sizeof(set));
            set.value[BLIT_EFFECT_MASK] = amount;
            set.value[BLIT_EFFECT_MASK_TYPE] = kind;
            set.value[BLIT_EFFECT_BRIGHTNESS] = brightness;
            set.value[BLIT_EFFECT_CONTRAST] = contrast;
            c[0] = r / 255.0; c[1] = g / 255.0; c[2] = b / 255.0;
            blit_effect_tube_colour(&set, c);
            blit_effect_mask_rgb(&set, x, y, f);
            for (i = 0; i < 3; ++i)
                printf("%d%c", (int)(blit_effect_clamp01(c[i] * f[i]) * 255.0 + 0.5),
                       i < 2 ? ' ' : '\\n');
        }
        return 0;
    }
  C
  check(system('tcc', '-I', File.join(ROOT, 'src'), "#{probe}.c", '-o', probe), 'mask probe compile')
  # Every pixel of a capture (rows, top-left at physical (0, 0)) through
  # the rule, for one set.
  predict = lambda do |rows, amount:, kind: 0, brightness: 0, contrast: 0|
    input = +''
    rows.each_with_index do |row, y|
      row.each_with_index { |c, x| input << "#{amount} #{kind} #{brightness} #{contrast} #{x} #{y} #{c.join(' ')}\n" }
    end
    # Through a file: the answer outgrows a pipe's buffer before the
    # question is written.
    File.write("#{probe}.in", input)
    out = IO.popen([probe], in: "#{probe}.in", &:read).split("\n")
    rows.each_with_index.map { |row, y| row.each_index.map { |x| out[y * row.size + x].split.map(&:to_i) } }
  end
  # [pixels compared, the first offenders] of a capture against a prediction.
  compare = lambda do |drawn, wanted|
    wrong = []
    n = 0
    drawn.each_with_index do |row, y|
      row.each_with_index do |c, x|
        n += 1
        wrong << [x, y, c, wanted[y][x]] unless near?(c, wanted[y][x])
      end
    end
    [n, wrong]
  end
  describe = ->(wrong) { wrong.first(4).map { |x, y, got, w| "(#{x},#{y}) #{got} not #{w}" }.join(', ') }

  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  $server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  output = get('/v1/displays')['displays'].first.dig('state', 'connector')
  # Pane 1: four rows of stripes three cells wide, two of a white block,
  # the rest grey; drawn again on SIGWINCH (zoom). A file: a session
  # argument is at most 255 bytes.
  script = File.join(root, 'picture.sh')
  File.write(script, <<~SH)
    a='\\033[48;2;#{A.join(';')}m   '; b='\\033[48;2;#{B.join(';')}m   '
    w='\\033[48;2;255;255;255m                    '
    r=''; i=0; while [ $i -lt 14 ]; do r="$r$a$b"; i=$((i + 1)); done
    draw() {
      printf '\\033[?25l\\033[?7l\\033[48;2;#{GREY.join(';')}m\\033[2J'
      j=1; while [ $j -le 4 ]; do printf "\\033[$j;1H$r"; j=$((j + 1)); done
      printf "\\033[5;1H$w\\033[6;1H$w\\033[48;2;#{GREY.join(';')}m"
    }
    trap draw WINCH
    draw
    while :; do sleep 0.1; done
  SH
  session('pane-display-1-1', ['/bin/sh', script])
  # Pane 2: flat B, also drawn again on SIGWINCH.
  session('pane-display-1-2', ['/bin/sh', '-c', "d() { printf '\\033[?25l\\033[48;2;#{B.join(';')}m\\033[2J'; }; " \
                                                'trap d WINCH; d; while :; do sleep 0.1; done'])
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: W - 1, y: H - 1 })

  # The registry, as the API reports it.
  available = get('/v1/effects')['available'].to_h { |e| [e['name'], e] }
  { 'mask' => ['amount', 0, 100], 'mask_type' => ['kind', 0, 2] }.each do |name, (parameter, minimum, maximum)|
    row = available[name]
    check(row && row.values_at('parameter', 'minimum', 'maximum', 'animated', 'class') ==
                 [parameter, minimum, maximum, false, 'presentation'], "registry row #{name}: #{row}")
  end
  [{ 'mask_type' => { 'kind' => 3 } }, { 'mask_type' => { 'kind' => -1 } },
   { 'mask' => { 'amount' => 101 } }, { 'mask' => { 'amount' => -1 } },
   { 'mask' => { 'amount' => 50, 'kind' => 1 } }].each do |bad|
    code, = save_effects(output => { '1' => bad })
    check(code == 400, "#{bad} -> #{code}, expected 400")
  end
  Timeout.timeout(10) { sleep 0.1 until pane1(settled)[FLAT[:y].first][0] == GREY }

  unless GLES
    plain = pane1(settled)
    set = { 'mask' => { 'amount' => 100 }, 'mask_type' => { 'kind' => 2 } }
    saved!(output => { '1' => set })
    pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
    check(pane['effects'] == set && pane['unavailable'] == set.keys.to_h { |k| [k, 'cpu compositor'] },
          "CPU mask not kept and reported unavailable: #{pane}")
    check(pane1(settled) == plain, 'the CPU compositor drew the mask')
    sleep 0.6
    p0 = fx['present_calls']
    sleep 1.5
    p1 = fx['present_calls']
    check(p1 - p0 <= 2, "a CPU-composed idle screen presented #{p1 - p0} frames for the mask")
    puts 'MASK_PASS backend=cpu mask=unavailable_kept,undrawn,no_frames registry=rows,strict'
    exit 0
  end

  shot = settled
  plain = pane1(shot)
  plain2 = pane2(shot)
  colours = plain.flatten(1).uniq
  check(([A, B, WHITE, GREY] - colours).empty?, "the picture is not drawn: #{colours.first(6)}")

  # The unchanged path.
  saved!(output => { '1' => { 'mask' => { 'amount' => 0 } } })
  check(pane1(settled) == plain && fx.dig('effects', 'present_panes').zero?,
        'mask 0 drew a pass or changed pane 1')
  saved!(output => { '1' => { 'mask_type' => { 'kind' => 2 } } })
  check(pane1(settled) == plain, 'a mask type alone changed pane 1')
  check(fx.dig('effects', 'present_panes') == 1, 'a mask type alone did not take the pass')

  compared = 0
  drawn_at = {}
  [100, 40].each do |amount|
    KINDS.each do |kind, (px, py)|
      saved!(output => { '1' => { 'mask' => { 'amount' => amount }, 'mask_type' => { 'kind' => kind } } })
      shot = settled
      drawn = pane1(shot)
      check(fx.dig('effects', 'present_panes') == 1, "kind #{kind} at #{amount}: no pass")
      n, wrong = compare.call(drawn, predict.call(plain, amount: amount, kind: kind))
      compared += n
      check(wrong.empty?, "kind #{kind} at #{amount}: #{wrong.size} pixels off the shared rule, first #{describe.call(wrong)}")
      check(pane2(shot) == plain2, "kind #{kind} at #{amount} reached pane 2")
      drawn_at[[kind, amount]] = drawn

      # The pattern, from the capture alone, over the flat grey.
      flat = FLAT[:y].map { |y| FLAT[:x].map { |x| drawn[y][x] } }
      period = lambda do |dx, dy|
        (0...flat.size - dy).all? { |y| (0...flat[0].size - dx).all? { |x| flat[y][x] == flat[y + dy][x + dx] } }
      end
      found = [(1..12).find { |p| period.call(p, 0) }, (1..8).find { |p| period.call(0, p) }]
      check(found == [px, py], "kind #{kind} at #{amount}: period #{found}, not #{[px, py]}")
      s = amount / 100.0
      lit = kind == 1 ? 0.25 : 1.0 / 3
      gain = 1.0 / (lit + (1 - lit) * (1 - s))
      top = (GREY[0] * gain).round
      dim = (GREY[0] * (1 - s) * gain).round
      flat.each_with_index do |row, y|
        row.each_with_index do |c, x|
          gx = x + FLAT[:x].first
          gy = y + FLAT[:y].first
          channel = case kind
                    when 1 then (gy + 2 * ((gx / 3) & 1)) % 4 == 3 ? nil : gx % 3
                    when 2 then (gx + 3 * (gy / 2)) % 6 / 2
                    else gx % 3
                    end
          want = [0, 1, 2].map { |i| i == channel ? top : dim }
          check(near?(c, want), "kind #{kind} at #{amount}: (#{gx},#{gy}) is #{c}, want #{want} (channel #{channel.inspect})")
        end
      end
      # Compensation: a colour the gain does not clip keeps its light.
      means = [0, 1, 2].map { |i| flat.sum { |row| row.sum { |c| c[i] } }.to_f / (flat.size * flat[0].size) }
      check(means.all? { |m| (m - GREY[0]).abs <= 1 }, "kind #{kind} at #{amount}: mean #{means.map { |m| m.round(2) }}, not #{GREY[0]}")
    end
  end
  KINDS.each_key do |kind|
    check(drawn_at[[kind, 100]] != drawn_at[[kind, 40]], "kind #{kind}: 40 and 100 draw the same")
  end

  # After the colour controls: the tube's rule first, then the mask's.
  tube = { amount: 70, kind: 1, brightness: -30, contrast: 25 }
  saved!(output => { '1' => { 'mask' => { 'amount' => 70 }, 'mask_type' => { 'kind' => 1 },
                              'brightness' => { 'level' => -30 }, 'contrast' => { 'level' => 25 } } })
  n, wrong = compare.call(pane1(settled), predict.call(plain, **tube))
  compared += n
  check(wrong.empty?, "with the colour controls: #{wrong.size} pixels off the shared rule, first #{describe.call(wrong)}")

  # 200%: the plain picture enlarged, the mask on physical pixels.
  saved!(output => { '1' => {} })
  code, item = request('PATCH', '/v1/displays/display-1', { 'configuration' => { 'zoom' => 200 } })
  check(code == 200, "PATCH zoom 200: #{code} #{item}")
  Timeout.timeout(10) { sleep 0.1 until get('/v1/displays/display-1').dig('bounds', 'width') == W / 2 }
  sleep 0.5
  zoomed = pane1(settled)
  # Logical rows 0..95: the stripes, then the white block.
  check(zoomed[0][0] == A && zoomed[1][1] == A && zoomed[150][0] == WHITE,
        "the 200% picture is not drawn: #{[zoomed[0][0], zoomed[150][0]]}")
  zoom_blocks = 0
  [0, 2].each do |kind|
    saved!(output => { '1' => { 'mask' => { 'amount' => 100 }, 'mask_type' => { 'kind' => kind } } })
    drawn = pane1(settled)
    n, wrong = compare.call(drawn, predict.call(zoomed, amount: 100, kind: kind))
    compared += n
    check(wrong.empty?, "200%, kind #{kind}: #{wrong.size} pixels off the rule at physical pixels, first #{describe.call(wrong)}")
    # A physical mask splits the zoom's 2x2 blocks: the grille's every
    # one, the shadow mask's (2 px dots) every other pair of rows.
    split = (0...PANE_H).step(2).sum do |y|
      (0...W).step(2).count { |x| [drawn[y][x + 1], drawn[y + 1][x], drawn[y + 1][x + 1]].any? { |c| c != drawn[y][x] } }
    end
    check(split >= W * PANE_H / 16, "200%, kind #{kind}: only #{split} 2x2 blocks are split -- the mask was enlarged")
    zoom_blocks += split
  end
  request('PATCH', '/v1/displays/display-1', { 'configuration' => { 'zoom' => 100 } })
  Timeout.timeout(10) { sleep 0.1 until get('/v1/displays/display-1').dig('bounds', 'width') == W }
  # The zoom clamped the pointer to the smaller display; back to its corner.
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: W - 1, y: H - 1 })

  # Off: the plain picture again.
  saved!(output => { '1' => {} })
  sleep 0.5
  shot = settled
  check(pane1(shot) == plain, 'the mask switched off left a pattern')
  diff2 = pane2(shot).each_with_index.reject { |c, i| c == plain2[i] }.map { |c, i| [i % W, PANE_H + i / W, c, plain2[i]] }
  check(diff2.empty?, "pane 2 changed: #{diff2.size} pixels, first #{diff2.first(4)}")
  puts "MASK_PASS backend=gles rule=shared(#{compared}px) kinds=grille_3x1,slot_6x4,shadow_6x4 " \
       'attenuation=one_channel,1-s compensation=mean_kept strength=40!=100 after_colour=yes ' \
       "zero=no_pass,identical type_alone=identical zoom200=physical(#{zoom_blocks}_split_blocks) " \
       'isolation=pane registry=rows,strict'
end
