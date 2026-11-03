#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0210: the window-chrome glyph sets, Chalk and Slate (U+100231-100299).
#
# - The catalogue lists all 105 by name, as cell geometry, in three sets of
#   35 in one order, clear of the icons and of every retired codepoint.
# - Through the production raster, every glyph equals the design
#   (tools/fonts/draw-window-glyphs.py, which reads the geometry table in
#   src/blit_window_glyphs.h) pixel for pixel: at the owner's 11x22 cell
#   and at seven other cell sizes.
# - Chalk is coverage (the foreground), Slate premultiplied RGBA in its
#   own colours; every pixel is fully set or fully transparent, and the
#   frame cells are transparent off their lines.
# - At 11x22 the approved mock-up's pixels hold, written here, not read
#   from the design: stripe and bevel rows, the full-width 11x11 box, the
#   insets, the shadow's offset, the dialog's layers, the buttons' ends.
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
SETS = { 'chalk' => 0x100231, 'slate-light' => 0x100254, 'slate-dark' => 0x100277 }.freeze

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
window = geometry.select { |e| SETS.keys.any? { |s| e['id'].start_with?("#{s}-") } }
expected = SETS.flat_map do |style, first|
  NAMES.each_with_index.map { |name, i| ["#{style}-#{name}", format('%06X', first + i), style != 'chalk'] }
end
check(window.map { |e| [e['id'], e['codepoint'], e['colour']] } == expected,
      'the catalogue does not list the 105 window glyphs in order with their names')
check(window.all? { |e| e['shape'].is_a?(String) && !e['shape'].empty? }, 'a window glyph has no shape')
icon_codes = catalogue.fetch('icons').map { |i| i['codepoint'].to_i(16) }
retired = catalogue.fetch('retired').flat_map { |r| (r['from'].to_i(16)..r['to'].to_i(16)).to_a }
codes = window.map { |e| e['codepoint'].to_i(16) }
check((codes & icon_codes).empty? && (codes & retired).empty?, 'a window glyph reuses an icon or retired codepoint')
check(codes == (0x100231..0x100299).to_a, 'the window glyphs are not U+100231-100299')
check(!(icon_codes + geometry.map { |e| e['codepoint'].to_i(16) }).include?(0x10029A), 'U+10029A is no longer free')

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
check(raster.size == 105 * SIZES.size, "the raster drew #{raster.size} of #{105 * SIZES.size}")
mismatches = []
raster.each do |key, got|
  mismatches << format('U+%X at %dx%d', key[1], *key[0]) unless design[key] == got
end
check(mismatches.empty?, "differs from the design: #{mismatches.first(8).join(', ')}#{mismatches.size > 8 ? " and #{mismatches.size - 8} more" : ''}")

# Pixels as [alpha or coverage, rgb] rows.
def pixels(raster, w, h, cp)
  fmt, hex = raster.fetch([[w, h], cp])
  bytes = [hex].pack('H*').bytes
  if fmt == 'coverage'
    Array.new(h) { |y| Array.new(w) { |x| [bytes[y * w + x], nil] } }
  else
    Array.new(h) { |y| Array.new(w) { |x| b = bytes[(y * w + x) * 4, 4]; [b[3], b[0, 3]] } }
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
      check(fmt == (style == 'chalk' ? 'coverage' : 'rgba'), "#{style}-#{NAMES[i]} is #{fmt}")
      check(pixels(raster, w, h, first + i).flatten(1).all? { |a, _| [0, 255].include?(a) },
            "#{style}-#{NAMES[i]} at #{w}x#{h} has a partial pixel")
    end
  end
end

# The approved mock-up at 11x22, written out here.
ink = ->(g, x, y) { g[y][x][0] == 255 }
rgb = ->(g, x, y) { g[y][x][1] }
STRIPE_ROWS = [5, 7, 9, 11, 13, 15].freeze
LIGHT = { K: [0, 0, 0], D: [119, 119, 119], L: [255, 255, 255], B: [214, 214, 214], W: [255, 255, 255],
          S: [0, 0, 0], G: [238, 238, 238] }.freeze
# Colours in the raster's byte order: Slate dark's frame is (8, 8, 10).
g = glyph(raster, 'slate-dark', 'stripe')
check(rgb.call(g, 0, 0) == [8, 8, 10] && rgb.call(g, 0, 2) == [60, 60, 67], "Slate dark's colours are #{rgb.call(g, 0, 0)}")
# Slate dark's controls read on its faces: the button ring on the button
# face (78,78,86), the dialog's outer and wide lines on the dialog face
# (52,52,58), each at least 60 levels apart.
luma = ->(c) { (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000 }
ring = glyph(raster, 'slate-dark', 'button-left')
check(luma.call(rgb.call(ring, 2, 11)) - luma.call([78, 78, 86]) >= 60, "Slate dark's button ring #{rgb.call(ring, 2, 11)} does not read on its face")
ring = glyph(raster, 'slate-dark', 'default-button-left')
check(luma.call(rgb.call(ring, 3, 11)) - luma.call([78, 78, 86]) >= 60, "Slate dark's default ring #{rgb.call(ring, 3, 11)} does not read")
border = glyph(raster, 'slate-dark', 'dialog-left')
[4, 6, 8].each do |x|
  check(luma.call(rgb.call(border, x, 10)) - luma.call([52, 52, 58]) >= 60,
        "Slate dark's dialog line at x #{x} (#{rgb.call(border, x, 10)}) does not read on its face")
end
g = glyph(raster, 'chalk', 'stripe')
check((0...22).select { |y| ink.call(g, 5, y) } == [0, *STRIPE_ROWS, 21], 'Chalk stripes are not rows 5-15 with the frame and rule')
g = glyph(raster, 'slate-light', 'stripe')
column = (0...22).map { |y| LIGHT.key(rgb.call(g, 5, y)) }
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
check((0...22).all? { |y| ink.call(g, 0, y) } && !ink.call(g, 1, 1) && ink.call(g, 1, 2) && ink.call(g, 2, 21) && !ink.call(g, 3, 5),
      'frame-right-top: no line, or its shadow is not 2 px wide from 2 px down')
g = glyph(raster, 'chalk', 'frame-left')
check((0...22).all? { |y| ink.call(g, 10, y) && !ink.call(g, 0, y) }, 'frame-left does not hug the content, transparent elsewhere')
g = glyph(raster, 'slate-light', 'frame-bottom-first')
check(rgb.call(g, 0, 0) == LIGHT[:K] && g[1][0][0].zero? && rgb.call(g, 1, 1) == LIGHT[:S] && rgb.call(g, 1, 2) == LIGHT[:S] && g[3][5][0].zero?,
      'frame-bottom-first: the shadow does not start 2 px from the left frame')
g = glyph(raster, 'slate-light', 'dialog-left')
layers = (0...11).map { |x| g[10][x][0].zero? ? :none : LIGHT.key(rgb.call(g, x, 10)) }
want = %i[none none none none K G D L D G G]
check(layers == want, "the Slate dialog border is #{layers.inspect}, not #{want.inspect}")
g = glyph(raster, 'chalk', 'dialog-left')
check((0...11).map { |x| ink.call(g, x, 10) } == [false] * 4 + [true, false, true, true, true, false, false],
      'the Chalk dialog border is not outer line, gap, 3 px line, margin')
g = glyph(raster, 'chalk', 'dialog-right')
check((0...11).map { |x| ink.call(g, x, 0) } == [false, false, true, true, true, false, true, true, true, false, false],
      'the dialog right edge does not carry its 2 px shadow')
g = glyph(raster, 'chalk', 'dialog-top-right')
check(!ink.call(g, 8, 16) && ink.call(g, 8, 17) && ink.call(g, 7, 21), 'the dialog shadow does not start 2 px below its top')
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

puts "WINDOW_GLYPH_PASS glyphs=105 catalogue=named sizes=#{SIZES.map { |s| s.join('x') }.join(',')} " \
     'design=pixel-exact chalk=coverage slate=rgba pixels=crisp mockup_11x22=held seams=joined'
