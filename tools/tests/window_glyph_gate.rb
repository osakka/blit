#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0210, 0211, 0214: the window-chrome glyph sets, Chalk (U+100231-100253)
# and Slate (U+100254-100276); Slate dark (U+100277-100299) is retired.
#
# - The catalogue lists all 70 by name, as cell geometry, in two sets of
#   35 in one order, clear of the icons and of every retired codepoint, and
#   lists Slate dark as retired. The harness requires the retired range to
#   draw nothing of its own.
# - Through the production raster, every glyph equals the design
#   (tools/fonts/draw-window-glyphs.py, which reads the geometry table in
#   src/blit_window_glyphs.h) pixel for pixel: at the owner's 11x22 cell
#   and at eleven other cell sizes. Chalk as coverage; Slate as its role
#   map, read from its three pen planes (FG, LINE, MIX), which the harness
#   requires to be disjoint and together its one-colour lookup.
# - The outer-edge frame (ADR 0214), U+1002AE-1002BA: thirteen more glyphs (the last four, 0.3.45, are
#   the plain title ends, below). The
#   five frame cells (frame-left, -right, -bottom-left, -bottom,
#   -bottom-right) are one set, Chalk and Slate alike: a 1 px line on the
#   cell's OUTERMOST edge and no other ink in the cell, at every size, in
#   the foreground alone (coverage, no pen planes). The four title-end cells
#   (chalk-title-end-left/-right as coverage, slate-light-title-end-left/
#   -right as pens) are the bar row's end cells: the outer line, the bar's
#   top line and rule (Slate's bevel too), and stripes that run INTO the
#   cell, starting (or stopping) 2 px from its outer edge -- the line and
#   one clear pixel -- and join the next cell's stripes row for row at
#   every size. Chalk's ink is Slate's foreground and, in the stripe rows,
#   its mix. The old frame and stripe-first/-last glyphs are unchanged
#   (they are deprecated, not retired).
# - Every pixel is fully set or fully clear, and the frame cells are
#   transparent off their lines.
# - Pewter (ADR 0211): Slate drawn in the cell's pens with bg 214, fg black
#   and SGR 58 white is 0.3.38's Slate light, except where a pixel is the
#   MIX pen, and except the dialog and the button caps, which ADR 0211
#   redrew: a dialog's margin and gap are the line pen (SGR 58 its face),
#   its lines solid foreground, nothing outside them; a cap's face is the
#   line pen (SGR 58 the button's), its corners outside the ring nothing
#   (the dialog's face shows), no highlight, a shade only down and right.
# - At 11x22 the approved mock-up's pixels hold, written here, not read
#   from the design: stripe and bevel rows, the full-width 11x11 box, the
#   insets, the dialog's layers, the buttons' ends.
# - No frame or dialog piece draws a shadow, or any pixel outside its
#   line or its border, in any set at any size (ADR 0210, revised).
# - At every size, bar cells join: a stripe's rows run edge to edge and
#   match its neighbours', and the frame line and rule cross every edge.

require 'json'
require 'open3'
require 'tmpdir'

ROOT = File.expand_path('../..', __dir__)
SIZES = [[11, 22], [4, 8], [5, 10], [6, 12], [8, 16], [9, 18], [10, 20], [12, 24], [14, 28], [16, 32],
         [22, 44], [32, 64]].freeze
NAMES = %w[stripe stripe-first stripe-last stripe-before-title stripe-after-title title
           stripe-to-box stripe-from-box stripe-between close shade zoom frame-left
           frame-right-top frame-right frame-bottom-left frame-bottom-first frame-bottom
           frame-bottom-right frame-top-left frame-top frame-top-right note-close
           dialog-top-left dialog-top dialog-top-right dialog-left dialog-right
           dialog-bottom-left dialog-bottom dialog-bottom-right button-left button-right
           default-button-left default-button-right].freeze
SETS = { 'chalk' => 0x100231, 'slate-light' => 0x100254 }.freeze

# ADR 0214: the outer-edge frame, in order from U+1002AE.
FRAME_FIRST = 0x1002AE
FRAME_NAMES = %w[frame-left frame-right frame-bottom-left frame-bottom frame-bottom-right
                 chalk-title-end-left chalk-title-end-right slate-light-title-end-left
                 slate-light-title-end-right chalk-title-end-plain-left chalk-title-end-plain-right
                 slate-light-title-end-plain-left slate-light-title-end-plain-right].freeze
FRAME_SETS = %w[frame frame frame frame frame chalk chalk slate-light slate-light chalk chalk slate-light slate-light].freeze
FRAME_PENS = %w[foreground foreground foreground foreground foreground foreground foreground cell cell foreground foreground cell cell].freeze

def fail!(why)
  abort "WINDOW_GLYPH_FAIL #{why}"
end

def check(ok, why)
  fail!(why) unless ok
end

Dir.chdir(ROOT)

# The catalogue.
catalogue = JSON.parse(File.read('assets/fonts/icons/catalogue.json'))
geometry = catalogue.fetch('cell_geometry')
# The window glyphs are U+100231-100276; the controls (ADR 0212) follow the
# retired Slate dark range, and have their own gate.
window = geometry.select { |e| e['codepoint'].to_i(16).between?(0x100231, 0x100276) }
expected = SETS.flat_map do |style, first|
  NAMES.each_with_index.map do |name, i|
    ["#{style}-#{name}", format('%06X', first + i), false, style == 'chalk' ? 'foreground' : 'cell']
  end
end
check(window.map { |e| [e['id'], e['codepoint'], e['colour'], e['pens']] } == expected,
      'the catalogue does not list the 70 window glyphs in order with their names and pens')
check(geometry.none? { |e| e['id'].start_with?('slate-dark-') }, 'the catalogue still lists Slate dark')
check(window.all? { |e| e['shape'].is_a?(String) && !e['shape'].empty? }, 'a window glyph has no shape')
icon_codes = catalogue.fetch('icons').map { |i| i['codepoint'].to_i(16) }
retired = catalogue.fetch('retired').flat_map { |r| (r['from'].to_i(16)..r['to'].to_i(16)).to_a }
codes = window.map { |e| e['codepoint'].to_i(16) }
check((codes & icon_codes).empty? && (codes & retired).empty?, 'a window glyph reuses an icon or retired codepoint')
check(codes == (0x100231..0x100276).to_a, 'the window glyphs are not U+100231-100276')
check((0x100277..0x100299).all? { |c| retired.include?(c) }, 'Slate dark (U+100277-100299) is not retired')
check(geometry.none? { |e| e['codepoint'].to_i(16).between?(0x100277, 0x100299) },
      'a glyph is mapped in the retired Slate dark range')
frames = geometry.select { |e| e['codepoint'].to_i(16).between?(FRAME_FIRST, FRAME_FIRST + 12) }
check(frames.map { |e| [e['id'], e['codepoint'], e['set'], e['pens'], e['colour']] } ==
      FRAME_NAMES.each_with_index.map { |n, i| [n, format('%06X', FRAME_FIRST + i), FRAME_SETS[i], FRAME_PENS[i], false] },
      'the catalogue does not list the thirteen outer-edge frame glyphs (U+1002AE-1002BA) in order with their sets and pens')
check(frames.all? { |e| e['shape'].is_a?(String) && !e['shape'].empty? }, 'an outer-edge frame glyph has no shape')
check((frames.map { |e| e['codepoint'].to_i(16) } & (icon_codes + retired)).empty?, 'a frame glyph reuses an icon or retired codepoint')

# The raster, at every size.
freetype = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{ROOT}/build/deps/prefix/include/freetype2")
raster = {}
design = {}
Dir.mktmpdir('blit-window-glyphs') do |work|
  binary = File.join(work, 'gate')
  check(system('tcc', '-O2', '-std=c99', '-Wall', "-I#{freetype}",
               'tools/tests/window_glyph_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
               ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
               ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),
               '-o', binary), 'the harness does not compile')
  out, err, status = Open3.capture3(binary, 'assets/fonts/Blit-Terminus16.psf',
                                    'assets/fonts/text/Hack-Regular.ttf', 'assets/fonts/NotoColorEmoji.ttf',
                                    *SIZES.flatten.map(&:to_s))
  check(status.success?, "the raster harness failed: #{err.strip}")
  size = nil
  out.each_line do |line|
    parts = line.split
    if parts[0] == 'SIZE'
      size = [parts[1].to_i, parts[2].to_i]
      next
    end
    raster[[size, parts[0].to_i(16)]] = [parts[1], parts[2]]
  end
  SIZES.each do |w, h|
    text, err, status = Open3.capture3('python3', 'tools/fonts/draw-window-glyphs.py', '--reference', w.to_s, h.to_s)
    check(status.success?, "the design does not run: #{err.strip}")
    text.each_line do |line|
      cp, fmt, hex = line.split
      design[[[w, h], cp.to_i(16)]] = [fmt, hex]
    end
  end
end
check(raster.size == 83 * SIZES.size, "the raster drew #{raster.size} of #{83 * SIZES.size}")
mismatches = []
raster.each do |key, got|
  mismatches << format('U+%X at %dx%d', key[1], *key[0]) unless design[key] == got
end
check(mismatches.empty?, "differs from the design: #{mismatches.first(8).join(', ')}#{mismatches.size > 8 ? " and #{mismatches.size - 8} more" : ''}")

# Pixels as [coverage, role] rows: Chalk's role is nil, Slate's K, L or M
# (nothing drawn: coverage 0, role nil).
def pixels(raster, w, h, cp)
  fmt, data = raster.fetch([[w, h], cp])
  if fmt == 'coverage'
    bytes = [data].pack('H*').bytes
    Array.new(h) { |y| Array.new(w) { |x| [bytes[y * w + x], nil] } }
  else
    Array.new(h) { |y| Array.new(w) { |x| r = data[y * w + x]; r == '.' ? [0, nil] : [255, r] } }
  end
end

def glyph(raster, style, name, w = 11, h = 22)
  pixels(raster, w, h, SETS.fetch(style) + NAMES.index(name))
end

# Formats, and crisp, transparent-or-opaque pixels.
SETS.each do |style, first|
  NAMES.each_index do |i|
    SIZES.each do |w, h|
      fmt, = raster.fetch([[w, h], first + i])
      check(fmt == (style == 'chalk' ? 'coverage' : 'roles'), "#{style}-#{NAMES[i]} is #{fmt}")
      check(pixels(raster, w, h, first + i).flatten(1).all? { |a, _| [0, 255].include?(a) },
            "#{style}-#{NAMES[i]} at #{w}x#{h} has a partial pixel")
    end
  end
end

# The approved mock-up at 11x22, written out here.
ink = ->(g, x, y) { g[y][x][0] == 255 }
rgb = ->(g, x, y) { g[y][x][1] }
STRIPE_ROWS = [5, 7, 9, 11, 13, 15].freeze
# Slate's roles under the approved Slate light names: K frame, D the dark
# grey (now the MIX pen), L and W the highlight and the box face (now the
# LINE pen); the bar and the faces are the cell's background, not drawn.
LIGHT = { K: 'K', D: 'M', L: 'L', W: 'L' }.freeze
role_name = ->(r) { { 'K' => :K, 'M' => :D, 'L' => :L }.fetch(r, :B) }
g = glyph(raster, 'chalk', 'stripe')
check((0...22).select { |y| ink.call(g, 5, y) } == [0, *STRIPE_ROWS, 21], 'Chalk stripes are not rows 5-15 with the frame and rule')
g = glyph(raster, 'slate-light', 'stripe')
column = (0...22).map { |y| role_name.call(rgb.call(g, 5, y)) }
want = %i[K L B B B D L D L D L D L D L D L B B B D K]
check(column == want, "Slate light stripe column is #{column.join}, not #{want.join}")
{ 'stripe-first' => [2, 11], 'stripe-from-box' => [2, 11], 'stripe-last' => [0, 9], 'stripe-to-box' => [0, 9],
  'stripe-between' => [2, 9], 'stripe-before-title' => [0, 6], 'stripe-after-title' => [5, 11], 'title' => [0, 0] }
  .each do |name, (x0, x1)|
  %w[chalk slate-light].each do |style|
    g = glyph(raster, style, name)
    xs = (0...11).select { |x| style == 'chalk' ? ink.call(g, x, 7) : rgb.call(g, x, 7) == LIGHT[:D] }
    check(xs == (x0...x1).to_a, "#{style}-#{name}: stripes at x #{xs.inspect}, not #{x0}...#{x1}")
  end
end
%w[close shade zoom note-close].each do |name|
  g = glyph(raster, 'slate-light', name)
  outline = (0...11).flat_map { |x| [[x, 5], [x, 15]] } + (5..15).flat_map { |y| [[0, y], [10, y]] }
  check(outline.all? { |x, y| rgb.call(g, x, y) == LIGHT[:K] }, "slate-light-#{name}: the box is not the full-width 11x11 at y 5-15")
  check(rgb.call(g, 8, 13) == LIGHT[:W], "slate-light-#{name}: the box's face is not white")
end
g = glyph(raster, 'slate-light', 'zoom')
check([[6, 6], [6, 11], [3, 11], [7, 7]].map { |x, y| rgb.call(g, x, y) == LIGHT[:K] } == [true, true, true, false],
      'the zoom box has no inner square at (0,5)-(6,11)')
g = glyph(raster, 'slate-light', 'shade')
check((1..9).all? { |x| rgb.call(g, x, 9) == LIGHT[:K] } && rgb.call(g, 5, 8) == LIGHT[:W], 'the shade box has no bar at y 9')
g = glyph(raster, 'chalk', 'note-close')
check(!ink.call(g, 5, 2) && !ink.call(g, 5, 10) && ink.call(g, 0, 5), 'the note close box is not alone and open')
g = glyph(raster, 'chalk', 'frame-right-top')
check((0...22).all? { |y| ink.call(g, 0, y) && !ink.call(g, 1, y) && !ink.call(g, 2, y) },
      'frame-right-top: no line against the content, or a shadow beside it')
g = glyph(raster, 'chalk', 'frame-left')
check((0...22).all? { |y| ink.call(g, 10, y) && !ink.call(g, 0, y) }, 'frame-left does not hug the content, transparent elsewhere')
g = glyph(raster, 'slate-light', 'frame-bottom-first')
check((0...11).all? { |x| rgb.call(g, x, 0) == LIGHT[:K] && g[1][x][0].zero? && g[2][x][0].zero? },
      'frame-bottom-first: no line against the content, or a shadow under it')
g = glyph(raster, 'slate-light', 'dialog-left')
layers = (0...11).map { |x| g[10][x][0].zero? ? :none : role_name.call(rgb.call(g, x, 10)) }
want = %i[none none none none K L K K K L L]
check(layers == want, "the Slate dialog border is #{layers.inspect}, not #{want.inspect}")
g = glyph(raster, 'chalk', 'dialog-left')
check((0...11).map { |x| ink.call(g, x, 10) } == [false] * 4 + [true, false, true, true, true, false, false],
      'the Chalk dialog border is not outer line, gap, 3 px line, margin')
g = glyph(raster, 'chalk', 'dialog-right')
check((0...11).map { |x| ink.call(g, x, 0) } == [false, false, true, true, true, false, true, false, false, false, false],
      'the dialog right edge is not margin, 3 px line, gap, outer line, with nothing beyond')
g = glyph(raster, 'chalk', 'dialog-bottom-right')
check(ink.call(g, 6, 6) && !ink.call(g, 7, 7) && !ink.call(g, 8, 2) && !ink.call(g, 2, 8), 'the dialog corner draws a shadow')
%w[button default-button].each do |kind|
  thick = kind == 'default-button' ? 3 : 1
  %w[left right].each do |side|
    g = glyph(raster, 'chalk', "#{kind}-#{side}")
    seam = side == 'left' ? 10 : 0
    far = side == 'left' ? 0 : 10
    rows = (0...22).select { |y| ink.call(g, seam, y) }
    check(rows == (0...thick).to_a + (22 - thick...22).to_a,
          "#{kind}-#{side}: the edge the label joins is inked at #{rows.inspect}, not #{thick} px at top and bottom")
    check((0...22).none? { |y| ink.call(g, far, y) } && !ink.call(g, far, 0), "#{kind}-#{side}: the end is not rounded and inset")
    check(ink.call(g, side == 'left' ? 2 : 8, 11), "#{kind}-#{side}: the end's side is not 2 px in")
  end
end

# Footprints, at every size and in every set: a frame piece inks only its
# 1 px line against the content, a dialog piece only its 7 px border
# (2 margin, 3 line, 1 gap, 1 outer). Nothing else, so no shadow.
FRAME_LINE = {
  'frame-left' => ->(x, y, w, h) { x == w - 1 }, 'frame-right-top' => ->(x, y, w, h) { x.zero? },
  'frame-right' => ->(x, y, w, h) { x.zero? }, 'frame-bottom-left' => ->(x, y, w, h) { x == w - 1 && y.zero? },
  'frame-bottom-first' => ->(x, y, w, h) { y.zero? }, 'frame-bottom' => ->(x, y, w, h) { y.zero? },
  'frame-bottom-right' => ->(x, y, w, h) { x.zero? && y.zero? }, 'frame-top-left' => ->(x, y, w, h) { x == w - 1 && y == h - 1 },
  'frame-top' => ->(x, y, w, h) { y == h - 1 }, 'frame-top-right' => ->(x, y, w, h) { x.zero? && y == h - 1 }
}.freeze
SIZES.each do |w, h|
  SETS.each_key do |style|
    FRAME_LINE.each do |name, line|
      g = glyph(raster, style, name, w, h)
      (0...h).each do |y|
        (0...w).each do |x|
          check((g[y][x][0] == 255) == line.call(x, y, w, h), "#{style}-#{name} at #{w}x#{h}: pixel #{x},#{y} is off its line")
        end
      end
    end
    %w[top-left top top-right left right bottom-left bottom bottom-right].each do |part|
      g = glyph(raster, style, "dialog-#{part}", w, h)
      sides = part.split('-')
      (0...h).each do |y|
        (0...w).each do |x|
          dx = sides.include?('left') ? w - 1 - x : sides.include?('right') ? x : 0
          dy = sides.include?('top') ? h - 1 - y : sides.include?('bottom') ? y : 0
          d = [dx, dy].max
          inked = g[y][x][0] == 255
          # Chalk's margin and gap are its cell's background; Slate's are
          # the line pen, the dialog's face (ADR 0211).
          want = style == 'chalk' ? (d.between?(2, 4) || d == 6) : d < 7
          check(inked == want, "#{style}-dialog-#{part} at #{w}x#{h}: pixel #{x},#{y} (#{d} px out) is #{inked ? 'set' : 'clear'}")
        end
      end
    end
  end
end

# The outer-edge frame (ADR 0214), written out here, not read from the
# design. Formats first: the frame cells and Chalk's title ends are coverage
# in the foreground, Slate's title ends are pens.
frame_pixels = ->(name, w, h) { pixels(raster, w, h, FRAME_FIRST + FRAME_NAMES.index(name)) }
SIZES.each do |w, h|
  FRAME_NAMES.each_with_index do |name, i|
    fmt, = raster.fetch([[w, h], FRAME_FIRST + i])
    check(fmt == (name.start_with?('slate') ? 'roles' : 'coverage'), "#{name} is #{fmt}")
    check(pixels(raster, w, h, FRAME_FIRST + i).flatten(1).all? { |a, _| [0, 255].include?(a) }, "#{name} at #{w}x#{h} has a partial pixel")
  end
  # The frame cells: the line on the outermost edge, and no ink anywhere
  # else, so the cell's own background is the padding.
  on_line = {
    'frame-left' => ->(x, y) { x.zero? }, 'frame-right' => ->(x, y) { x == w - 1 },
    'frame-bottom-left' => ->(x, y) { x.zero? || y == h - 1 }, 'frame-bottom' => ->(x, y) { y == h - 1 },
    'frame-bottom-right' => ->(x, y) { x == w - 1 || y == h - 1 }
  }
  on_line.each do |name, line|
    g = frame_pixels.call(name, w, h)
    (0...h).each do |y|
      (0...w).each do |x|
        check((g[y][x][0] == 255) == line.call(x, y), "#{name} at #{w}x#{h}: pixel #{x},#{y} is #{g[y][x][0] == 255 ? 'inked off' : 'clear on'} the outermost edge's line")
      end
    end
  end
  # The title ends. Stripe rows are where the plain stripe cell has ink in
  # its middle column besides its frame line and rule.
  chalk_stripe = glyph(raster, 'chalk', 'stripe', w, h)
  rows = (1...h - 1).select { |y| chalk_stripe[y][w / 2][0] == 255 }
  check(rows.size.positive?, "no stripe rows at #{w}x#{h}")
  { 'left' => 0, 'right' => w - 1 }.each do |side, edge|
    far = side == 'left' ? w - 1 : 0 # the column that meets the next cell
    inner = side == 'left' ? (2...w).to_a : (0...w - 2).to_a
    chalk = frame_pixels.call("chalk-title-end-#{side}", w, h)
    slate = frame_pixels.call("slate-light-title-end-#{side}", w, h)
    (0...h).each do |y|
      (0...w).each do |x|
        want = x == edge || y.zero? || y == h - 1 || (rows.include?(y) && inner.include?(x))
        check((chalk[y][x][0] == 255) == want, "chalk-title-end-#{side} at #{w}x#{h}: pixel #{x},#{y} is #{want ? 'clear' : 'inked'}")
      end
    end
    # 2 px clear: the first stripe pixel is INSET (2) px from the outer edge.
    rows.each do |y|
      xs = (0...w).select { |x| chalk[y][x][0] == 255 && x != edge }
      check(xs == inner, "chalk-title-end-#{side} at #{w}x#{h}: stripe row #{y} inks #{xs.inspect}, not #{inner.first}..#{inner.last}")
    end
    # The same ink in Slate: its foreground, and its mix in the stripe rows.
    (0...h).each do |y|
      (0...w).each do |x|
        role = slate[y][x][1]
        ink_now = role == 'K' || (role == 'M' && rows.include?(y))
        check((chalk[y][x][0] == 255) == ink_now, "slate-light-title-end-#{side} at #{w}x#{h}: pixel #{x},#{y} is #{role.inspect}, Chalk has #{chalk[y][x][0]}")
      end
    end
    # Slate's bar: the outer line, the line and rule, the highlight under
    # the line and the shade over the rule (both start beside the outer
    # line), a dark stripe over a highlight row, the bar (no pixel) between.
    check((0...h).all? { |y| slate[y][edge][1] == 'K' }, "slate-light-title-end-#{side} at #{w}x#{h}: the outer line is not the foreground")
    check((0...w).all? { |x| slate[0][x][1] == 'K' && slate[h - 1][x][1] == 'K' }, "slate-light-title-end-#{side} at #{w}x#{h}: no line and rule")
    (0...w).reject { |x| x == edge }.each do |x|
      check(slate[1][x][1] == 'L' && slate[h - 2][x][1] == 'M', "slate-light-title-end-#{side} at #{w}x#{h}: no highlight and shade at #{x}")
    end
    rows.each do |y|
      inner.each do |x|
        check(slate[y][x][1] == 'M' && slate[y + 1][x][1] == 'L', "slate-light-title-end-#{side} at #{w}x#{h}: stripe at #{x},#{y} is not a dark stripe over a highlight")
      end
    end
    # Seamless: the column that meets the next cell is that cell's stripe
    # column, row for row, in both styles.
    { 'chalk' => chalk, 'slate-light' => slate }.each do |style, e|
      plain = glyph(raster, style, 'stripe', w, h)
      (0...h).each do |y|
        check(e[y][far] == plain[y][far], "#{style}-title-end-#{side} at #{w}x#{h}: row #{y} does not join the stripe cell")
      end
    end
  end
end
# The plain title ends (0.3.45), the inactive bar's: the title-end cell without
# stripes. Written out: the outer line down the outer edge, the top line and
# rule across, nothing else in Chalk; in Slate also the bevel (highlight under
# the line, shade over the rule) and no pixel for the bar. Beside the plain
# `title` bar cell it seams at every size: the column that meets it is that
# cell's column, row for row, and every other column is the title cell's.
SIZES.each do |w, h|
  %w[chalk slate-light].each do |style|
    title = glyph(raster, style, 'title', w, h)
    { 'left' => 0, 'right' => w - 1 }.each do |side, edge|
      far = side == 'left' ? w - 1 : 0
      plain = frame_pixels.call("#{style}-title-end-plain-#{side}", w, h)
      (0...h).each do |y|
        check(plain[y][far] == title[y][far], "#{style}-title-end-plain-#{side} at #{w}x#{h}: row #{y} does not join the title cell")
        (0...w).each do |x|
          want = x == edge ? [255, style == 'chalk' ? nil : 'K'] : title[y][x]
          check(plain[y][x] == want, "#{style}-title-end-plain-#{side} at #{w}x#{h}: pixel #{x},#{y} is #{plain[y][x].inspect}, want #{want.inspect}")
        end
      end
      stripes = frame_pixels.call("#{style}-title-end-#{side}", w, h)
      check((0...h).count { |y| (0...w).count { |x| stripes[y][x] != plain[y][x] } }.positive?, "#{style}-title-end-#{side} has no stripes the plain cell lacks")
    end
  end
end
g = frame_pixels.call('chalk-title-end-plain-left', 11, 22)
check((0...22).select { |y| g[y][5][0] == 255 } == [0, 21] && (0...22).select { |y| g[y][0][0] == 255 } == (0...22).to_a &&
      (1...11).all? { |x| [1, 20].none? { |y| g[y][x][0] == 255 } },
      'chalk-title-end-plain-left at 11x22 is not the outer line, top line and rule alone')
g = frame_pixels.call('slate-light-title-end-plain-right', 11, 22)
check((0...22).map { |y| g[y][10][1] } == ['K'] * 22 && (0...22).map { |y| g[y][5][1] } == ['K', 'L'] + [nil] * 18 + ['M', 'K'],
      'slate-light-title-end-plain-right at 11x22 is not the outer line and bevel of the plain bar')

# At the owner's 11x22, by eye: the left cell's stripe rows run from x 2 to
# its right edge (x 1 clear), the right cell's from its left edge to x 8.
g = frame_pixels.call('chalk-title-end-left', 11, 22)
check((0...22).select { |y| g[y][1][0] == 255 } == [0, 21] && (0...22).select { |y| g[y][2][0] == 255 } == [0, *STRIPE_ROWS, 21],
      'chalk-title-end-left at 11x22: stripes do not start at x 2 on rows 5-15')
g = frame_pixels.call('chalk-title-end-right', 11, 22)
check((0...22).select { |y| g[y][9][0] == 255 } == [0, 21] && (0...22).select { |y| g[y][8][0] == 255 } == [0, *STRIPE_ROWS, 21],
      'chalk-title-end-right at 11x22: stripes do not stop at x 8 on rows 5-15')
g = frame_pixels.call('slate-light-title-end-left', 11, 22)
check((0...22).map { |y| g[y][0][1] } == ['K'] * 22 && (0...22).map { |y| g[y][1][1] } == ['K', 'L'] + [nil] * 18 + ['M', 'K'] &&
      (0...22).map { |y| g[y][5][1] } == %w[K L] + [nil] * 3 + %w[M L M L M L M L M L M L] + [nil] * 3 + %w[M K],
      'slate-light-title-end-left at 11x22 is not the outer line, bevel and stripes of the approved bar')

# Pewter: Slate in the cell's pens, against 0.3.38's fixed Slate light
# (tools/fixtures, made by 0.3.38's own design script). Each glyph sits on
# the background Hibr gives it: the bar 214, a note's close box 238; frame
# pieces have no background role. A pixel may differ only where it is the
# MIX pen, whose half of fg and bg replaced the palette's fixed greys. The
# dialog and the button caps are not compared: ADR 0211 redrew them.
pewter_bg = ->(name) { name == 'note-close' ? [238, 238, 238] : [214, 214, 214] }
old = {}
File.foreach('tools/tests/fixtures/window-slate-light-0.3.38-11x22.txt') do |line|
  cp, _, hex = line.split
  old[cp.to_i(16)] = [hex].pack('H*').bytes.each_slice(4).to_a
end
check(old.size == 35, 'the 0.3.38 Slate light reference is incomplete')
mix_differs = 0
compared = 0
NAMES.each_with_index do |name, i|
  next if name.start_with?('dialog-') || name.include?('button-')

  cp = SETS.fetch('slate-light') + i
  bg = pewter_bg.call(name)
  pen = { 'K' => [0, 0, 0], 'L' => [255, 255, 255], 'M' => bg.map { |c| c / 2 } }
  g = pixels(raster, 11, 22, cp)
  compared += 1
  22.times do |y|
    11.times do |x|
      was = old.fetch(cp)[y * 11 + x]
      role = g[y][x][1]
      now = role ? pen.fetch(role) : (was[3].zero? ? nil : bg)
      then_ = was[3].zero? ? nil : was[0, 3]
      next if now == then_
      check(role == 'M', "Pewter slate-light-#{name} at #{x},#{y} is #{now.inspect}, 0.3.38 drew #{then_.inspect}")
      mix_differs += 1
    end
  end
end
check(compared == 23, "Pewter compared #{compared} glyphs, not the 23 that ADR 0211 left alone")

# The dialog (ADR 0211): its cells keep the wallpaper as their background,
# so Slate draws the face itself, as the line pen (SGR 58 the dialog's face):
# only the foreground and the line pen, no MIX, and nothing at all outside
# the border's 7 px. The margin (2 px) and the gap (1 px) are the line pen;
# the 3 px line and the 1 px outer line are the foreground.
%w[top-left top top-right left right bottom-left bottom bottom-right].each do |part|
  sides = part.split('-')
  g = pixels(raster, 11, 22, SETS.fetch('slate-light') + NAMES.index("dialog-#{part}"))
  22.times do |y|
    11.times do |x|
      dx = sides.include?('left') ? 10 - x : sides.include?('right') ? x : 0
      dy = sides.include?('top') ? 21 - y : sides.include?('bottom') ? y : 0
      d = [dx, dy].max
      want = if d >= 7 then nil elsif d.between?(2, 4) || d == 6 then 'K' else 'L' end
      check(g[y][x][1] == want, "slate-dialog-#{part} pixel #{x},#{y} (#{d} px out) is #{g[y][x][1].inspect}, want #{want.inspect}: " \
                                'a fixed colour or a halo outside the border')
    end
  end
end

# The button caps (ADR 0211): they sit on the dialog's face, so the corners
# outside the ring draw nothing and the dialog's face shows there; the face
# inside the ring is the line pen (SGR 58 the button's face); no highlight;
# the shade (MIX) only where the end's edge faces down or right.
%w[button default-button].each do |kind|
  thick = kind == 'default-button' ? 3 : 1
  %w[left right].each do |side|
    g = pixels(raster, 11, 22, SETS.fetch('slate-light') + NAMES.index("#{kind}-#{side}"))
    roles = g.flat_map { |row| row.map { |_, r| r } }
    check(roles.include?('L') && roles.include?('K') && roles.include?('M'), "#{kind}-#{side}: not ring, face and shade")
    outer = side == 'left' ? (0...2) : (9...11)
    check(22.times.all? { |y| outer.all? { |x| g[y][x][1].nil? } }, "#{kind}-#{side}: the 2 px inset is not clear")
    corner = side == 'left' ? (2...7) : (4...9)
    check([0, 21].all? { |y| corner.all? { |x| g[y][x][1].nil? } },
          "#{kind}-#{side}: the corners outside the ring are not clear, so the dialog's face would not show")
    # The face is one pen, solid from the ring to the seam where the label
    # begins (no second, highlight, pen): every pixel of the middle row
    # past the ring is drawn.
    span = side == 'left' ? (2...11) : (0...9)
    check(span.all? { |x| g[11][x][1] }, "#{kind}-#{side}: the face is not solid from the ring to the seam")
    m = (0...22).flat_map { |y| (0...11).select { |x| g[y][x][1] == 'M' }.map { |x| [x, y] } }
    if side == 'left'
      check(m.all? { |_, y| y > 11 }, "#{kind}-left has a shade on its upper half: #{m.first(4).inspect}")
    else
      check(m.any? { |_, y| y < 11 } && m.all? { |x, y| x >= 4 || y > 11 }, "#{kind}-right's shade is not down and right")
    end
  end
end

# Seams, at every size: the frame and rule cross every bar cell's edges,
# and the stripes in neighbouring cells sit on the same rows.
bar = %w[stripe stripe-first stripe-last stripe-before-title stripe-after-title title stripe-to-box
         stripe-from-box stripe-between close shade zoom]
SIZES.each do |w, h|
  SETS.each_key do |style|
    reference = glyph(raster, style, 'stripe', w, h)
    bar.each do |name|
      g = glyph(raster, style, name, w, h)
      [0, w - 1].each do |x|
        check(g[0][x] == reference[0][x] && g[h - 1][x] == reference[h - 1][x] && g[0][x][0] == 255,
              "#{style}-#{name} at #{w}x#{h}: the frame or rule breaks at x #{x}")
        # A box is the cell's full width: its neighbours stop short of it.
        next if %w[close shade zoom].include?(name)

        (0...h).each do |y|
          check(g[y][x] == reference[y][x] || g[y][x] == glyph(raster, style, 'title', w, h)[y][x],
                "#{style}-#{name} at #{w}x#{h}: row #{y} at its edge is neither a stripe nor the bar")
        end
      end
    end
    (0...h).each do |y|
      check(reference[y][0] == reference[y][w - 1], "#{style}-stripe at #{w}x#{h}: row #{y} does not run edge to edge")
    end
    stripes = (0...h).count { |y| reference[y][w / 2] != glyph(raster, style, 'title', w, h)[y][w / 2] }
    check(stripes.positive?, "#{style} at #{w}x#{h} has no stripes")
    left = glyph(raster, style, 'frame-left', w, h)
    check((0...h).all? { |y| left[y][w - 1][0] == 255 && left[y][0][0].zero? }, "#{style} frame-left at #{w}x#{h}")
    # Lines stay one pixel at every size (ADR 0210).
    check((0...h).count { |y| left[y][w - 2][0] == 255 }.zero?, "#{style} frame-left at #{w}x#{h} is wider than 1 px")
  end
end

puts "WINDOW_GLYPH_PASS glyphs=70+13_frame(U+1002AE-1002BA) outer_edge=line_only title_ends=stripes_join,2px_clear,plain_ends_seam_with_title " \
     "slate_dark=retired footprints=lines_only shadow=none catalogue=named " \
     "sizes=#{SIZES.map { |s| s.join('x') }.join(',')} design=pixel-exact chalk=coverage slate=pen_planes " \
     "pewter=0.3.38_except_mix(#{mix_differs}_px,#{compared}_glyphs) dialog=no_halo,line+fg_only caps=corners_clear " \
     'pixels=crisp mockup_11x22=held seams=joined'
