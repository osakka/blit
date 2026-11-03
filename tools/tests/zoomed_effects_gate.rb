#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0204 with ADR 0187: presentation effects on a zoomed display. Two mock
# outputs: display-1, 1024x768, is zoomed to 200% -- logical 512x384 --
# and display-2 is 512x384 at 100%, the size of display-1's logical display.
# Both run the same session (vertical stripes three cells wide) with the
# same effects, so display-2 is what display-1 must show, enlarged.
#
# On gles:
#   * 100% unchanged: display-1 with the static effects at 100% is captured
#     before zooming, and zooming back is byte-identical to it
#   * same, enlarged: with curvature, RGB shift, scanlines and corners, every
#     physical pixel (x, y) of display-1 is display-2's (x / 2, y / 2): the
#     effects are drawn in logical coordinates, as at 100%
#   * drawn: the tube's outside is black at the corners, and stripe edges
#     fringe -- colours that are neither stripe
#   * pixel zoom: every 2x2 block of display-1 is one colour, exactly
#   * animated: the glowing line runs on the zoomed display -- its frame
#     clock ticks, its band moves -- and every frame is still 2x2 blocks
#   * ADR 0207: with chroma, a phosphor, brightness and contrast added to
#     the static effects, display-1 is still display-2 enlarged; static
#     noise changes between frames in whole 2x2 blocks
#   * pointer: a click on the curved, zoomed pane reaches the cell the warp
#     shows at that logical point
#   * diagnostics: nothing is reported unavailable on the zoomed display
# On native (CPU) presentation effects are unavailable whatever the zoom,
# and that is what it checks.
# BLIT_ZOOMED_EFFECTS_SHOT=<path> keeps display-1's 100% capture with
# effects, for comparing builds.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19587'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 1024
H = 768
ZOOM = 200
WL = W * 100 / ZOOM
HL = H * 100 / ZOOM
A = [200, 120, 40].freeze
B = [40, 160, 220].freeze
CURVATURE = 100
GLES = GATE_BACKEND == 'gles'
STATIC = { 'curvature' => { 'amount' => CURVATURE }, 'rgb_shift' => { 'amount' => 50 },
           'scanlines' => { 'intensity' => 50 }, 'corners' => { 'radius_px' => 24 } }.freeze
TUBE = { 'chroma' => { 'amount' => 60 }, 'phosphor' => { 'color' => 0xffb000 },
         'brightness' => { 'level' => 20 }, 'contrast' => { 'level' => -15 } }.freeze

abort("zoomed effects gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  raise "ZOOMED_EFFECTS_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 10)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def screenshot_bytes(display)
  Net::HTTP.get(BASE + "/v1/diagnostics/screenshots/#{display}")
end

# A screenshot once two in a row agree.
def settled(display)
  previous = nil
  Timeout.timeout(10) do
    loop do
      sleep 0.15
      bytes = screenshot_bytes(display)
      return bytes if bytes == previous
      previous = bytes
    end
  end
rescue Timeout::Error
  check(false, "#{display}'s screenshots never settled")
end

# Each row as [r, g, b] triples, top row first.
def rows_of(bytes)
  shot = GateBmp.new(bytes)
  (0...shot.height).map { |y| (0...shot.width).map { |x| shot.pixel(x, y) } }
end

def near?(actual, wanted, tolerance = 2)
  actual.zip(wanted).all? { |a, b| (a - b).abs <= tolerance }
end

def darken(rgb, alpha)
  rgb.map { |c| (c * (255 - alpha) + 127) / 255 }
end

def output(id)
  get('/v1/diagnostics')['render']['outputs'].find { |o| o['display_id'] == id }
end

def set_zoom(zoom)
  code, item = request('PATCH', '/v1/displays/display-1', { 'configuration' => { 'zoom' => zoom } })
  check(code == 200 && item.dig('configuration', 'zoom') == zoom, "PATCH zoom #{zoom}: #{code} #{item}")
end

def save_effects(effects)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document['effects'] = effects
  code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
  check(code == 200, "effects #{effects}: #{code} #{reply}")
end

# The pointer, kept on the display not being captured.
def park(id)
  bounds = get("/v1/displays/#{id}")['bounds']
  # Its middle: at an edge its shadow reaches the display beside it.
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: bounds['x'] + bounds['width'] / 2,
                                        y: bounds['y'] + bounds['height'] / 2 })
end

# The shared warp (src/blit_effects.h): screen q -> content s, pane-normalised.
def warp(k, qx, qy)
  f = (1.0 + k * (qx * qx + qy * qy)) / (1.0 + k)
  [qx * f, qy * f]
end

# Every 2x2 block of a zoomed frame one colour: [blocks, first offenders].
def blocks(rows)
  wrong = []
  (0...H).step(2) do |y|
    (0...W).step(2) do |x|
      c = rows[y][x]
      wrong << [x, y] unless rows[y][x + 1] == c && rows[y + 1][x] == c && rows[y + 1][x + 1] == c
    end
  end
  wrong
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x1', 'BLIT_AUTH_TOKEN' => '',
  'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'))
File.write(env['BLIT_DRM_MOCK_STATE'], "2 0 0 0\n1 #{W} #{H}\n2 #{WL} #{HL}\n")
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-zoomed-effects') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  connectors = %w[display-1 display-2].map { |id| get("/v1/displays/#{id}").dig('state', 'connector') }
  effects_on = ->(set) { save_effects(connectors.to_h { |c| [c, { '1' => set }] }) }
  # Rows of vertical stripes, three cells wide, in A and B: edges for RGB
  # shift, colour for the warp; drawn again on each SIGWINCH, so the frame
  # 100% returns to is the one before. (A session argument is at most 255
  # bytes, so it is a file.)
  script = File.join(root, 'stripes.sh')
  File.write(script, <<~SH)
    a='\\033[48;2;#{A.join(';')}m   '; b='\\033[48;2;#{B.join(';')}m   '
    r=''; i=0; while [ $i -lt 22 ]; do r="$r$a$b"; i=$((i + 1)); done
    draw() {
      printf '\\033[?25l\\033[?7l\\033[48;2;#{A.join(';')}m\\033[2J'
      j=1; while [ $j -le 60 ]; do printf "\\033[$j;1H$r"; j=$((j + 1)); done
    }
    trap draw WINCH
    draw
    while :; do sleep 0.1; done
  SH
  stripes = ['/bin/sh', [script]]
  sessions = {}
  [1, 2].each do |n|
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: stripes[0], arguments: stripes[1], pane_id: "pane-display-#{n}-1" })
    check(code == 201, "session on display-#{n}: #{code} #{session}")
    sessions[n] = session['id']
  end
  backend = GLES ? 'gles' : 'cpu'
  wait_until("the #{backend} compositor is not presenting display-1") do
    output('display-1').dig('compositor', 'active') == backend
  end
  park('display-2')
  wait_until('the stripes never arrived') { GateBmp.new(screenshot_bytes('display-1')).pixel(W / 2, H / 2) != [0, 0, 0] }

  unless GLES
    effects_on.call(STATIC)
    set_zoom(ZOOM)
    sleep 0.5
    reason = output('display-1').dig('effects', 'presentation_unavailable')
    check(reason == 'cpu compositor', "cpu at #{ZOOM}%: presentation effects unavailable because #{reason.inspect}")
    puts 'ZOOMED_EFFECTS_PASS backend=cpu presentation=unavailable(cpu compositor)'
    exit 0
  end

  # 100%, with the static effects: the frame zooming must come back to.
  effects_on.call(STATIC)
  sleep 0.5
  before = settled('display-1')
  File.binwrite(ENV['BLIT_ZOOMED_EFFECTS_SHOT'], before) if ENV['BLIT_ZOOMED_EFFECTS_SHOT']
  check(output('display-1').dig('effects', 'present_panes') == 1, '100%: the presentation pass did not run')

  set_zoom(ZOOM)
  wait_until("#{ZOOM}%: display-1's bounds are not #{WL}x#{HL}") do
    b = get('/v1/displays/display-1')['bounds']
    [b['width'], b['height']] == [WL, HL]
  end
  check(output('display-1').dig('compositor', 'active') == 'gles', "#{ZOOM}%: display-1 is not composed by GLES")

  # Same, enlarged: display-1's physical (x, y) is display-2's (x / 2, y / 2).
  park('display-1')
  reference = rows_of(settled('display-2'))
  park('display-2')
  zoomed = nil
  wrong = nil
  # The session repaints after its SIGWINCH: retry until the frame holds.
  20.times do
    zoomed = rows_of(settled('display-1'))
    wrong = []
    zoomed.each_with_index do |row, y|
      row.each_with_index do |colour, x|
        wrong << [x, y, colour, reference[y / 2][x / 2]] unless near?(colour, reference[y / 2][x / 2])
      end
    end
    break if wrong.empty?
    sleep 0.3
  end
  check(wrong.empty?, "#{ZOOM}%: #{wrong.length} pixels are not the 100% display's enlarged, first " \
                      "#{wrong.first(6).map { |x, y, got, want| "(#{x},#{y}) #{got} not #{want}" }.join(', ')}")
  compared = W * H
  # Drawn, not only enlarged: black outside the tube, and fringes.
  check([[2, 2], [W - 3, 2], [2, H - 3], [W - 3, H - 3]].all? { |x, y| zoomed[y][x] == [0, 0, 0] },
        "#{ZOOM}%: the corners are not the tube's black outside: " \
        "#{[[2, 2], [W - 3, H - 3]].map { |x, y| zoomed[y][x] }}")
  plain = [A, B, darken(A, 127), darken(B, 127), [0, 0, 0]]
  fringed = zoomed[H / 2].count { |c| plain.none? { |p| near?(c, p, 4) } }
  check(fringed >= 20, "#{ZOOM}%: only #{fringed} pixels of the middle row fringe")
  bad = blocks(zoomed)
  check(bad.empty?, "#{ZOOM}%: #{bad.length} 2x2 blocks are not one colour, first #{bad.first(4)}")
  reason = output('display-1').dig('effects', 'presentation_unavailable')
  check(reason == '', "#{ZOOM}%: presentation effects unavailable because #{reason.inspect}")
  check(output('display-1').dig('effects', 'present_panes') == 1, "#{ZOOM}%: the presentation pass did not run")

  # Pointer: a session reporting any-motion (SGR 1006) into a file.
  mouse = File.join(root, 'mouse.txt')
  request('DELETE', "/v1/terminal-sessions/#{sessions[1]}")
  sleep 0.3
  code, session = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', pane_id: 'pane-display-1-1',
                            arguments: ['-c', "stty raw -echo; printf '\\033[?25l\\033[?1003h\\033[?1006h'; exec cat > #{mouse}"] })
  check(code == 201, "mouse session: #{code} #{session}")
  sessions[1] = session['id']
  sleep 0.5
  grid = get('/v1/topology')['panes'].find { |p| p['id'] == 'pane-display-1-1' }['cell_grid']
  bounds = get('/v1/displays/display-1')['bounds']
  k = CURVATURE / 400.0
  # Whole logical pixels: an injected position is.
  [[WL * 4 / 5, HL * 3 / 4], [WL * 3 / 20, HL / 5], [2, 2]].each do |x, y|
    File.write(mouse, '')
    request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: bounds['x'] + x, y: bounds['y'] + y })
    Timeout.timeout(5) { sleep 0.05 until File.read(mouse) =~ /\e\[<\d+;(\d+);(\d+)[Mm]\z/ }
    col, row = File.read(mouse).scan(/\e\[<\d+;(\d+);(\d+)[Mm]/).last.map(&:to_i)
    sx, sy = warp(k, x.to_f / WL * 2 - 1, y.to_f / HL * 2 - 1)
    want_col = (((sx.clamp(-1, 1) + 1) / 2 * WL - grid['origin_x']) / grid['cell_width']).floor + 1
    want_row = (((sy.clamp(-1, 1) + 1) / 2 * HL - grid['origin_y']) / grid['cell_height']).floor + 1
    want = [want_col.clamp(1, grid['columns']), want_row.clamp(1, grid['rows'])]
    check([col, row] == want, "#{ZOOM}%: the pointer at logical #{x},#{y} reached #{col},#{row}, the warp shows #{want}")
  end
  request('DELETE', "/v1/terminal-sessions/#{sessions[1]}")
  sleep 0.3
  code, session = request('POST', '/v1/terminal-sessions',
                          { command: stripes[0], arguments: stripes[1], pane_id: 'pane-display-1-1' })
  check(code == 201, "stripes again: #{code} #{session}")
  park('display-2')

  # Animated: the glowing line, on the zoomed display only.
  save_effects(connectors[0] => { '1' => { 'glowing_line' => { 'intensity' => 100 } } })
  sleep 0.5
  ticks0 = output('display-1').dig('effects', 'animation_frames')
  lit = []
  6.times do
    rows = rows_of(screenshot_bytes('display-1'))
    bad = blocks(rows)
    check(bad.empty?, "#{ZOOM}%, glowing line: #{bad.length} 2x2 blocks are not one colour, first #{bad.first(4)}")
    # The column's stripe colour brightened: the rows the band lights. The
    # band is a third of the pane, so the column's commonest colour is plain.
    column = (0...H).map { |y| rows[y][W / 2 + 1] }
    base = column.tally.max_by { |_, n| n }.first
    lit << (0...H).select { |y| column[y].zip(base).sum { |a, b| a - b } > 6 }
    sleep 0.15
  end
  ticks = output('display-1').dig('effects', 'animation_frames') - ticks0
  check(ticks >= 10, "#{ZOOM}%: the glowing line's clock ticked #{ticks} times")
  check(lit.count { |l| !l.empty? } >= 3 && lit.uniq.size >= 3,
        "#{ZOOM}%: the glowing line is not drawn and moving: lit rows #{lit.map { |l| l.empty? ? '-' : "#{l.first}..#{l.last}" }}")
  check(lit.all? { |l| l.empty? || (l.first.even? && l.last.odd?) },
        "#{ZOOM}%: the band does not cover whole logical rows: #{lit.map { |l| [l.first, l.last] }}")

  # ADR 0207, the tube's colour controls with the static effects: still
  # the 100% display's picture, enlarged; and static noise, re-seeded each
  # frame, one grain per logical pixel.
  effects_on.call(STATIC.merge(TUBE))
  park('display-1')
  reference = rows_of(settled('display-2'))
  park('display-2')
  wrong = nil
  10.times do
    tinted = rows_of(settled('display-1'))
    wrong = []
    tinted.each_with_index do |row, y|
      row.each_with_index do |colour, x|
        wrong << [x, y, colour, reference[y / 2][x / 2]] unless near?(colour, reference[y / 2][x / 2])
      end
    end
    break if wrong.empty?
    sleep 0.3
  end
  check(wrong.empty?, "#{ZOOM}%, colour controls: #{wrong.length} pixels are not the 100% display's enlarged, first " \
                      "#{wrong.first(6).map { |x, y, got, want| "(#{x},#{y}) #{got} not #{want}" }.join(', ')}")
  check(reference[HL / 2][WL / 2] != A && reference[HL / 2][WL / 2] != B,
        "the colour controls did not change the picture: #{reference[HL / 2][WL / 2]}")
  save_effects(connectors[0] => { '1' => { 'static_noise' => { 'amount' => 100 } } })
  sleep 0.5
  grains = Array.new(3) do
    rows = rows_of(screenshot_bytes('display-1'))
    bad = blocks(rows)
    check(bad.empty?, "#{ZOOM}%, static noise: #{bad.length} 2x2 blocks are not one colour, first #{bad.first(4)}")
    sleep 0.15
    rows
  end
  grained = (0...H).step(2).sum { |y| (0...W).step(2).count { |x| grains[0][y][x] != grains[1][y][x] } }
  check(grained > WL * HL / 2, "#{ZOOM}%, static noise: only #{grained} logical pixels changed between frames")

  # 100% again, the static effects: byte-identical to before zooming.
  effects_on.call(STATIC)
  set_zoom(100)
  # display-2 moved back beside the wider display-1; the pointer did not.
  wait_until('display-1 is not 1024 wide again') { get('/v1/displays/display-1').dig('bounds', 'width') == W }
  park('display-2')
  after = nil
  10.times do
    after = settled('display-1')
    break if after == before
    sleep 0.3
  end
  check(after == before, '100% with effects again is not byte-identical to the frame before zooming')

  puts "ZOOMED_EFFECTS_PASS backend=gles zoom=#{ZOOM} logical=#{WL}x#{HL} " \
       "same_as_100_enlarged=#{compared}px(curvature+rgb_shift+scanlines+corners) tube=black " \
       "fringed=#{fringed}px pixel=2x2_blocks glowing_line=ticks_#{ticks},moves,2x2 " \
       "colour_controls=same_as_100_enlarged static_noise=2x2,fresh_#{grained} " \
       'pointer=warped_logical unavailable=none back_to_100=byte_identical'
end
