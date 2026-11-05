import re
R = '/tmp/claude-2000/-home-osakka-hw-manager/51f0679e-9abf-43d8-a616-451debc87eb3/scratchpad/frame3/'


def edit(path, pairs):
    s = open(R + path).read()
    for a, b in pairs:
        assert a in s, (path, a[:60])
        s = s.replace(a, b, 1)
    open(R + path, 'w').write(s)


edit('tools/tests/window_glyph_gate.c', [
    ("#define FRAME_LAST 0x1002B6U", "#define FRAME_LAST 0x1002BAU /* 0.3.45: four plain title ends, U+1002B7-1002BA */"),
    ("#define FRAME_SLATE 0x1002B5U", "#define FRAME_SLATE 0x1002B5U /* Slate's title ends, U+1002B5-B6, and the plain ones, U+1002B9-BA */"),
    ("pen_drawn = (cp >= SLATE && cp <= LAST) || cp >= FRAME_SLATE;",
     "pen_drawn = (cp >= SLATE && cp <= LAST) || cp == 0x1002B5U || cp == 0x1002B6U || cp == 0x1002B9U || cp == 0x1002BAU;"),
])

edit('tools/tests/window_glyph_gate.rb', [
    ("U+1002AE-1002B6: nine more glyphs. The", "U+1002AE-1002BA: thirteen more glyphs (the last four, 0.3.45, are\n#   the plain title ends, below). The"),
    ("""                 slate-light-title-end-right].freeze""", """                 slate-light-title-end-right chalk-title-end-plain-left chalk-title-end-plain-right
                 slate-light-title-end-plain-left slate-light-title-end-plain-right].freeze"""),
    ("FRAME_SETS = %w[frame frame frame frame frame chalk chalk slate-light slate-light].freeze",
     "FRAME_SETS = %w[frame frame frame frame frame chalk chalk slate-light slate-light chalk chalk slate-light slate-light].freeze"),
    ("FRAME_PENS = %w[foreground foreground foreground foreground foreground foreground foreground cell cell].freeze",
     "FRAME_PENS = %w[foreground foreground foreground foreground foreground foreground foreground cell cell foreground foreground cell cell].freeze"),
    ("between?(FRAME_FIRST, FRAME_FIRST + 8)", "between?(FRAME_FIRST, FRAME_FIRST + 12)"),
    ("does not list the nine outer-edge frame glyphs (U+1002AE-1002B6)", "does not list the thirteen outer-edge frame glyphs (U+1002AE-1002BA)"),
    ("raster.size == 79 * SIZES.size, \"the raster drew #{raster.size} of #{79 * SIZES.size}\"",
     "raster.size == 83 * SIZES.size, \"the raster drew #{raster.size} of #{83 * SIZES.size}\""),
    ("""# At the owner's 11x22, by eye: the left cell's stripe rows""", """# The plain title ends (0.3.45), the inactive bar's: the title-end cell without
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

# At the owner's 11x22, by eye: the left cell's stripe rows"""),
])
