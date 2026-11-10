#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0212 through a real server and its compositor (BLIT_GATE_BACKEND
# native or gles), at the owner's 11x22 cell (Hack at 22 px):
#
# - A session prints the 20 controls twice in an orange foreground on a blue
#   background: once with no underline colour (SGR 58 unset: white) and once
#   with SGR 58 a green. Every pixel of every glyph cell is what the design
#   (tools/fonts/draw-control-glyphs.py) says: Chalk in the foreground;
#   Slate's and the capsule's roles in the cell's pens -- K the foreground,
#   L the underline colour (white when unset), M each channel
#   (fg + bg) / 2 -- and the background wherever nothing is drawn.
# - The capsule's green is not in the glyph: drawn with a green foreground,
#   a grey one, on a light panel and on a dark one, its track is the
#   foreground, its knob the underline colour, its smoothed edge the mean of
#   the foreground and the background, and every other pixel the background
#   -- no pixel of any other colour; the same pixels are the track, knob and
#   edge whichever foreground it is drawn in. A few pixels are written out
#   here from the geometry: the knob's centre, a track pixel clear of the
#   knob, the corner outside the track.

require 'json'
require 'net/http'
require 'open3'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19571'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
FG = [255, 200, 0].freeze
BG = [40, 80, 160].freeze
LINE = [10, 220, 120].freeze
WHITE = [255, 255, 255].freeze
FIRST = 0x10029A
GREEN = [52, 199, 89].freeze
GREY = [206, 206, 212].freeze
LIGHT = [245, 245, 247].freeze
DARK = [38, 38, 42].freeze
DARK_GREEN = [48, 209, 88].freeze

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def check(ok, why)
  raise "CONTROL_STYLE_FAIL backend=#{GATE_BACKEND} #{why}" unless ok
end

def u(cp) = [cp].pack('U')
def sgr(text) = "\\033[#{text}m"
def rgb(c) = c.join(';')
def mix(fg, bg) = fg.zip(bg).map { |a, b| (a + b) / 2 }

checked = 0
Dir.mktmpdir('blit-control-style') do |root|
  env = gate_display_env.merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    code, = request('PUT', '/v1/fonts', { font_id: 'hack', size_px: 22 })
    check(code == 200, "font -> #{code}")
    sleep 0.5
    all = (0...20).map { |i| u(FIRST + i) }.join
    plain = sgr("0;38;2;#{rgb(FG)};48;2;#{rgb(BG)}")
    lined = sgr("0;38;2;#{rgb(FG)};48;2;#{rgb(BG)};58;2;#{rgb(LINE)}")
    capsule = ->(fg, bg) { sgr("0;38;2;#{rgb(fg)};48;2;#{rgb(bg)};58;2;#{rgb(WHITE)}") + (14..19).map { |i| u(FIRST + i) }.join }
    # Capsule rows: green and grey on a light panel, green on a dark one, and
    # the default (unset) underline colour on the dark one.
    script = "printf '\\033[?25l#{plain}\\033[2J" \
             "\\033[2;3H#{plain}#{all}\\033[3;3H#{lined}#{all}" \
             "\\033[5;3H#{capsule.call(GREEN, LIGHT)}\\033[6;3H#{capsule.call(GREY, LIGHT)}" \
             "\\033[7;3H#{capsule.call(DARK_GREEN, DARK)}" \
             "\\033[8;3H#{sgr("0;38;2;#{rgb(DARK_GREEN)};48;2;#{rgb(DARK)}")}#{(14..19).map { |i| u(FIRST + i) }.join}" \
             "#{plain}'; sleep 600"
    path = File.join(root, 'scene.sh')
    File.write(path, script + "\n")
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: '/bin/sh', display_id: 'display-1', arguments: [path] })
    check(code == 201, "session -> #{code} #{session}")
    sleep 1.5
    grid = request('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
    cw, ch = grid['cell_width'], grid['cell_height']
    check([cw, ch] == [11, 22], "hack at 22 px is #{cw}x#{ch}, not the owner's 11x22")
    shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
    px = ->(col, row, dx, dy) { shot.pixel(grid['origin_x'] + col * cw + dx, grid['origin_y'] + row * ch + dy) }

    design, err, status = Open3.capture3('python3', File.join(ROOT, 'tools/fonts/draw-control-glyphs.py'),
                                         '--reference', cw.to_s, ch.to_s)
    check(status.success?, "the design does not run: #{err}")
    lines = design.lines
    check(lines.size == 20, "the design has #{lines.size} glyphs, not 20")

    # Every glyph, pixel for pixel: row 1 with SGR 58 unset (white), row 2 with
    # it green. Pens: K the foreground, L the line pen, M the mean of the
    # foreground and the background.
    [[1, WHITE], [2, LINE]].each do |row, line|
      pens = { 'K' => FG, 'L' => line, 'M' => mix(FG, BG) }
      20.times do |i|
        _, fmt, data = lines[i].split
        ch.times do |dy|
          cw.times do |dx|
            want = fmt == 'coverage' ? (data[2 * (dy * cw + dx), 2].hex == 255 ? FG : BG) : pens.fetch(data[dy * cw + dx], BG)
            got = px.call(2 + i, row, dx, dy)
            check(got == want, format('U+%X on row %d pixel %d,%d is %s, the design says %s', FIRST + i, row, dx, dy, got.inspect, want.inspect))
            checked += 1
          end
        end
      end
    end

    # The capsule on its own: six cells a row (the off capsule in screen
    # columns 2-4, the on one in 5-7), in each of its four draws, on the
    # screen rows 4 to 7 (zero-based).
    draws = [[4, GREEN, LIGHT], [5, GREY, LIGHT], [6, DARK_GREEN, DARK], [7, DARK_GREEN, DARK]]
    classes = draws.map do |row, fg, bg|
      line = WHITE
      m = mix(fg, bg)
      map = {}
      6.times do |cell|
        ch.times do |dy|
          cw.times do |dx|
            colour = px.call(2 + cell, row, dx, dy)
            kind = { fg => 'K', line => 'L', m => 'M', bg => '.' }[colour]
            check(kind, "capsule row #{row} cell #{cell} pixel #{dx},#{dy} is #{colour.inspect}: not the foreground #{fg}, " \
                        "the line pen #{line}, their mean #{m} or the background #{bg}")
            map[[cell, dx, dy]] = kind
            checked += 1
          end
        end
      end
      # A few pixels, from the geometry alone: the track runs from x 1 to 31
      # and y 2 to 19 of the three cells (33 px); the knob, 14 px across and
      # centred on y 11, is centred on x 10 when off and on x 23 when on.
      check(px.call(2, row, 10, 10) == line, "the off capsule's knob centre (10,10) is #{px.call(2, row, 10, 10)}, not the line pen #{line}")
      check(px.call(7, row, 1, 10) == line, "the on capsule's knob centre (23,10) is #{px.call(7, row, 1, 10)}, not the line pen #{line}")
      check(px.call(4, row, 6, 10) == fg, "the off capsule's track at (28,10), clear of the knob, is #{px.call(4, row, 6, 10)}, not #{fg}")
      check(px.call(5, row, 6, 10) == fg, "the on capsule's track at (6,10), clear of the knob, is #{px.call(5, row, 6, 10)}, not #{fg}")
      check(px.call(2, row, 0, 0) == bg && px.call(4, row, 10, 21) == bg && px.call(2, row, 0, 10) == bg,
            "the capsule's corners and the column before its end are not the background #{bg}")
      check([[3, 5, 2], [3, 5, 19]].all? { |c, dx, dy| px.call(c, row, dx, dy) == fg } &&
            [[3, 5, 1], [3, 5, 20]].all? { |c, dx, dy| px.call(c, row, dx, dy) == bg },
            "the capsule's track is not rows 2-19 at x 16")
      map
    end
    # The colour is not in the glyph: the green and the grey capsule on the
    # same panel have the same track, knob and edge pixels.
    check(classes[0] == classes[1], 'the green and the grey capsule differ in more than their colour')
    check(classes[2] == classes[3], 'the capsule with SGR 58 set white and unset differ')
    check(classes[0] == classes[2], 'the capsule differs between the light and the dark panel')
    kinds = classes[0].values.tally
    check(%w[K L M .].all? { |k| kinds[k].to_i.positive? }, "the capsule lacks a pen: #{kinds}")
    request('DELETE', "/v1/terminal-sessions/#{session['id']}")
    puts "CONTROL_STYLE_PASS backend=#{GATE_BACKEND} cell=#{cw}x#{ch} glyphs=20 pixels=#{checked} " \
         'chalk=foreground slate,capsule=cell_pens(fg,line,mix) sgr58=unset:white,set capsule=track:fg,knob:line,edge:mix ' \
         'green_not_in_glyph=same_pixels_green,grey,light,dark'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
