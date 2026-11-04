#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0210 through a real server and its compositor (BLIT_GATE_BACKEND
# native or gles), at the owner's 11x22 cell (Hack at 22 px):
#
# - A session prints all 105 window-chrome glyph sets, a row per set, in an
#   orange foreground on a blue background. Every pixel of every glyph cell
#   is what the design (tools/fonts/draw-window-glyphs.py) says: Chalk in
#   the foreground, Slate in its own colours, and the background wherever
#   the glyph is transparent.
# - SGR 51 draws a 1 px line along a cell's top and bottom, 51:3 a 3 px
#   one, in the underline colour (SGR 58) if set, else the foreground; SGR
#   54 and SGR 0 end it, and a framed blank still draws.
# - A button's left end, its framed label and its right end make one
#   outline: the top and bottom rows run unbroken from end to end, thin and
#   default.

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
FIRST = 0x100231

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
    rows = (0...3).map { |set| (0...35).map { |i| u(FIRST + set * 35 + i) }.join }
    bl, br = u(FIRST + 31), u(FIRST + 32)
    dl, dr = u(FIRST + 33), u(FIRST + 34)
    # Row 5: a button, a default button, a red-framed label, framing ended
    # by 54 and by 0. Row 6: a framed blank, framing after a reset, and a
    # 51:5 blank (the rule under a note's strip: the bottom line only).
    buttons = "#{bl}#{sgr('51')} OK #{sgr('54')}#{br} #{dl}#{sgr('51:3')} Go #{sgr('54')}#{dr} " \
              "#{sgr('58;2;255;0;0;51')}red#{sgr('59;54')} #{sgr('51')}A#{sgr('54')}B#{sgr('51')}C#{sgr('0')}D"
    # Row 7: a Slate light and a Slate dark title, framed 51:2 and 51:4 on
    # the bar's colour between two title glyphs.
    titles = [[0x100254 + 5, '214;214;214', '51:2'], [0x100277 + 5, '60;60;67', '51:4']].map do |cp, bar, kind|
      "#{u(cp)}#{sgr("48;2;#{bar};#{kind}")}Ti#{sgr("54;48;2;#{rgb(BG)}")}#{u(cp)}"
    end.join
    script = "printf '\\033[?25l#{colours}\\033[2J" \
             "\\033[2;3H#{rows[0]}\\033[3;3H#{rows[1]}\\033[4;3H#{rows[2]}" \
             "\\033[6;3H#{buttons}#{colours}\\033[7;3H#{sgr('51')} #{sgr('0')}#{colours}E#{sgr('51:5')} #{sgr('0')}#{colours}" \
             "\\033[8;3H#{titles}'; sleep 600"
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
    shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
    px = ->(col, row, dx, dy) { shot.pixel(grid['origin_x'] + col * cw + dx, grid['origin_y'] + row * ch + dy) }

    # Every glyph cell, pixel for pixel, against the design.
    design, err, status = Open3.capture3('python3', File.join(ROOT, 'tools/fonts/draw-window-glyphs.py'),
                                         '--reference', cw.to_s, ch.to_s)
    check(status.success?, "the design does not run: #{err}")
    design.each_line.with_index do |line, index|
      _, fmt, hex = line.split
      bytes = [hex].pack('H*').bytes
      set, i = index.divmod(35)
      ch.times do |dy|
        cw.times do |dx|
          want = if fmt == 'coverage'
                   bytes[dy * cw + dx] == 255 ? FG : BG
                 else
                   b = bytes[(dy * cw + dx) * 4, 4]
                   b[3] == 255 ? b[0, 3] : BG
                 end
          got = px.call(2 + i, 1 + set, dx, dy)
          check(got == want, format('U+%X pixel %d,%d is %s, the design says %s', FIRST + index, dx, dy, got.inspect, want.inspect))
          checked += 1
        end
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

    # A Slate title: its text cells carry the bar's top and bottom rows
    # (frame line, highlight; shade, rule) exactly as the title glyph beside
    # them, light (51:2) and dark (51:4).
    [[2, 'light'], [6, 'dark']].each do |col, set|
      [0, 1, ch - 2, ch - 1].each do |dy|
        want = px.call(col, 7, 0, dy)
        [col + 1, col + 2].each do |text|
          check((0...cw).all? { |dx| px.call(text, 7, dx, dy) == want },
                "the Slate #{set} title's row #{dy} does not match the bar's (#{px.call(text, 7, 5, dy)} vs #{want})")
        end
      end
      check(px.call(col, 7, 0, 1) != px.call(col, 7, 0, 0), "the Slate #{set} bar has no highlight row")
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
    puts "WINDOW_STYLE_PASS backend=#{GATE_BACKEND} cell=#{cw}x#{ch} glyphs=105 pixels=#{checked} " \
         'chalk=foreground slate=own_colours transparent=background sgr51=1px sgr51:3=3px sgr51:5=bottom_rule ' \
         'underline_colour=used sgr54,sgr0=end buttons=continuous titles=51:2,51:4_match_bar'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
