#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0208: burn-in, the phosphor's persistence. One display split 1x2;
# pane 1 gets the effect, pane 2 never does. Pane 1 shows a white block of
# ten by three cells on black; each line sent to its session moves the
# block between two places, in one write, so the old place goes dark in one
# frame.
#
# On both:
#   * the registry row as GET /v1/effects reports it, and strict settings
# On gles:
#   * unchanged: switched on, nothing has faded yet, so the pane is the
#     plain capture exactly, and an idle screen presents nothing
#   * the fade: after a move, every pixel of the old place is one grey,
#     falling monotonically; its level is the shared C rule
#     (blit_effect_burn_in_levels, compiled into a probe) for the time since
#     the move within the frames around the capture, and the level the
#     diagnostics report (`burn_in_faded`) for the frames around it; the
#     block's new place is white and every other pixel black
#   * the end: black within the stated fade time, the pane byte-identical
#     to the plain capture, and the frame clock stopped: no frames on an
#     idle screen
#   * a long hold does not count: the block held for seconds starts its
#     fade at full brightness when it moves
#   * the amount sets the time: at 30 the trail is gone well before 100's
#   * a layout change mid-fade leaves no trail: no grey anywhere after it
#   * zoomed to 200%: the trail is in whole 2x2 blocks, over exactly the
#     100% trail's pixels enlarged
#   * off: no history is kept, a move leaves no trail, and the pass is gone
#   * isolation: pane 2 is byte-identical throughout
# On native (CPU): saved burn-in is kept, reported unavailable ("cpu
# compositor"), draws nothing and costs no frames.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19623'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384
PANE_H = H / 2
WHITE = [255, 255, 255].freeze
BLACK = [0, 0, 0].freeze
OTHER = [40, 160, 220].freeze
# From the input POST to the frame that shows the move: the shell reading
# its line and writing, the server parsing it, and the next frame.
LATENCY = 0.25
GLES = GATE_BACKEND == 'gles'

abort("burn-in gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  raise "BURN_IN_FAIL #{why}" unless ok
end

def now
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

def capture
  GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
end

def screenshot
  sleep 0.5
  capture
end

def fx
  get('/v1/diagnostics').dig('render', 'outputs', 0)
end

def pane1(shot)
  (0...PANE_H).map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def pane2(shot)
  (PANE_H...H).flat_map { |y| (0...W).map { |x| shot.pixel(x, y) } }
end

def colours(shot, height = H, width = W)
  seen = {}
  (0...height).each { |y| (0...width).each { |x| seen[shot.pixel(x, y)] = true } }
  seen.keys
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

# Ten by three white cells at column 5 or 21 of rows 3..5, on black. Each
# line read moves the block: the erase and the draw are one write.
BLOCK = "stty -echo; k='\\033[48;2;0;0;0m'; w='\\033[48;2;255;255;255m'; s='          '; " \
        'at() { o=""; r=3; while [ $r -le 5 ]; do o="$o\\033[$r;${1}H$2$s"; r=$((r + 1)); done; ' \
        'printf "%s" "$o"; }; printf "\\033[?25l$k\\033[2J"; c=5; printf "$(at $c "$w")"; ' \
        'while read x; do if [ $c = 5 ]; then n=21; else n=5; fi; ' \
        'printf "$(at $c "$k")$(at $n "$w")"; c=$n; done'

def move!
  code, = request('POST', "/v1/terminal-sessions/#{$sessions['pane-display-1-1']}/input",
                  { mode: 'raw', data: "m\n" })
  check(code == 202, "input: #{code}")
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

def layout!(output, columns, rows)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document['display_layouts'] = { output => { 'columns' => columns, 'rows' => rows } }
  code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
  check(code == 200, "layout #{columns}x#{rows}: #{code} #{reply}")
end

def animation_hz!(hz)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  server = (document['server'] || {}).reject { |k, _| k == 'effects_animation_hz' }
  server['effects_animation_hz'] = hz if hz
  document['server'] = server
  code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
  check(code == 200, "effects_animation_hz #{hz}: #{code} #{reply}")
end

def set_zoom(zoom)
  code, item = request('PATCH', '/v1/displays/display-1', { 'configuration' => { 'zoom' => zoom } })
  check(code == 200 && item.dig('configuration', 'zoom') == zoom, "PATCH zoom #{zoom}: #{code} #{item}")
end

def present_calls_idle(seconds)
  p0 = fx['present_calls']
  sleep seconds
  fx['present_calls'] - p0
end

Dir.mktmpdir('blit-burn-in') do |root|
  # The shared rule, from src/blit_effects.h: "amount elapsed_us" -> the
  # fade time and the levels faded over that time from a fresh carry.
  probe = File.join(root, 'burn_probe')
  File.write("#{probe}.c", <<~C)
    #include <stdio.h>
    #include <string.h>
    #include "blit_effects.h"
    int main(void) {
        int amount;
        long long elapsed;
        while (scanf("%d %lld", &amount, &elapsed) == 2) {
            BlitEffectSet set;
            long long carry = 0, fade;
            memset(&set, 0, sizeof(set));
            set.value[BLIT_EFFECT_BURN_IN] = amount;
            fade = blit_effect_burn_in_us(&set);
            printf("%lld %d\\n", fade, blit_effect_burn_in_levels(fade, elapsed, &carry));
        }
        return 0;
    }
  C
  check(system('tcc', '-I', File.join(ROOT, 'src'), "#{probe}.c", '-o', probe), 'burn probe compile')
  rule = lambda do |amount, seconds|
    out = IO.popen([probe], 'r+') { |io| io.write("#{amount} #{(seconds * 1e6).round}\n"); io.close_write; io.read }
    out.split.map(&:to_i)
  end
  fade100 = rule.call(100, 0).first / 1e6
  fade30 = rule.call(30, 0).first / 1e6
  check(rule.call(0, 0).first.zero? && (fade100 - 1.6).abs < 1e-9 && fade30 > 0.5 && fade30 < 0.65,
        "fade times: 0 -> #{rule.call(0, 0).first}, 30 -> #{fade30}, 100 -> #{fade100}")
  check(rule.call(100, fade100 / 2).last == 127 && rule.call(100, fade100).last == 255 &&
        rule.call(100, 0.001).last.zero?, 'the rule: half the fade time is half the levels')

  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  $server = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  output = get('/v1/displays')['displays'].first.dig('state', 'connector')
  run('pane-display-1-2', "printf '\\033[?25l\\033[48;2;#{OTHER.join(';')}m\\033[2J'; sleep 600")
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: W - 1, y: H - 1 })

  # The registry, as the API reports it.
  row = get('/v1/effects')['available'].find { |e| e['name'] == 'burn_in' }
  check(row && row.values_at('parameter', 'minimum', 'maximum', 'animated', 'class', 'title') ==
               ['amount', 0, 100, false, 'presentation', 'Burn-in'], "registry row burn_in: #{row}")
  [{ 'burn_in' => { 'amount' => -1 } }, { 'burn_in' => { 'amount' => 101 } },
   { 'burn_in' => { 'level' => 50 } }, { 'burn_in' => { 'amount' => 50, 'level' => 5 } }].each do |bad|
    code, = save_effects(output => { '1' => bad })
    check(code == 400, "#{bad} -> #{code}, expected 400")
  end

  File.write(File.join(root, 'block.sh'), BLOCK)
  run('pane-display-1-1', "exec /bin/sh #{File.join(root, 'block.sh')}")
  plain_shot = screenshot
  plain_a = pane1(plain_shot)
  plain2 = pane2(plain_shot)
  mask_a = []
  plain_a.each_with_index { |r, y| r.each_with_index { |c, x| mask_a << [x, y] if c == WHITE } }
  check(mask_a.size == 10 * 3 * 8 * 16 && plain_a.flatten(1).uniq.sort == [BLACK, WHITE],
        "the block is not drawn: #{mask_a.size} white, colours #{plain_a.flatten(1).uniq.first(4)}")
  move!
  plain_b = pane1(screenshot)
  mask_b = []
  plain_b.each_with_index { |r, y| r.each_with_index { |c, x| mask_b << [x, y] if c == WHITE } }
  check(mask_b.size == mask_a.size && (mask_a & mask_b).empty? &&
        plain_b.flatten(1).uniq.sort == [BLACK, WHITE], 'the block did not move cleanly without burn-in')

  if GLES
    trail = ->(shot, mask) { mask.map { |x, y| shot.pixel(x, y) }.uniq }
    # Switched on: a history starts as the picture, so nothing has faded.
    move!
    sleep 0.5
    saved!(output => { '1' => { 'burn_in' => { 'amount' => 100 } } })
    on = screenshot
    check(pane1(on) == plain_a, 'switching burn-in on changed the picture')
    e = fx['effects']
    check(e['present_panes'] == 1 && e['burn_in_panes'] == 1 && e['burn_in_faded'] == 255,
          "burn-in on: #{e.slice('present_panes', 'burn_in_panes', 'burn_in_faded')}")
    sleep 0.3
    idle = present_calls_idle(1.5)
    check(idle <= 2 && fx.dig('effects', 'animation_hz').zero?,
          "burn-in with nothing to fade presented #{idle} frames")

    # The fade, after the block was held for seconds.
    sleep 1.0
    samples = []
    t0 = now
    move!
    loop do
      before = fx['effects']
      tb = now - t0
      shot = capture
      ta = now - t0
      after = fx['effects']
      levels = trail.call(shot, mask_a)
      # Captured before the move reached the screen: not a sample yet.
      next if tb < LATENCY && levels == [WHITE] && trail.call(shot, mask_b) == [BLACK]

      check(levels.size == 1 && levels[0].uniq.size == 1,
            "at #{tb.round(3)} s the old place is not one grey: #{levels.first(4)}")
      check(trail.call(shot, mask_b) == [WHITE], "at #{tb.round(3)} s the block's new place is not white")
      samples << { tb: tb, ta: ta, level: levels[0][0], before: before, after: after }
      break if ta > fade100 + LATENCY + 0.3
    end
    fading = samples.select { |s| s[:level].positive? && s[:level] < 255 }
    check(fading.size >= 5, "no gradual fade: #{samples.map { |s| s[:level] }}")
    samples.each_cons(2) do |a, b|
      check(b[:level] <= a[:level], "the trail brightened: #{a[:level]} then #{b[:level]}")
    end
    rule_checked = 0
    reported_checked = 0
    samples.each do |s|
      # The shared rule, for the time since the move: no more than the time
      # since the input was sent, no less than that less the latency.
      most = rule.call(100, s[:ta]).last
      least = rule.call(100, [s[:tb] - LATENCY, 0].max).last
      faded = 255 - s[:level]
      check(faded >= least - 1 && faded <= most + 1,
            "at #{s[:tb].round(3)}..#{s[:ta].round(3)} s the trail faded #{faded}, the rule #{least}..#{most}")
      rule_checked += 1
      # The level the server reports for the frames around the capture.
      next unless s[:before]['burn_in_faded'] <= s[:after]['burn_in_faded'] &&
                  s[:after]['burn_in_faded'] < 255

      check(faded >= s[:before]['burn_in_faded'] - 1 && faded <= s[:after]['burn_in_faded'] + 1,
            "at #{s[:tb].round(3)} s the trail faded #{faded}, reported " \
            "#{s[:before]['burn_in_faded']}..#{s[:after]['burn_in_faded']}")
      reported_checked += 1
    end
    check(reported_checked >= 3, "only #{reported_checked} captures inside reported fades")
    first = samples.first
    check(255 - first[:level] <= rule.call(100, first[:ta]).last + 1,
          "the held block did not start its fade at full: #{first[:level]} at #{first[:ta].round(3)} s")
    late = samples.select { |s| s[:tb] > fade100 + LATENCY }
    check(!late.empty? && late.all? { |s| s[:level].zero? },
          "not black #{fade100} s after the move: #{late.map { |s| s[:level] }}")
    # At the display's refresh: "auto", the default (ADR 0188).
    refresh = fx['refresh_hz'].round
    check(samples.any? { |s| s[:after]['animation_hz'] == refresh },
          "the frame clock did not run at the display's #{refresh} Hz while fading")
    # The end: the plain picture, and no clock.
    sleep 0.3
    done = capture
    check(pane1(done) == plain_b, 'the faded pane is not the plain capture')
    check(fx.dig('effects', 'burn_in_faded') == 255 && fx.dig('effects', 'animation_hz').zero?,
          "still fading after the fade: #{fx['effects'].slice('burn_in_faded', 'animation_hz')}")
    idle = present_calls_idle(1.5)
    check(idle <= 2, "an idle screen presented #{idle} frames after the fade")
    check(pane2(done) == plain2, 'pane 2 changed during the fade')
    # Unwatched -- the captures above were frames of their own -- the
    # frame clock alone carries the fade, at the display's refresh ("auto",
    # ADR 0188), and stops after it.
    frames0 = fx.dig('effects', 'animation_frames')
    move!
    sleep fade100 + LATENCY + 0.3
    ticks = fx.dig('effects', 'animation_frames') - frames0
    # A third of the rate at least: ticks only fill gaps, aligned to the
    # vblank, and llvmpipe is slow; none at all without the clock.
    check(ticks >= fade100 * refresh / 3 && ticks <= (fade100 + LATENCY) * refresh + 3,
          "#{ticks} animation frames for a #{fade100} s fade at #{refresh} Hz")
    idle = present_calls_idle(1.5)
    check(idle <= 2 && fx.dig('effects', 'animation_hz').zero?,
          "an idle screen presented #{idle} frames after the unwatched fade")
    check(pane1(capture) == plain_a, 'the unwatched fade did not end on the plain picture')

    # A change while a trail still fades: what the pane showed until the
    # change starts its own fade from full at that frame, however long ago
    # the frame before it was. At 10 Hz that is up to a tenth of a second,
    # 16 levels, that fading after the new picture would lose.
    animation_hz!(10)
    over = 0
    3.times do
      move!
      sleep 0.4
      move!
      t1 = now
      while now - t1 < 0.6
        before = fx['effects']
        shot = capture
        after = fx['effects']
        level = trail.call(shot, mask_b)
        check(level.size == 1 && level[0].uniq.size == 1, "the second trail is not one grey: #{level.first(3)}")
        # Both readings from this trail's fade, not the one before it.
        next unless before['burn_in_faded'] <= after['burn_in_faded'] &&
                    after['burn_in_faded'] < 255 && level[0][0] < 255

        faded = 255 - level[0][0]
        check(faded >= before['burn_in_faded'] - 1 && faded <= after['burn_in_faded'] + 1,
              "a trail laid during a fade faded #{faded}, reported " \
              "#{before['burn_in_faded']}..#{after['burn_in_faded']}")
        over += 1
      end
      sleep fade100 + 0.3
    end
    check(over >= 6, "only #{over} captures of trails laid during a fade")
    animation_hz!(nil)

    # The amount sets the time: 30 is gone in under 0.6 s.
    saved!(output => { '1' => { 'burn_in' => { 'amount' => 30 } } })
    sleep 0.5
    t0 = now
    move!
    sleep fade30 / 3
    mid = capture
    mid_level = trail.call(mid, mask_a)
    sleep [fade30 + LATENCY + 0.15 - (now - t0), 0].max
    gone = capture
    check(mid_level.size == 1 && mid_level[0][0].positive? && mid_level[0][0] < 255,
          "no trail a third of the way into amount 30's fade: #{mid_level}")
    check(pane1(gone) == plain_b, "amount 30 not black #{fade30.round(3)} s after the move")

    # A layout change mid-fade: the trail goes with the old layout.
    saved!(output => { '1' => { 'burn_in' => { 'amount' => 100 } } })
    sleep 0.5
    move!
    sleep 0.4
    check(trail.call(capture, mask_b).then { |l| l.size == 1 && l[0][0].positive? && l[0][0] < 255 },
          'no trail before the layout change')
    layout!(output, 2, 1)
    sleep 0.4
    after_layout = capture
    check(after_layout.pixel(W - 1, 0) == OTHER && after_layout.pixel(0, H - 1) != OTHER,
          'the layout did not change to 2x1')
    # The burning pane, now the left column, covers the old trail's place;
    # the pointer, in the bottom-right corner, is the only other grey.
    check(mask_b.all? { |x, _| x < W / 2 }, 'the old trail is not inside the new pane 1')
    greys = colours(after_layout, H, W / 2) - [BLACK, WHITE]
    check(greys.empty?, "a trail crossed the layout change: #{greys.first(4)}")
    check(fx.dig('effects', 'burn_in_faded') == 255, 'the new layout started with something fading')
    layout!(output, 1, 2)
    sleep 1.0
    check(pane1(capture) == plain_a, 'back to 1x2, the picture is not the plain one')

    # Zoomed to 200%: the 100% trail, enlarged.
    set_zoom(200)
    sleep 1.0
    zoomed_plain = capture
    zoomed_white = []
    (0...PANE_H).each { |y| (0...W).each { |x| zoomed_white << [x, y] if zoomed_plain.pixel(x, y) == WHITE } }
    enlarged = ->(mask) { mask.select { |x, y| x < W / 2 && y < PANE_H / 2 }.flat_map { |x, y| [[2 * x, 2 * y], [2 * x + 1, 2 * y], [2 * x, 2 * y + 1], [2 * x + 1, 2 * y + 1]] }.sort }
    check(zoomed_white.sort == enlarged.call(mask_a), 'the zoomed block is not the 100% block enlarged')
    move!
    sleep 0.4
    zoomed = capture
    grey = []
    blocks = 0
    (0...PANE_H).step(2) do |y|
      (0...W).step(2) do |x|
        c = zoomed.pixel(x, y)
        check([zoomed.pixel(x + 1, y), zoomed.pixel(x, y + 1), zoomed.pixel(x + 1, y + 1)].all? { |o| o == c },
              "the zoomed 2x2 block at #{x},#{y} is not one colour")
        blocks += 1
        grey.push([x, y], [x + 1, y], [x, y + 1], [x + 1, y + 1]) unless c == BLACK || c == WHITE
      end
    end
    zoom_level = grey.map { |x, y| zoomed.pixel(x, y) }.uniq
    check(grey.sort == enlarged.call(mask_a) && zoom_level.size == 1 && zoom_level[0].uniq.size == 1,
          "the zoomed trail is not the 100% trail enlarged: #{grey.size} grey of #{enlarged.call(mask_a).size}, #{zoom_level.first(3)}")
    set_zoom(100)
    sleep 1.0

    # Off: no history, no pass, no trail. Pane 2's terminal was resized by
    # the layout and zoom changes, so its picture is taken again.
    plain2 = pane2(capture)
    saved!(output => { '1' => {} })
    sleep 0.5
    move!
    off = screenshot
    e = fx['effects']
    check(e['present_panes'].zero? && e['burn_in_panes'].zero?,
          "burn-in off kept a pass or a history: #{e.slice('present_panes', 'burn_in_panes')}")
    check([plain_a, plain_b].include?(pane1(off)), 'burn-in off left a trail')
    check(pane2(off) == plain2, 'pane 2 changed with burn-in switched off')
    puts "BURN_IN_PASS backend=gles fade=rule(#{rule_checked}_captures,#{fading.size}_grey)," \
         "reported(#{reported_checked}),monotonic,black_by_#{fade100}s,clock_#{ticks}_then_idle " \
         "on=identical,no_frames held=full_start over_fading=full_start(#{over}) amount30=gone_by_#{fade30.round(3)}s " \
         "layout=reset zoomed=2x2(#{blocks}_blocks),100%_enlarged off=no_history,no_trail isolation=pane"
  else
    saved!(output => { '1' => { 'burn_in' => { 'amount' => 100 } } })
    pane = get('/v1/effects')['panes'].find { |p| p['pane_id'] == 'pane-display-1-1' }
    check(pane['effects'] == { 'burn_in' => { 'amount' => 100 } } &&
          pane['unavailable'] == { 'burn_in' => 'cpu compositor' },
          "CPU burn-in not kept and reported unavailable: #{pane}")
    move!
    check(pane1(screenshot) == plain_a, 'the CPU compositor drew a trail')
    sleep 0.6
    idle = present_calls_idle(1.5)
    check(idle <= 2, "a CPU-composed idle screen presented #{idle} frames for burn-in")
    puts 'BURN_IN_PASS backend=cpu burn_in=unavailable_kept,undrawn,no_frames'
  end
end
