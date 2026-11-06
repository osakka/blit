#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0210, 0211 through a real server and its compositor
# (BLIT_GATE_BACKEND native or gles), at the owner's 11x22 cell (Hack at
# 22 px):
#
# - A session prints the 70 window-chrome glyphs in an orange foreground on
#   a blue background: Chalk, then Slate twice, without and with an
#   underline colour. Every pixel of every glyph cell is what the design
#   (tools/fonts/draw-window-glyphs.py) says: Chalk in the foreground;
#   Slate's roles in the cell's pens -- K the foreground, L the underline
#   colour (white when unset), M each channel (fg + bg) / 2 -- and the
#   background wherever nothing is drawn.
# - The outer-edge frame (ADR 0214, U+1002AE-1002BA): its thirteen glyphs on a
#   row, every pixel the design's (the frame cells' background the window's
#   face, the line the foreground; Chalk's title ends in the foreground,
#   Slate's in the cell's pens); then two whole windows built from them, a
#   Slate one with a 1 px shadow and a Chalk one with a 2 px shadow, each
#   cell a design glyph or the plain face: the line on the window's
#   outermost edge on every side, the stripes running from the title-end
#   cells into their neighbours with no gap (and one clear pixel beside the
#   outer line), and the shadow, SGR 66 with inset 0, starting in the cell
#   beyond the frame at the pixel next to its line.
# - Inactive windows (0.3.45), Chalk and Slate, built from the plain title
#   ends: the bar with no stripes anywhere (the end cells, the plain `title`
#   cells between), the outer line on both edges, the body and bottom as in
#   the active windows.
# - SGR 66 (ADR 0211): each shadow kind at each width is exactly its strips,
#   in the foreground or the underline colour, under the cell's glyph (a
#   frame glyph's line stays on top); SGR 67 and SGR 0 end it.
# - SGR 51 draws a 1 px line along a cell's top and bottom, 51:3 a 3 px
#   one, in the underline colour (SGR 58) if set, else the foreground; SGR
#   54 and SGR 0 end it, and a framed blank still draws.
# - A Slate dialog (ADR 0211) on the wallpaper, SGR 58 its face: every pixel
#   of its border cells is the foreground (the lines), the face (margin and
#   gap) or the wallpaper (everything beyond the 7 px border): no fixed
#   colour, no halo. Button caps on the dialog's face, SGR 58 the button's
#   face: the corners outside the ring and the 2 px inset show the dialog's
#   face, the face inside is the button's, the shade is half foreground,
#   half the dialog's face, and only down and right.
# - A button's left end, its framed label and its right end make one
#   outline: the top and bottom rows run unbroken from end to end, thin and
#   default.
# - A Slate title framed 51:2 has the bar glyph's frame line, highlight,
#   shade and rule rows in the cell's pens; 51:4 draws the same.

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
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19561'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
FG = [255, 200, 0].freeze
BG = [40, 80, 160].freeze
RED = [255, 0, 0].freeze
LINE = [10, 220, 120].freeze
FACE = [120, 30, 150].freeze
BTN = [230, 130, 20].freeze
FIRST = 0x100231
# ADR 0214: the outer-edge frame, and the windows built from it.
STATE_FIRST = 0x1002BB # ADR 0219: the pressed boxes and grow cells, ten glyphs
MIN_FIRST = 0x1002CD # ADR 0222: the minimise box and the box pressed, Chalk's then Slate's
FRAME_FIRST = 0x1002AE
INK = [0, 0, 0].freeze
BAR = [214, 214, 214].freeze
HI = [255, 255, 255].freeze
SHADOW = [255, 200, 0].freeze
WIN_COLS = 10
WIN_BODY = 3
MIX = FG.zip(BG).map { |a, b| (a + b) / 2 }.freeze
# SGR 66's strips, written out here from ADR 0211, not read from Blit: for
# kind k, width t (1 or 2) and inset i (1, just outside a frame glyph's
# line; 0, at the cell's edge) in a w x h cell, [x0, x1, y0, y1] rectangles.
def shadow_rects(kind, t, w, h, inset = 1)
  i = inset
  case kind
  when 1 then [[i, i + t, 0, h]]
  when 2 then [[i, i + t, 2, h]]
  when 3 then [[0, w, i, i + t]]
  when 4 then [[2 - i, w, i, i + t]]
  when 5 then [[i, i + t, 0, i + t], [0, i + t, i, i + t]]
  end
end
SHADOWS = [1, 2, 3, 4, 5].product([1, 2], [1, 0]).freeze

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
  raise "WINDOW_STYLE_FAIL backend=#{GATE_BACKEND} #{why}" unless ok
end

def u(cp) = [cp].pack('U')
def sgr(text) = "\\033[#{text}m"
def rgb(c) = c.join(';')

checked = 0
Dir.mktmpdir('blit-window-style') do |root|
  env = gate_display_env.merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    code, = request('PUT', '/v1/fonts', { font_id: 'hack', size_px: 22 })
    check(code == 200, "font -> #{code}")
    sleep 0.5
    colours = sgr("38;2;#{rgb(FG)};48;2;#{rgb(BG)}")
    rows = [(0...35).map { |i| u(FIRST + i) }.join, (0...35).map { |i| u(FIRST + 35 + i) }.join]
    bl, br = u(FIRST + 31), u(FIRST + 32)
    dl, dr = u(FIRST + 33), u(FIRST + 34)
    # Row 5: a button, a default button, a red-framed label, framing ended
    # by 54 and by 0. Row 6: a framed blank, framing after a reset, and a
    # 51:5 blank (the rule under a note's strip: the bottom line only).
    buttons = "#{bl}#{sgr('51')} OK #{sgr('54')}#{br} #{dl}#{sgr('51:3')} Go #{sgr('54')}#{dr} " \
              "#{sgr('58;2;255;0;0;51')}red#{sgr('59;54')} #{sgr('51')}A#{sgr('54')}B#{sgr('51')}C#{sgr('0')}D"
    # Row 7: a Slate title in the Pewter scheme (bar 214, ink black): the
    # title glyph, text framed 51:2, the glyph, text framed 51:4, the glyph.
    title = u(0x100254 + 5)
    titles = "#{sgr('0;38;2;0;0;0;48;2;214;214;214')}#{title}#{sgr('51:2')}Ti#{sgr('54')}#{title}" \
             "#{sgr('51:4')}Ti#{sgr('54')}#{title}"
    # Row 8: each shadow kind and width on a blank, an unshadowed blank
    # after each (SGR 67); a red shadow (SGR 58); one ended by SGR 0; and a
    # Chalk frame-right glyph over a red right:2 shadow.
    # The outer-edge frame (ADR 0214). Row 11: the nine glyphs, the frame
    # cells on the window's face (FACE), Chalk's title ends on the bar and
    # Slate's on the bar with SGR 58 the highlight. Rows 13-18 a Slate
    # window with a 1 px shadow beyond it (inset 0), rows 21-26 a Chalk one
    # with a 2 px shadow.
    face_pens = sgr("0;38;2;#{rgb(INK)};48;2;#{rgb(FACE)}")
    bar_pens = sgr("0;38;2;#{rgb(INK)};48;2;#{rgb(BAR)};58;2;#{rgb(HI)}")
    nine = (0...13).map { |i| (i < 5 ? face_pens : bar_pens) + u(FRAME_FIRST + i) }.join + sgr('0') + colours
    at = ->(x, y, text) { "\\033[#{y + 1};#{x + 1}H#{text}" }
    win_text = lambda do |x0, y0, slate, thick|
      w = WIN_COLS
      bar = bar_pens
      stripe = u((slate ? 0x100254 : 0x100231))
      out = +''
      title = [FRAME_FIRST + (slate ? 7 : 5)] + [stripe.ord] * 3 + [(slate ? 0x100254 : 0x100231) + 3] +
              [(slate ? 0x100254 : 0x100231) + 5] * 2 + [(slate ? 0x100254 : 0x100231) + 4] + [stripe.ord] + [FRAME_FIRST + (slate ? 8 : 6)]
      out << at.call(x0, y0, bar + title.map { |c| u(c) }.join)
      (1..WIN_BODY).each do |r|
        out << at.call(x0, y0 + r, face_pens + u(FRAME_FIRST) + ' ' * (w - 2) + u(FRAME_FIRST + 1))
      end
      by = y0 + WIN_BODY + 1
      out << at.call(x0, by, face_pens + u(FRAME_FIRST + 2) + u(FRAME_FIRST + 3) * (w - 2) + u(FRAME_FIRST + 4))
      # The shadow: in the cells beyond the frame, inset 0 (flush), on the wallpaper.
      t = thick ? 2 : 1
      shadow_pens = sgr("0;38;2;#{rgb(SHADOW)};48;2;#{rgb(BG)}")
      out << at.call(x0 + w, y0, shadow_pens + sgr("66:2:#{t}:0") + ' ')
      (1..WIN_BODY + 1).each { |r| out << at.call(x0 + w, y0 + r, shadow_pens + sgr("66:1:#{t}:0") + ' ') }
      out << at.call(x0, by + 1, shadow_pens + sgr("66:4:#{t}:0") + ' ')
      (1...w).each { |c| out << at.call(x0 + c, by + 1, shadow_pens + sgr("66:3:#{t}:0") + ' ') }
      out << at.call(x0 + w, by + 1, shadow_pens + sgr("66:5:#{t}:0") + ' ')
      out << sgr('0') + colours
    end
    # Inactive windows: plain title ends round plain `title` cells, no stripes.
    inactive_text = lambda do |x0, y0, slate|
      first = slate ? 0x100254 : 0x100231
      ends = [FRAME_FIRST + (slate ? 11 : 9), FRAME_FIRST + (slate ? 12 : 10)]
      title = [ends[0]] + [first + 5] * (WIN_COLS - 2) + [ends[1]]
      out = at.call(x0, y0, bar_pens + title.map { |c| u(c) }.join)
      (1..WIN_BODY).each do |r|
        out << at.call(x0, y0 + r, face_pens + u(FRAME_FIRST) + ' ' * (WIN_COLS - 2) + u(FRAME_FIRST + 1))
      end
      out << at.call(x0, y0 + WIN_BODY + 1, face_pens + u(FRAME_FIRST + 2) + u(FRAME_FIRST + 3) * (WIN_COLS - 2) + u(FRAME_FIRST + 4))
      out << sgr('0') + colours
    end
    # ADR 0219: the pressed boxes (on the bar) and the grow cells (on the face), beside them.
    # ADR 0222: then the four minimise boxes, on the bar.
    states = (0...10).map { |i| (i % 5 < 3 ? bar_pens : face_pens) + u(STATE_FIRST + i) }.join +
             (0...4).map { |i| bar_pens + u(MIN_FIRST + i) }.join + sgr('0') + colours
    frames_scene = at.call(2, 10, nine) + at.call(16, 10, states) + win_text.call(2, 12, true, false) + win_text.call(2, 20, false, true) +
                   inactive_text.call(16, 12, true) + inactive_text.call(16, 20, false)
    shadows = SHADOWS.map { |k, t, i| "#{sgr("66:#{k}:#{t}:#{i}")} #{sgr('67')} " }.join +
              "#{sgr('58;2;255;0;0;66:1:2')} #{sgr('0')}#{colours}#{sgr('66:1:2;0')}#{colours} " \
              "#{sgr('58;2;255;0;0;66:1:2')}#{u(FIRST + 14)}#{sgr('0')}#{colours}"
    # Row 9: a Slate dialog's eight border cells on the wallpaper (the blue),
    # SGR 58 the dialog's face; then the four button caps on that face, SGR 58
    # the button's face.
    slate = 0x100254
    dialog = (23..30).map { |i| u(slate + i) }.join
    caps = (31..34).map { |i| u(slate + i) }.join
    pen_row = "#{sgr("0;38;2;#{rgb(FG)};48;2;#{rgb(BG)};58;2;#{rgb(FACE)}")}#{dialog}" \
              "#{sgr("0;38;2;#{rgb(FG)};48;2;#{rgb(FACE)};58;2;#{rgb(BTN)}")}#{caps}#{sgr('59')}#{colours}"
    script = "printf '\\033[?25l#{colours}\\033[2J" \
             "\\033[2;3H#{rows[0]}\\033[3;3H#{rows[1]}\\033[4;3H#{sgr("58;2;#{rgb(LINE)}")}#{rows[1]}#{sgr('59')}" \
             "\\033[6;3H#{buttons}#{colours}\\033[7;3H#{sgr('51')} #{sgr('0')}#{colours}E#{sgr('51:5')} #{sgr('0')}#{colours}" \
             "\\033[8;3H#{titles}#{colours}\\033[9;3H#{shadows}\\033[10;3H#{pen_row}#{frames_scene}'; sleep 600"
    # A file, not -c: the 105 glyphs make a script longer than an argument
    # may be.
    path = File.join(root, 'scene.sh')
    File.write(path, script + "\n")
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: '/bin/sh', display_id: 'display-1', arguments: [path] })
    check(code == 201, "session -> #{code} #{session}")
    sleep 1.5
    grid = request('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
    cw, ch = grid['cell_width'], grid['cell_height']
    check([cw, ch] == [11, 22], "hack at 22 px is #{cw}x#{ch}, not the owner's 11x22")
    cells = request('GET', "/v1/surfaces/#{session['surface_id']}/cells")[1]['cells']
    style = ->(x, y) { (cells.find { |c| c['x'] == x && c['y'] == y } || {}).dig('style', 'framed') }
    check(style.call(4, 5) == 'thin' && style.call(10, 5) == 'thick' && style.call(7, 5) == 'none' &&
          style.call(16, 5) == 'thin', 'the surface does not hold SGR 51 / 51:3 / 54 as written')
    shadow = ->(x, y) { (cells.find { |c| c['x'] == x && c['y'] == y } || {}).dig('style', 'shadow') }
    names = %w[right right-start under under-start corner]
    SHADOWS.each_with_index do |(k, t, inset), i|
      name = "#{names[k - 1]}:#{t}#{inset.zero? ? ':0' : ''}"
      check(shadow.call(2 + 2 * i, 8) == name && [nil, 'none'].include?(shadow.call(3 + 2 * i, 8)),
            "the surface does not hold SGR 66:#{k}:#{t}:#{inset} then 67 (#{shadow.call(2 + 2 * i, 8).inspect})")
    end
    shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
    px = ->(col, row, dx, dy) { shot.pixel(grid['origin_x'] + col * cw + dx, grid['origin_y'] + row * ch + dy) }

    # Every glyph cell, pixel for pixel, against the design.
    design, err, status = Open3.capture3('python3', File.join(ROOT, 'tools/fonts/draw-window-glyphs.py'),
                                         '--reference', cw.to_s, ch.to_s)
    check(status.success?, "the design does not run: #{err}")
    lines = design.lines
    check(lines.size == 97, "the design has #{lines.size} glyphs, not 97")
    # Chalk on row 1; Slate on row 2 (no underline colour: LINE is white)
    # and row 3 (underline colour LINE).
    [[0, 1, nil], [35, 2, [255, 255, 255]], [35, 3, LINE]].each do |offset, row, line_pen|
      35.times do |i|
        _, fmt, data = lines[offset + i].split
        pens = { 'K' => FG, 'L' => line_pen, 'M' => MIX }
        ch.times do |dy|
          cw.times do |dx|
            want = if fmt == 'coverage'
                     [data[2 * (dy * cw + dx), 2].hex == 255 ? FG : BG]
                   else
                     [pens.fetch(data[dy * cw + dx], BG)]
                   end.first
            got = px.call(2 + i, row, dx, dy)
            check(got == want, format('U+%X on row %d pixel %d,%d is %s, the design says %s',
                                      FIRST + offset + i, row, dx, dy, got.inspect, want.inspect))
            checked += 1
          end
        end
      end
    end

    # The outer-edge frame, ADR 0214: a cell's pixels from the design.
    ref = lines.to_h { |l| cp, fmt, data = l.split; [cp.to_i(16), [fmt, data]] }
    cell_ok = lambda do |col, row, cp, fg, bg, line, what|
      fmt, data = ref.fetch(cp)
      pens = { 'K' => fg, 'L' => line, 'M' => fg.zip(bg).map { |a, b| (a + b) / 2 } }
      ch.times do |dy|
        cw.times do |dx|
          want = fmt == 'coverage' ? (data[2 * (dy * cw + dx), 2].hex == 255 ? fg : bg) : pens.fetch(data[dy * cw + dx], bg)
          got = px.call(col, row, dx, dy)
          check(got == want, "#{what} (U+#{cp.to_s(16).upcase}) at pixel #{dx},#{dy} is #{got.inspect}, the design says #{want.inspect}")
          checked += 1
        end
      end
    end
    13.times do |i|
      cell_ok.call(2 + i, 10, FRAME_FIRST + i, INK, i < 5 ? FACE : BAR, HI, "frame glyph #{i}")
    end
    # ADR 0219: the same through the compositor, each of the ten from the design.
    10.times do |i|
      cell_ok.call(16 + i, 10, STATE_FIRST + i, INK, i % 5 < 3 ? BAR : FACE, HI, "pressed or grow glyph #{i}")
    end
    4.times do |i|
      cell_ok.call(26 + i, 10, MIN_FIRST + i, INK, BAR, HI, "minimise box #{i}")
    end
    # Inactive windows: every cell the design's; no stripe anywhere in the bar
    # (every pixel off the bevel rows and the outer lines is the bar colour);
    # the body and bottom as the active windows'.
    [[12, true, 0x100254], [20, false, 0x100231]].each do |y0, slate, first|
      w = WIN_COLS
      ends = [FRAME_FIRST + (slate ? 11 : 9), FRAME_FIRST + (slate ? 12 : 10)]
      ([ends[0]] + [first + 5] * (w - 2) + [ends[1]]).each_with_index do |cp, i|
        cell_ok.call(16 + i, y0, cp, INK, BAR, HI, "inactive window title cell #{i}")
      end
      (2...ch - 2).each do |dy|
        (1...w * cw - 1).each do |ax|
          colour = px.call(16 + ax / cw, y0, ax % cw, dy)
          check(colour == BAR, "the inactive #{slate ? 'Slate' : 'Chalk'} bar has a stripe at #{ax},#{dy}: #{colour.inspect}")
          checked += 1
        end
      end
      (1..WIN_BODY + 1).each do |r|
        w.times do |c|
          ch.times do |dy|
            cw.times do |dx|
              bottom = r == WIN_BODY + 1
              line = (c.zero? && dx.zero?) || (c == w - 1 && dx == cw - 1) || (bottom && dy == ch - 1)
              got = px.call(16 + c, y0 + r, dx, dy)
              check(got == (line ? INK : FACE), "inactive window cell #{c},#{r} pixel #{dx},#{dy} is #{got.inspect}")
              checked += 1
            end
          end
        end
      end
    end
    [[12, true, 1, 0x100254], [20, false, 2, 0x100231]].each do |y0, slate, t, first|
      w = WIN_COLS
      # The title row: every cell the design's, the ends from the frame set.
      ends = [FRAME_FIRST + (slate ? 7 : 5), FRAME_FIRST + (slate ? 8 : 6)]
      cps = [ends[0]] + [first] * 3 + [first + 3] + [first + 5] * 2 + [first + 4] + [first] + [ends[1]]
      cps.each_with_index { |cp, i| cell_ok.call(2 + i, y0, cp, INK, BAR, HI, "window title cell #{i}") }
      # The stripes run from the end cells into their neighbours: on every
      # stripe row the pixels from 2 px in (one clear beside the line) to the
      # neighbour's edge are one stripe colour, and the outer line is ink.
      stripe_rows = (1...ch - 1).select { |dy| px.call(3, y0, cw / 2, dy) != px.call(2 + 5, y0, cw / 2, dy) }
      stripe_rows.each do |dy|
        run = (2...cw).map { |dx| px.call(2, y0, dx, dy) } + (0...cw).map { |dx| px.call(3, y0, dx, dy) }
        check(run.uniq.size == 1, "the stripes do not run from the left end cell into the next at row #{dy}: #{run.uniq}")
        check(px.call(2, y0, 0, dy) == INK && px.call(2, y0, 1, dy) != run[0], "the left end cell's outer line or the clear pixel beside it at row #{dy}")
        run = (0...cw - 2).map { |dx| px.call(2 + w - 1, y0, dx, dy) } + (0...cw).map { |dx| px.call(2 + w - 2, y0, dx, dy) }
        check(run.uniq.size == 1, "the stripes do not run from the right end cell into the previous at row #{dy}: #{run.uniq}")
        check(px.call(2 + w - 1, y0, cw - 1, dy) == INK && px.call(2 + w - 1, y0, cw - 2, dy) != run[0], "the right end cell's outer line at row #{dy}")
      end
      check(stripe_rows.size >= 6, "the window's title bar has #{stripe_rows.size} stripe rows")
      # The body: the face, with the line on the outermost edge of the
      # frame cells and nothing else.
      (1..WIN_BODY).each do |r|
        w.times do |c|
          ch.times do |dy|
            cw.times do |dx|
              line = (c.zero? && dx.zero?) || (c == w - 1 && dx == cw - 1)
              got = px.call(2 + c, y0 + r, dx, dy)
              check(got == (line ? INK : FACE), "window body cell #{c},#{r} pixel #{dx},#{dy} is #{got.inspect}: the line is not just the outermost edge")
              checked += 1
            end
          end
        end
      end
      by = y0 + WIN_BODY + 1
      w.times do |c|
        ch.times do |dy|
          cw.times do |dx|
            line = dy == ch - 1 || (c.zero? && dx.zero?) || (c == w - 1 && dx == cw - 1)
            got = px.call(2 + c, by, dx, dy)
            check(got == (line ? INK : FACE), "window bottom cell #{c} pixel #{dx},#{dy} is #{got.inspect}")
            checked += 1
          end
        end
      end
      # The shadow beyond the frame (inset 0): the first pixel next to the
      # line is the shadow's, not wallpaper (SGR 66 flush), t px wide.
      sc = 2 + w
      (1..WIN_BODY + 1).each do |r|
        ch.times do |dy|
          cw.times do |dx|
            want = dx < t ? SHADOW : BG
            got = px.call(sc, y0 + r, dx, dy)
            check(got == want, "shadow right of the frame at pixel #{dx},#{dy} (row #{r}) is #{got.inspect}, want #{want.inspect}")
            checked += 1
          end
        end
      end
      check(px.call(sc - 1, y0 + 1, cw - 1, 5) == INK && px.call(sc, y0 + 1, 0, 5) == SHADOW, 'the shadow does not start at the pixel next to the right frame line')
      (1...w).each do |c|
        ch.times do |dy|
          cw.times do |dx|
            want = dy < t ? SHADOW : BG
            got = px.call(2 + c, by + 1, dx, dy)
            check(got == want, "shadow under the frame at cell #{c} pixel #{dx},#{dy} is #{got.inspect}, want #{want.inspect}")
            check(px.call(2 + c, by, dx, ch - 1) == INK && px.call(2 + c, by + 1, dx, 0) == SHADOW, 'the shadow does not start at the pixel under the bottom line')
            checked += 2
          end
        end
      end
      # Under the left corner the shadow starts 2 px in; at the corner it
      # joins the right strip.
      ch.times do |dy|
        cw.times do |dx|
          want = dy < t && dx >= 2 ? SHADOW : BG
          check(px.call(2, by + 1, dx, dy) == want, "shadow under the left corner at #{dx},#{dy}")
          rects = shadow_rects(5, t, cw, ch, 0)
          inside = rects.any? { |x0, x1, ya, yb| dx >= x0 && dx < x1 && dy >= ya && dy < yb }
          check(px.call(sc, by + 1, dx, dy) == (inside ? SHADOW : BG), "shadow corner at #{dx},#{dy}")
          checked += 2
        end
      end
    end

    # SGR 66: every kind and width exactly its strips, in the foreground,
    # and nothing in the cell after SGR 67.
    SHADOWS.each_with_index do |(k, t, inset), i|
      rects = shadow_rects(k, t, cw, ch, inset)
      ch.times do |dy|
        cw.times do |dx|
          inside = rects.any? { |x0, x1, y0, y1| dx >= x0 && dx < x1 && dy >= y0 && dy < y1 }
          check(px.call(2 + 2 * i, 8, dx, dy) == (inside ? FG : BG),
                "SGR 66:#{k}:#{t}:#{inset} pixel #{dx},#{dy} is #{px.call(2 + 2 * i, 8, dx, dy)}, want #{inside ? FG : BG}")
          check(px.call(3 + 2 * i, 8, dx, dy) == BG, "SGR 67 left a shadow after 66:#{k}:#{t}:#{inset}")
          checked += 2
        end
      end
    end
    base = 2 + 2 * SHADOWS.size
    red = shadow_rects(1, 2, cw, ch)
    ch.times do |dy|
      cw.times do |dx|
        inside = red.any? { |x0, x1, y0, y1| dx >= x0 && dx < x1 && dy >= y0 && dy < y1 }
        check(px.call(base, 8, dx, dy) == (inside ? RED : BG), 'SGR 66 does not draw in the underline colour')
        check(px.call(base + 1, 8, dx, dy) == BG, 'SGR 0 does not end the shadow')
        # The frame glyph's line (x 0, the foreground) over the red shadow.
        want = dx.zero? ? FG : inside ? RED : BG
        check(px.call(base + 2, 8, dx, dy) == want,
              "frame-right over a shadow: pixel #{dx},#{dy} is #{px.call(base + 2, 8, dx, dy)}, want #{want}")
        checked += 3
      end
    end

    # SGR 51 / 51:3 lines, their colour, and their end.
    lines = ->(col, row, colour) { (0...ch).select { |dy| (0...cw).all? { |dx| px.call(col, row, dx, dy) == colour } } }
    check(lines.call(4, 5, FG) == [0, ch - 1], "SGR 51 does not draw 1 px at top and bottom: #{lines.call(4, 5, FG)}")
    check(lines.call(10, 5, FG) == [0, 1, 2, ch - 3, ch - 2, ch - 1], 'SGR 51:3 does not draw 3 px lines')
    check(lines.call(16, 5, RED) == [0, ch - 1], 'SGR 51 does not draw in the underline colour')
    check(lines.call(20, 5, FG) == [0, ch - 1] && lines.call(21, 5, FG).empty? &&
          lines.call(22, 5, FG) == [0, ch - 1] && lines.call(23, 5, FG).empty?, 'SGR 54 or 0 does not end framing')
    check(lines.call(2, 6, FG) == [0, ch - 1], 'a framed blank draws nothing')
    check(lines.call(3, 6, FG).empty?, 'framing survives SGR 0')
    check(lines.call(4, 6, FG) == [ch - 1], "SGR 51:5 does not draw the bottom rule only: #{lines.call(4, 6, FG)}")

    # A Slate dialog on the wallpaper: from the content out the face (margin,
    # 2 px), the foreground (3 px), the face (gap, 1 px), the foreground (1 px),
    # then the wallpaper itself: no halo, no pixel of any other colour.
    %w[top-left top top-right left right bottom-left bottom bottom-right].each_with_index do |part, i|
      sides = part.split('-')
      ch.times do |dy|
        cw.times do |dx|
          dxx = sides.include?('left') ? cw - 1 - dx : sides.include?('right') ? dx : 0
          dyy = sides.include?('top') ? ch - 1 - dy : sides.include?('bottom') ? dy : 0
          d = [dxx, dyy].max
          want = if d >= 7 then BG elsif d.between?(2, 4) || d == 6 then FG else FACE end
          check(px.call(2 + i, 9, dx, dy) == want,
                "dialog-#{part} pixel #{dx},#{dy} (#{d} px out) is #{px.call(2 + i, 9, dx, dy)}, want #{want}: " \
                'a fixed colour, or the face outside the border')
          checked += 1
        end
      end
    end

    # The button caps on the dialog's face: ring in the foreground, the face
    # inside it the button's (SGR 58), the 2 px inset and the corners outside
    # the ring the dialog's face, the shade half foreground and half the
    # dialog's face and only down and right (the left cap: the lower half).
    shade = FG.zip(FACE).map { |a, b| (a + b) / 2 }
    allowed = [FACE, FG, BTN, shade]
    [[10, 'left', false], [11, 'right', false], [12, 'default-left', true], [13, 'default-right', true]].each do |col, name, thick|
      left = name.end_with?('left')
      ch.times do |dy|
        cw.times do |dx|
          colour = px.call(col, 9, dx, dy)
          check(allowed.include?(colour), "button-#{name} pixel #{dx},#{dy} is #{colour}, not the face, ring, button or shade")
          check(colour == FACE, "button-#{name}: the inset at #{dx},#{dy} is #{colour}, not the dialog's face") if (left ? dx < 2 : dx > 8)
          check(colour != shade, "button-#{name}: shade at #{dx},#{dy} in the upper half") if left && dy < 11
          checked += 1
        end
      end
      row = left ? (2...cw).map { |dx| px.call(col, 9, dx, 11) } : (0..8).map { |dx| px.call(col, 9, dx, 11) }
      check(row.include?(BTN) && !row.include?(FACE), "button-#{name}: the face is not the button's, solid to the seam: #{row.uniq}")
      corners = (left ? (0...7) : (4...cw)).map { |dx| px.call(col, 9, dx, 0) } + (left ? (0...7) : (4...cw)).map { |dx| px.call(col, 9, dx, ch - 1) }
      check(corners.all?(FACE), "button-#{name}: the corners outside the ring are not the dialog's face: #{corners.uniq}")
    end

    # A Slate title: its text cells carry the bar's top and bottom rows
    # (frame line, highlight; shade, rule) exactly as the title glyph beside
    # them, in the cell's pens: Pewter's black, white and 107 grey. 51:4,
    # once Slate dark's, draws the same as 51:2.
    pewter = { 0 => [0, 0, 0], 1 => [255, 255, 255], ch - 2 => [107, 107, 107], ch - 1 => [0, 0, 0] }
    pewter.each do |dy, colour|
      [2, 5, 8].each do |glyph_col|
        check(px.call(glyph_col, 7, 0, dy) == colour, "the Slate title glyph's row #{dy} is #{px.call(glyph_col, 7, 0, dy)}")
      end
      [3, 4, 6, 7].each do |text|
        check((0...cw).all? { |dx| px.call(text, 7, dx, dy) == colour },
              "the Slate title's text cell #{text} row #{dy} is #{px.call(text, 7, 5, dy)}, the bar's is #{colour}")
      end
    end

    # A button is one outline: its top and bottom rows unbroken from the
    # first inked pixel of its left end to the last of its right end.
    [[2, 7, 1], [9, 14, 3]].each do |left, right, thick|
      thick.times do |t|
        [t, ch - 1 - t].each do |dy|
          xs = (left..right).flat_map { |col| (0...cw).map { |dx| [col, dx] } }
          inked = xs.map { |col, dx| px.call(col, 5, dx, dy) == FG }
          first, last = inked.index(true), inked.rindex(true)
          check(first && inked[first..last].all?, "the #{thick == 3 ? 'default ' : ''}button's row #{dy} breaks")
          check(first < cw && last >= inked.size - cw, "the #{thick == 3 ? 'default ' : ''}button's row #{dy} does not reach its ends")
        end
      end
      check(px.call(left, 5, 0, ch / 2) == BG && px.call(left, 5, 2, ch / 2) == FG,
            "the #{thick == 3 ? 'default ' : ''}button's left end is not 2 px in")
    end
    request('DELETE', "/v1/terminal-sessions/#{session['id']}")
    puts "WINDOW_STYLE_PASS backend=#{GATE_BACKEND} cell=#{cw}x#{ch} glyphs=70+13_frame+10_state(pressed_boxes,grow)+4_minimise inactive_windows=plain_title_ends windows=slate+chalk(outer_edge,stripes_run_in,shadow_inset_0) pixels=#{checked} " \
         'chalk=foreground slate=cell_pens(fg,line,mix) transparent=background sgr51=1px sgr51:3=3px ' \
         'sgr51:5=bottom_rule underline_colour=used sgr54,sgr0=end buttons=continuous ' \
         'dialog=no_halo,no_fixed_colour caps=corners_show_dialog_face titles=51:2=51:4=bar_rows shadow=5_kinds*2_widths*2_insets,colour,under_glyph sgr67,sgr0=end'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
