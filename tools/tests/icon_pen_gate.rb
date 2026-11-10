#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0221: the icon pen is a cell attribute (SGR 68:v, ended by 69), through
# a real server and its compositor (BLIT_GATE_BACKEND native or gles), at the
# owner's 11x22 cell (Hack at 22 px):
#
# - A pen change is cell damage only. A row holds text, N colour icons and
#   text; rewriting the whole row with the icons in the black pen, inside a
#   synchronized update (so the damage is held for the gate to read), damages
#   exactly the N icon cells: one rectangle, N wide, one high, at the icons.
#   Back to colour, the same N cells. No reflow: the pane's cell grid, the
#   surface's size and the topology revision are unchanged; no font
#   reconfiguration: the active font (path, cell pixels) is the same raster,
#   whose render count does not drop, grows by at most one render a new pen
#   variant of an icon, and does not grow at all when the pen goes back
#   (the colour entries are still cached), on either backend.
# - Each pen draws differently, after the flips: one icon in colour, auto,
#   black and white, and in auto again under a second foreground. Black is
#   exactly black over the background (every pixel between the two, some
#   pixel black), white exactly white, auto exactly the cell's foreground,
#   for both foregrounds; the three share one shape (the pixels that are not
#   the background), and colour is none of them. The colour icons after the
#   flip back are pixel for pixel what they were before the flips.
# - A text cell carrying a pen draws exactly as one without: an outline
#   letter and a box-drawing glyph in each pen are byte-identical to pen 0.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19357'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
FG = [255, 200, 0].freeze
FG2 = [0, 200, 255].freeze
BG = [100, 120, 140].freeze
# Colour icons, one column each: wifi-2, battery-2, volume-1, bluetooth.
ICONS = [0x100003, 0x10000b, 0x100015, 0x100006].freeze
ICON = 0x10000b
LEFT = 2 # the icons' first column on the flipped row
ESC = "\e"

abort("icon pen gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  raise "ICON_PEN_FAIL backend=#{GATE_BACKEND} #{why}" unless ok
end

def u(cp) = [cp].pack('U')
def sgr(text) = "#{ESC}[#{text}m"
def at(x, y) = "#{ESC}[#{y + 1};#{x + 1}H"
def rgb(c) = c.join(';')

def wait_for(what, seconds = 10)
  Timeout.timeout(seconds) do
    loop do
      value = yield
      return value if value

      sleep 0.05
    end
  end
rescue Timeout::Error
  check(false, "timed out waiting for #{what}")
end

colours = sgr("0;38;2;#{rgb(FG)};48;2;#{rgb(BG)}")
icons = ICONS.map { |cp| u(cp) }.join
row = ->(pen) { "#{at(0, 1)}#{colours}ab#{sgr("68:#{pen}")}#{icons}#{sgr('69')}cd" }
pens_row = "#{at(0, 3)}#{colours}#{u(ICON)} #{sgr('68:1')}#{u(ICON)} #{sgr('68:2')}#{u(ICON)} " \
           "#{sgr('68:3')}#{u(ICON)} #{sgr("38;2;#{rgb(FG2)};68:1")}#{u(ICON)}#{colours}"
text_row = "#{at(0, 5)}#{colours}" + [0, 1, 2, 3].map { |p| "#{sgr("68:#{p}")}W│" }.join(' ') + colours
steps = [
  "#{ESC}[?25l#{colours}#{ESC}[2J#{row.call(0)}#{pens_row}#{text_row}",
  "#{ESC}[?2026h#{row.call(2)}",
  "#{ESC}[?2026l",
  "#{ESC}[?2026h#{row.call(0)}",
  "#{ESC}[?2026l"
]

result = nil
Dir.mktmpdir('blit-icon-pen') do |root|
  steps.each_with_index { |text, i| File.binwrite(File.join(root, "step#{i}"), text) }
  script = steps.each_index.map do |i|
    "while [ ! -f #{root}/go#{i} ]; do sleep 0.02; done; cat #{root}/step#{i}; "
  end.join + 'sleep 600'
  go = ->(i) { File.write(File.join(root, "go#{i}"), '') }
  # A file, not -c: a session argument is short.
  File.write(File.join(root, 'scene.sh'), script + "\n")
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
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: '/bin/sh', display_id: 'display-1', arguments: [File.join(root, 'scene.sh')] })
    check(code == 201, "session -> #{code} #{session}")
    surface = session['surface_id']
    summary = -> { request('GET', "/v1/surfaces/#{surface}/summary")[1] }
    cells = -> { request('GET', "/v1/surfaces/#{surface}/cells")[1]['cells'] }
    style = ->(list, x, y) { (list.find { |c| c['x'] == x && c['y'] == y } || {}).dig('style', 'icon_pen') }
    shot = -> { GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1')) }
    fonts = -> { request('GET', '/v1/fonts')[1]['active'] }
    topology = -> { request('GET', '/v1/topology')[1] }
    # A frame has drawn everything written, and nothing is pending.
    settled = lambda do
      wait_for('the frame to settle') do
        s = summary.call
        s && !s.dig('damage', 'valid') ? s : nil
      end
      sleep 0.3
    end

    go.call(0)
    wait_for('the scene') { style.call(cells.call, 4, 3) == 'black' }
    settled.call
    grid = topology.call['panes'][0]['cell_grid']
    cw, ch = grid['cell_width'], grid['cell_height']
    check([cw, ch] == [11, 22], "hack at 22 px is #{cw}x#{ch}, not the owner's 11x22")
    before_topology = topology.call
    before_font = fonts.call
    before_summary = summary.call
    first = shot.call
    renders0 = before_font.dig('raster_cache', 'renders')
    px = ->(s, col, row_y, dx, dy) { s.pixel(grid['origin_x'] + col * cw + dx, grid['origin_y'] + row_y * ch + dy) }
    cell_pixels = ->(s, col, row_y) { (0...ch).flat_map { |dy| (0...cw).map { |dx| px.call(s, col, row_y, dx, dy) } } }

    # The flip to black, held in a synchronized update: its damage is exactly
    # the icon cells.
    flips = []
    [[1, 2, 'black'], [3, 0, 'colour']].each do |step, pen, name|
      go.call(step)
      held = wait_for("the flip to #{name}") do
        s = summary.call
        s && s.dig('damage', 'valid') && style.call(cells.call, LEFT, 1) == name ? s : nil
      end
      # Held: the synchronized update keeps the surface from the renderer, so
      # its damage is still there to read a moment later.
      sleep 0.3
      damage = summary.call['damage']
      check(damage == { 'valid' => true, 'x' => LEFT, 'y' => 1, 'width' => ICONS.size, 'height' => 1 },
            "the flip to #{name} (pen #{pen}) damaged #{damage.inspect}, not the #{ICONS.size} icon cells " \
            "at #{LEFT},1")
      list = cells.call
      check((0...ICONS.size).all? { |i| style.call(list, LEFT + i, 1) == name } &&
            [0, 1, LEFT + ICONS.size, LEFT + ICONS.size + 1].all? { |x| [nil, 'colour'].include?(style.call(list, x, 1)) },
            "the row does not hold the #{name} pen on the icons alone")
      go.call(step + 1)
      settled.call
      flips << [name, damage, fonts.call.dig('raster_cache', 'renders')]
    end
    after_topology = topology.call
    after_font = fonts.call
    after_summary = summary.call
    # No reflow, no font reconfiguration.
    check(after_topology['revision'] == before_topology['revision'],
          "the topology revision moved #{before_topology['revision']} -> #{after_topology['revision']}")
    check(after_topology['panes'][0]['cell_grid'] == before_topology['panes'][0]['cell_grid'],
          'the pane cell grid changed')
    check([after_summary['width'], after_summary['height']] == [before_summary['width'], before_summary['height']],
          'the surface was resized')
    %w[id path size_px cell_pixels hinting].each do |key|
      check(after_font[key] == before_font[key], "the active font's #{key} changed")
    end
    black_renders = flips[0][2]
    colour_renders = flips[1][2]
    check(black_renders >= renders0 && black_renders - renders0 <= ICONS.size,
          "the flip to black rendered #{black_renders - renders0} glyphs (renders #{renders0} -> #{black_renders}), " \
          "not at most one a new icon variant")
    check(colour_renders == black_renders,
          "the flip back to colour rendered #{colour_renders - black_renders} glyphs; the colour entries were not reused")

    last = shot.call
    # Back to colour, the icons are what they were.
    ICONS.each_index do |i|
      check(cell_pixels.call(last, LEFT + i, 1) == cell_pixels.call(first, LEFT + i, 1),
            "icon #{i} in colour differs after the flips")
    end
    # Each pen draws differently.
    colour, auto, black, white, auto2 = [0, 2, 4, 6, 8].map { |col| cell_pixels.call(last, col, 3) }
    ink = ->(cell) { cell.map { |p| p != BG } }
    between = lambda do |cell, pen|
      cell.all? { |p| p.each_index.all? { |c| (p[c] - BG[c]) * (pen[c] - BG[c]) >= 0 && (p[c] - BG[c]).abs <= (pen[c] - BG[c]).abs } }
    end
    check(black.include?([0, 0, 0]) && between.call(black, [0, 0, 0]), 'the black pen is not black over the background')
    check(white.include?([255, 255, 255]) && between.call(white, [255, 255, 255]), 'the white pen is not white over the background')
    check(auto.include?(FG) && between.call(auto, FG), "the auto pen is not the cell's foreground")
    check(auto2.include?(FG2) && between.call(auto2, FG2), "the auto pen does not follow a second foreground")
    check(ink.call(black) == ink.call(white) && ink.call(white) == ink.call(auto) && ink.call(auto) == ink.call(auto2),
          'black, white and auto do not share one shape')
    check([auto, black, white].none? { |c| c == colour }, 'a pen draws the colour icon')
    check(colour.count { |p| p != BG } > 10, 'the colour icon did not draw')
    # A text cell with a pen draws as one without.
    letter0, box0 = cell_pixels.call(last, 0, 5), cell_pixels.call(last, 1, 5)
    check(letter0.count { |p| p != BG } > 10 && box0.count { |p| p != BG } > 10, 'the text row did not draw')
    [1, 2, 3].each do |p|
      check(cell_pixels.call(last, 3 * p, 5) == letter0, "W in pen #{p} differs from pen 0")
      check(cell_pixels.call(last, 3 * p + 1, 5) == box0, "the box-drawing line in pen #{p} differs from pen 0")
    end
    request('DELETE', "/v1/terminal-sessions/#{session['id']}")
    result = "ICON_PEN_PASS backend=#{GATE_BACKEND} cell=#{cw}x#{ch} damage=#{flips.map { |n, d, _| "#{n}:#{d['x']},#{d['y']},#{d['width']}x#{d['height']}" }.join('+')} " \
             "renders=#{renders0}->#{black_renders}->#{colour_renders} topology_revision=#{after_topology['revision']} (unchanged) " \
             'pens=colour,auto,black,white,auto(fg2) shape=shared text=pen_ignored'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
puts result
