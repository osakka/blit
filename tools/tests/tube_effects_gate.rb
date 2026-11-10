#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0207: the tube's colour controls (chroma toward a phosphor colour,
# brightness, contrast) and static noise. One display split 1x2; pane 1 gets
# the effects, pane 2 never does.
#
# On both:
#   * the registry: the five rows, their classes, `animated` and the signed
#     range of brightness and contrast, as GET /v1/effects reports them
#   * strict settings: a value outside a row's minimum..maximum is refused; a
#     negative level is kept, reported and saved as written
# On gles:
#   * colour: over three-cell stripes in two colours, every pixel of pane 1
#     is the plain capture's pixel through the shared C rule
#     (blit_effect_tube_colour, compiled into a probe here), within 1, for
#     chroma 100 in a green phosphor -- the stripes become one hue, the
#     phosphor's -- chroma 50 in amber, brightness down and up, contrast
#     down and up, and all four together
#   * the unchanged path: those effects at 0 draw no presentation pass and
#     the pane is byte-identical to the plain capture
#   * static noise: over dark grey, each frame adds grey grain within
#     0..BLIT_EFFECT_NOISE_GAIN of white, most pixels differ from frame to
#     frame, the frame clock runs, and off is the plain capture again with no
#     frames on an idle screen; under chroma the grain is the phosphor's
#   * isolation: pane 2 is byte-identical throughout
# On native (CPU): saved colour effects and static noise are kept, reported
# unavailable ("cpu compositor"), draw nothing and cost no frames.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19611'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384
PANE_H = H / 2
A = [200, 120, 40].freeze
B = [40, 160, 220].freeze
GREY = [40, 40, 40].freeze
GREEN = 0x33ff66
AMBER = 0xffb000
NOISE_GAIN = 0.5
GLES = GATE_BACKEND == 'gles'

abort("tube effects gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  raise "TUBE_EFFECTS_FAIL #{why}" unless ok
end

def screenshot
  sleep 0.5
  GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
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

# Vertical stripes, three cells wide, in A and B, over the pane's black.
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

Dir.mktmpdir('blit-tube-effects') do |root|
  # The shared rule, from src/blit_effects.h: one line per colour,
  # "chroma phosphor brightness contrast r g b" -> the drawn "r g b".
  probe = File.join(root, 'tube_probe')
  File.write("#{probe}.c", <<~C)
    #include <stdio.h>
    #include <string.h>
    #include "blit_effects.h"
    int main(void) {
        int chroma, phosphor, brightness, contrast, r, g, b, i;
        while (scanf("%d %d %d %d %d %d %d", &chroma, &phosphor, &brightness, &contrast,
                     &r, &g, &b) == 7) {
            BlitEffectSet set;
            double c[3];
            memset(&set, 0, sizeof(set));
            set.value[BLIT_EFFECT_CHROMA] = chroma;
            set.value[BLIT_EFFECT_PHOSPHOR] = phosphor;
            set.value[BLIT_EFFECT_BRIGHTNESS] = brightness;
            set.value[BLIT_EFFECT_CONTRAST] = contrast;
            c[0] = r / 255.0; c[1] = g / 255.0; c[2] = b / 255.0;
            blit_effect_tube_colour(&set, c);
            for (i = 0; i < 3; ++i) printf("%d%c", (int)(c[i] * 255.0 + 0.5), i < 2 ? ' ' : '\\n');
        }
        return 0;
    }
  C
  check(system('tcc', '-I', File.join(ROOT, 'src'), "#{probe}.c", '-o', probe), 'tube probe compile')
  # Each distinct colour of a capture through the rule, for one set.
  predict = lambda do |colours, chroma: 0, phosphor: 0, brightness: 0, contrast: 0|
    input = colours.map { |c| "#{chroma} #{phosphor} #{brightness} #{contrast} #{c.join(' ')}\n" }.join
    output = IO.popen([probe], 'r+') { |io| io.write(input); io.close_write; io.read }
    colours.zip(output.split("\n").map { |l| l.split.map(&:to_i) }).to_h
  end

  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  $server = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  output = get('/v1/displays')['displays'].first.dig('state', 'connector')
  flat('pane-display-1-2', B)
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: W - 1, y: H - 1 })

  # The registry, as the API reports it.
  available = get('/v1/effects')['available'].to_h { |e| [e['name'], e] }
  want = { 'static_noise' => ['amount', 0, 100, true], 'chroma' => ['amount', 0, 100, false],
           'phosphor' => ['color', 0, 0xffffff, false], 'brightness' => ['level', -100, 100, false],
           'contrast' => ['level', -100, 100, false] }
  want.each do |name, (parameter, minimum, maximum, animated)|
    row = available[name]
    check(row && row.values_at('parameter', 'minimum', 'maximum', 'animated', 'class') ==
                 [parameter, minimum, maximum, animated, 'presentation'], "registry row #{name}: #{row}")
  end
  check(available.values.all? { |e| e['minimum'].is_a?(Integer) && e['minimum'] <= 0 },
        'every effect reports its minimum')
  # Strict, signed where the row says so.
  [{ 'brightness' => { 'level' => -101 } }, { 'contrast' => { 'level' => 101 } },
   { 'chroma' => { 'amount' => -1 } }, { 'static_noise' => { 'amount' => -5 } },
   { 'phosphor' => { 'color' => 0x1000000 } }, { 'brightness' => { 'level' => '-' } },
   { 'chroma' => { 'amount' => 50, 'color' => GREEN } }].each do |bad|
    code, = save_effects(output => { '1' => bad })
    check(code == 400, "#{bad} -> #{code}, expected 400")
  end
  saved!(output => { '1' => { 'brightness' => { 'level' => -40 }, 'contrast' => { 'level' => -100 } } })
  pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
  check(pane['effects'] == { 'brightness' => { 'level' => -40 }, 'contrast' => { 'level' => -100 } },
        "negative levels in the API: #{pane['effects']}")
  check(get('/v1/settings').dig('document', 'effects', output, '1', 'brightness') == { 'level' => -40 },
        'a negative level is not saved as written')
  saved!(output => { '1' => {} })

  if GLES
    stripes('pane-display-1-1')
    plain_shot = screenshot
    plain = pane1(plain_shot)
    plain2 = pane2(plain_shot)
    colours = plain.flatten(1).uniq
    check(colours.include?(A) && colours.include?(B), "the stripes are not drawn: #{colours.first(6)}")

    # The unchanged path: every new effect at 0 is no effect at all.
    saved!(output => { '1' => { 'chroma' => { 'amount' => 0 }, 'brightness' => { 'level' => 0 },
                                'contrast' => { 'level' => 0 }, 'static_noise' => { 'amount' => 0 } } })
    zero = screenshot
    check(get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects', 'present_panes').zero?,
          'effects at 0 drew a presentation pass')
    check(pane1(zero) == plain, 'effects at 0 changed pane 1')

    # Colour: every pixel through the shared rule.
    compared = 0
    cases = [{ chroma: 100, phosphor: GREEN }, { chroma: 50, phosphor: AMBER },
             { brightness: -40 }, { brightness: 60 }, { contrast: -50 }, { contrast: 70 },
             { chroma: 60, phosphor: AMBER, brightness: 30, contrast: -20 }]
    cases.each do |set|
      effects = {}
      effects['chroma'] = { 'amount' => set[:chroma] } if set[:chroma]
      effects['phosphor'] = { 'color' => set[:phosphor] } if set[:phosphor]
      effects['brightness'] = { 'level' => set[:brightness] } if set[:brightness]
      effects['contrast'] = { 'level' => set[:contrast] } if set[:contrast]
      saved!(output => { '1' => effects })
      shot = screenshot
      check(get('/v1/diagnostics').dig('render', 'outputs', 0, 'effects', 'present_panes') == 1,
            "#{set}: the presentation pass did not run")
      wanted = predict.call(colours, **set)
      drawn = pane1(shot)
      wrong = []
      plain.each_with_index do |row, y|
        row.each_with_index do |c, x|
          wrong << [x, y, drawn[y][x], wanted[c]] unless near?(drawn[y][x], wanted[c])
          compared += 1
        end
      end
      check(wrong.empty?, "#{set}: #{wrong.size} pixels off the shared rule, first " \
                          "#{wrong.first(4).map { |x, y, got, w| "(#{x},#{y}) #{got} not #{w}" }.join(', ')}")
      check(wanted[A] != A && wanted[B] != B, "#{set}: the rule left the stripes alone: #{wanted}")
      check(pane2(shot) == plain2, "#{set} reached pane 2")
      next unless set == { chroma: 100, phosphor: GREEN }

      # Monochrome in the phosphor: each stripe is the phosphor scaled by
      # its luma, so the two colours have one hue.
      p = [GREEN >> 16, (GREEN >> 8) & 0xff, GREEN & 0xff]
      [A, B].each do |c|
        y = (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255.0
        check(near?(wanted[c], p.map { |v| (v * y).round }), "#{c} is not the phosphor at its luma: #{wanted[c]}")
      end
    end

    # Phosphor alone is chroma 0: full colour, so pane 1 is unchanged.
    saved!(output => { '1' => { 'phosphor' => { 'color' => AMBER } } })
    check(pane1(screenshot) == plain, 'a phosphor without chroma changed the picture')

    # Static noise over dark grey: grey grain within the gain, fresh each
    # frame, on the frame clock.
    flat('pane-display-1-1', GREY)
    saved!(output => { '1' => {} })
    grey_plain = pane1(screenshot)
    check(grey_plain.flatten(1).uniq == [GREY], "the grey pane is not flat: #{grey_plain.flatten(1).uniq.first(4)}")
    fx = -> { get('/v1/diagnostics').dig('render', 'outputs', 0) }
    saved!(output => { '1' => { 'static_noise' => { 'amount' => 100 } } })
    ticks0 = fx.call.dig('effects', 'animation_frames')
    frames = Array.new(3) { pane1(screenshot) }
    top = (NOISE_GAIN * 255).ceil + 1
    frames.each do |frame|
      lifts = frame.flatten(1).map do |px|
        check(px[0] == px[1] && px[1] == px[2], "static noise is not grey grain: #{px}")
        px[0] - GREY[0]
      end
      check(lifts.min >= 0 && lifts.max <= top, "grain outside 0..#{top}: #{lifts.minmax}")
      check(lifts.uniq.size > 64, "too few grain levels in a frame: #{lifts.uniq.size}")
      mean = lifts.sum.to_f / lifts.size
      check((mean - NOISE_GAIN * 255 / 2).abs < 8, "the grain's mean lift is #{mean.round(1)}")
    end
    changed = (0...PANE_H).sum { |y| (0...W).count { |x| frames[0][y][x] != frames[1][y][x] } }
    check(changed > W * PANE_H * 9 / 10, "only #{changed} pixels changed between frames")
    ticks = fx.call.dig('effects', 'animation_frames') - ticks0
    # The clock follows the display by default (ADR 0188, "auto"): the DRM
    # mock's 60 Hz mode.
    refresh = fx.call['refresh_hz'].round
    check(refresh == 60, "the DRM mock's refresh is #{fx.call['refresh_hz']} Hz, not 60")
    check(fx.call.dig('effects', 'animation_hz') == refresh && ticks >= 10,
          "static noise is not on the frame clock: #{ticks} ticks at " \
          "#{fx.call.dig('effects', 'animation_hz')} Hz on a #{refresh} Hz display")
    # A number is a ceiling met every Nth vblank: 30 ticks at 30, 50 every
    # 2nd vblank at 30, 120 at the display's 60.
    [[30, 30], [50, refresh / 2], [120, refresh], [nil, refresh]].each do |value, want|
      Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
      settings = get('/v1/settings')
      server = (settings['document']['server'] || {}).reject { |k, _| k == 'effects_animation_hz' }
      server['effects_animation_hz'] = value if value
      code, reply = request('PUT', '/v1/settings',
                            { document: settings['document'].merge('server' => server) }, settings['revision'])
      check(code == 200, "effects_animation_hz #{value.inspect}: #{code} #{reply}")
      hz = fx.call.dig('effects', 'animation_hz')
      check(hz == want, "effects_animation_hz #{value.inspect}: animation_hz #{hz}, wanted #{want}")
    end
    # Under chroma 100 the grain is the phosphor's colour too.
    saved!(output => { '1' => { 'static_noise' => { 'amount' => 100 }, 'chroma' => { 'amount' => 100 },
                                'phosphor' => { 'color' => AMBER } } })
    tinted = pane1(screenshot).flatten(1)
    check(tinted.all? { |px| px[2] <= 1 && px[1] <= px[0] }, "the grain is not amber under chroma: #{tinted.uniq.first(4)}")
    check(tinted.map(&:first).uniq.size > 32, 'no grain under chroma')
    # Off: the plain picture again, and no frames on an idle screen.
    saved!(output => { '1' => {} })
    sleep 0.6
    check(pane1(screenshot) == grey_plain, 'static noise switched off left grain')
    p0 = fx.call['present_calls']
    sleep 1.5
    p1 = fx.call['present_calls']
    check(p1 - p0 <= 2, "an idle screen presented #{p1 - p0} frames with static noise off")
    check(fx.call.dig('effects', 'animation_hz').zero?, 'animation_hz not 0 with static noise off')
    check(pane2(screenshot) == plain2, 'pane 2 changed')
    puts "TUBE_EFFECTS_PASS backend=gles colour=shared_rule(#{compared}px,#{cases.size}_sets) " \
         'chroma100=phosphor_monochrome phosphor_alone=unchanged zero=no_pass,identical ' \
         "static_noise=grey_grain_0..#{top},fresh_#{changed}px,clock_#{ticks},tinted,off_identical,off_free " \
         'signed=strict,kept isolation=pane'
  else
    stripes('pane-display-1-1')
    plain = pane1(screenshot)
    tube = { 'chroma' => { 'amount' => 100 }, 'phosphor' => { 'color' => GREEN },
             'brightness' => { 'level' => -40 }, 'contrast' => { 'level' => 30 },
             'static_noise' => { 'amount' => 100 } }
    saved!(output => { '1' => tube })
    pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
    check(pane['effects'] == tube && pane['unavailable'] == tube.keys.to_h { |k| [k, 'cpu compositor'] },
          "CPU tube effects not kept and reported unavailable: #{pane}")
    check(pane1(screenshot) == plain, 'the CPU compositor drew a tube effect')
    sleep 0.6
    p0 = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    sleep 1.5
    p1 = get('/v1/diagnostics').dig('render', 'outputs', 0, 'present_calls')
    check(p1 - p0 <= 2, "a CPU-composed idle screen presented #{p1 - p0} frames for static noise")
    puts 'TUBE_EFFECTS_PASS backend=cpu tube=unavailable_kept,undrawn,no_frames signed=strict,kept'
  end
end
