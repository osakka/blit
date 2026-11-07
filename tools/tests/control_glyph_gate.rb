#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0212: the controls glyphs, U+10029A-1002AD: a checkbox (Chalk, Slate),
# a compact switch (Chalk, Slate) and a capsule switch.
#
# - The catalogue lists all 20 in order with their names, sets and pens, in
#   `cell_geometry`, clear of the icons and every retired codepoint, and
#   U+1002AE-1002BA are the outer-edge frame's (ADR 0214, its own gate), and
#   U+1002BB-1002C4 are the pressed boxes and grow cells (ADR 0219, its own gate), and
#   U+1002C5-1002C6 are the hui mark and U+1002C7-1002CC the wiretap icons
#   (on, off; two-column, then one-column colour icons), U+1002CD-1002D0 the minimise boxes (ADR 0222, window_glyph_gate), U+1002D1-1002D9 the Wi-Fi
#   signal (1-4, off) and cellular (1-4) icons, U+1002DA-1002EB the two-column Wi-Fi
#   (wifi-signal-wide-1..4, wifi-off-wide) and volume (volume-wide-0..3) icons, U+1002EC-1002F3 the
#   two-column classic phones (phone-classic-wide-off, -wait, -on, -ring), U+1002F4-100309 the two-column
#   batteries (battery-wide-0..4, -0..4-charging, -ac), and U+10030A is the next free.
# - Through the production raster, every glyph equals the design
#   (tools/fonts/draw-control-glyphs.py, which reads the geometry tables in
#   src/blit_control_glyphs.h and src/blit_window_glyphs.h) pixel for
#   pixel, at the owner's 11x22 cell and at eleven other cell sizes. Chalk
#   as coverage; Slate and the capsule as role maps read from their three
#   pen planes (FG, LINE, MIX), which the harness requires to be disjoint
#   and together the one-colour lookup. Chalk has no planes. The
#   codepoints past the set draw nothing of their own.
# - At 11x22 the approved mock-ups' pixels hold, written here, not read
#   from the design.
# - The checkbox's three states differ from each other wherever its box has
#   room for the marks, and from off at every size; a switch's cells join
#   across their seam; the capsule has its track, knob and edge pens at
#   every size, its knob inside its track, and carries no colour of its own.

require 'json'
require 'open3'
require 'tmpdir'

ROOT = File.expand_path('../..', __dir__)
SIZES = [[11, 22], [4, 8], [5, 10], [6, 12], [8, 16], [9, 18], [10, 20], [12, 24], [14, 28], [16, 32],
         [22, 44], [32, 64]].freeze
FIRST = 0x10029A
NEXT_FREE = 0x1002AE
IDS = %w[chalk-check-off chalk-check-on chalk-check-mixed
         slate-check-off slate-check-on slate-check-mixed
         chalk-switch-off-left chalk-switch-off-right chalk-switch-on-left chalk-switch-on-right
         slate-switch-off-left slate-switch-off-right slate-switch-on-left slate-switch-on-right
         capsule-off-left capsule-off-mid capsule-off-right capsule-on-left capsule-on-mid capsule-on-right].freeze
SETS = %w[chalk chalk chalk slate slate slate chalk chalk chalk chalk slate slate slate slate
          capsule capsule capsule capsule capsule capsule].freeze
CHALK = IDS.each_index.select { |i| SETS[i] == 'chalk' }.freeze

def check(ok, why)
  abort "CONTROL_GLYPH_FAIL #{why}" unless ok
end

Dir.chdir(ROOT)

# The catalogue.
catalogue = JSON.parse(File.read('assets/fonts/icons/catalogue.json'))
geometry = catalogue.fetch('cell_geometry')
mine = geometry.select { |e| e['codepoint'].to_i(16).between?(FIRST, NEXT_FREE - 1) }
check(mine.map { |e| [e['id'], e['codepoint'], e['colour'], e['set'], e['pens']] } ==
      IDS.each_with_index.map { |id, i| [id, format('%06X', FIRST + i), false, SETS[i], SETS[i] == 'chalk' ? 'foreground' : 'cell'] },
      'the catalogue does not list the 20 controls in order with their names, sets and pens')
check(mine.all? { |e| e['shape'].is_a?(String) && !e['shape'].empty? }, 'a control has no shape')
icon_codes = catalogue.fetch('icons').map { |i| i['codepoint'].to_i(16) }
retired = catalogue.fetch('retired').flat_map { |r| (r['from'].to_i(16)..r['to'].to_i(16)).to_a }
check(((FIRST...NEXT_FREE).to_a & (icon_codes + retired)).empty?, 'a control reuses an icon or retired codepoint')
NEXT_FREE_ALL = 0x10030A # past the outer-edge frame (ADR 0214), the pressed states (ADR 0219), the hui mark, wiretap, the minimise boxes (ADR 0222), Wi-Fi signal and cellular, the two-column Wi-Fi and volume, the two-column classic phones, and the two-column batteries
check((icon_codes + geometry.map { |e| e['codepoint'].to_i(16) }).none? { |c| c >= NEXT_FREE_ALL },
      "something is mapped at or past U+#{NEXT_FREE_ALL.to_s(16).upcase}, the next free codepoint")

# The raster, at every size.
freetype = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{ROOT}/build/deps/prefix/include/freetype2")
raster = {}
design = {}
Dir.mktmpdir('blit-control-glyphs') do |work|
  binary = File.join(work, 'gate')
  check(system('tcc', '-O2', '-std=c99', '-Wall', "-I#{freetype}",
               'tools/tests/control_glyph_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
               ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
               ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),
               '-o', binary), 'the harness does not compile')
  out, err, status = Open3.capture3(binary, 'assets/fonts/Blit-Terminus16.psf',
                                    'assets/fonts/text/Hack-Regular.ttf', 'assets/fonts/NotoColorEmoji.ttf',
                                    *SIZES.flatten.map(&:to_s))
  check(status.success?, "the raster harness failed: #{err.lines.grep(/CONTROL_GLYPH_FAIL/).first || err.strip}")
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
    text, err, status = Open3.capture3('python3', 'tools/fonts/draw-control-glyphs.py', '--reference', w.to_s, h.to_s)
    check(status.success?, "the design does not run: #{err.strip}")
    text.each_line do |line|
      cp, fmt, data = line.split
      design[[[w, h], cp.to_i(16)]] = [fmt, data]
    end
  end
end
check(raster.size == 20 * SIZES.size, "the raster drew #{raster.size} of #{20 * SIZES.size}")
mismatches = raster.reject { |key, got| design[key] == got }.keys.map { |(s, cp)| format('U+%X at %dx%d', cp, *s) }
check(mismatches.empty?, "differs from the design: #{mismatches.first(8).join(', ')}#{mismatches.size > 8 ? " and #{mismatches.size - 8} more" : ''}")

# Role maps: a glyph as rows of characters, '.' nothing. Chalk is coverage,
# shown as K where inked.
def rows(raster, w, h, index)
  fmt, data = raster.fetch([[w, h], FIRST + index])
  if fmt == 'coverage'
    bytes = [data].pack('H*').bytes
    Array.new(h) { |y| Array.new(w) { |x| bytes[y * w + x] == 255 ? 'K' : '.' }.join }
  else
    Array.new(h) { |y| data[y * w, w] }
  end
end

IDS.each_index do |i|
  SIZES.each do |w, h|
    fmt, = raster.fetch([[w, h], FIRST + i])
    check(fmt == (SETS[i] == 'chalk' ? 'coverage' : 'roles'), "#{IDS[i]} is #{fmt} at #{w}x#{h}")
  end
end

# The approved mock-ups at 11x22, written out here. A glyph's pixels as a
# set of [x, y] for each role.
def at(raster, index, w = 11, h = 22)
  g = rows(raster, w, h, index)
  Hash.new { |hh, k| hh[k] = [] }.tap { |by| g.each_with_index { |r, y| r.each_char.with_index { |c, x| by[c] << [x, y] unless c == '.' } } }
end
box = ->(x0 = 0) { (0..10).flat_map { |i| [[x0 + i, 5], [x0 + i, 15], [x0, 5 + i], [x0 + 10, 5 + i]] }.uniq.sort }

# Checkbox: an 11x11 square on rows 5..15.
c = at(raster, 0)
check(c['K'].sort == box.call, 'chalk-check-off is not a plain 1 px, 11x11 square at rows 5-15')
x = (2..8).flat_map { |i| [[i, 5 + i], [i, 5 + 10 - i]] }.uniq
check(at(raster, 1)['K'].sort == (box.call + x).uniq.sort, 'chalk-check-on is not the square with a 1 px X from (2,2) to (8,8)')
check(at(raster, 2)['K'].sort == (box.call + (2..8).map { |i| [i, 10] }).uniq.sort, 'chalk-check-mixed is not the square with a dash at row 10')
s = at(raster, 3)
check(s['K'].sort == box.call, 'slate-check-off: the outline is not the 11x11 square')
check(s['M'].sort == ((1..9).map { |i| [i, 14] } + (6..14).map { |y| [9, y] }).uniq.sort,
      'slate-check-off: the shade is not the bottom and right inside edge')
check(s['L'].size == 9 * 9 - s['M'].size, 'slate-check-off: the face is not the line pen')
check(at(raster, 4)['K'].sort == (box.call + [[2, 10], [3, 11], [4, 12], [5, 11], [6, 10], [7, 9], [8, 8]].flat_map { |a, b| [[a, b], [a, b + 1]] }).uniq.sort,
      'slate-check-on is not the square with a 2 px check mark')
check(at(raster, 5)['K'].sort == (box.call + (2..7).flat_map { |i| [[i, 9], [i, 10]] }).uniq.sort,
      'slate-check-mixed is not the square with a heavier, 2 px dash')

# The compact switch: a 22x11 track on rows 5..15, its knob 7x7, a pixel clear
# of its ring.
off = at(raster, 6)
check(off['K'].map(&:last).minmax == [5, 15], 'chalk-switch-off-left: the track is not on rows 5-15')
check(off['K'].select { |px, py| py == 5 }.map(&:first).minmax == [3, 10], 'chalk-switch-off-left: the track top does not start 3 px in')
knob = off['K'].select { |px, py| px.between?(2, 8) && py.between?(7, 13) }
check(knob.size == 3 + 5 + 7 + 7 + 7 + 5 + 3, "chalk-switch-off-left: the knob is #{knob.size} px, not a 7x7 disc of 37")
check(knob.map(&:first).minmax == [2, 8] && knob.map(&:last).minmax == [7, 13], 'chalk-switch-off-left: the knob is not at x 2-8, y 7-13')
right = at(raster, 7)
check(right['K'].select { |_, py| py == 5 }.map(&:first).minmax == [0, 7], 'chalk-switch-off-right: the track top does not end 3 px in')
check(at(raster, 8)['K'].size > at(raster, 6)['K'].size && at(raster, 8)['K'].include?([5, 10]),
      'chalk-switch-on-left is not a filled track')
on_right = at(raster, 9)
check(!on_right['K'].include?([2, 10]) && !on_right['K'].include?([8, 10]), 'chalk-switch-on-right: the knob is not left empty at its centre')
check(at(raster, 12)['M'].size > 20, 'slate-switch-on has no mixed-grey track')
# The knob's face: a 5x5 disc (21 px) inside its ring, its last row shaded (3).
check(at(raster, 12).fetch('L', []).empty? && at(raster, 13)['L'].size == 18 && at(raster, 13)['M'].count { |_, py| py == 12 } >= 3,
      'slate-switch-on: the knob is not in the right cell, an 18 px face with a 3 px shade')
check(at(raster, 10)['L'].include?([4, 6]) && at(raster, 11)['L'].include?([17 - 11, 6]),
      'slate-switch-off: no highlight along the first row inside the ring, from x 4 to 17')
check(!at(raster, 10)['L'].include?([3, 6]) && !at(raster, 11)['L'].include?([18 - 11, 6]),
      'slate-switch-off: the highlight does not stop a pixel short of the curve')

# The capsule: track x 1..31, y 2..19, no outline; knob 14 px, 2 px in.
off_cells = (14..16).map { |i| at(raster, i) }
all_px = off_cells.each_with_index.flat_map { |g, part| g.values.flatten(1).map { |px, py| [part * 11 + px, py] } }
check(all_px.map(&:first).minmax == [1, 31] && all_px.map(&:last).minmax == [2, 19],
      "the capsule's pixels are not inside x 1-31, y 2-19: #{all_px.map(&:first).minmax} #{all_px.map(&:last).minmax}")
knob_off = off_cells.each_with_index.flat_map { |g, part| g.fetch('L', []).map { |px, py| [part * 11 + px, py] } }
on_cells = (17..19).map { |i| at(raster, i) }
knob_on = on_cells.each_with_index.flat_map { |g, part| g.fetch('L', []).map { |px, py| [part * 11 + px, py] } }
check(knob_off.map(&:first).min >= 3 && knob_off.map(&:first).max <= 17, "the off knob is at x #{knob_off.map(&:first).minmax}, not within 3-17")
check(knob_on.map(&:first).min >= 15 && knob_on.map(&:first).max <= 29, "the on knob is at x #{knob_on.map(&:first).minmax}, not within 15-29")
check(knob_off.map { |px, _| px }.sum < 16 * knob_off.size && knob_on.map { |px, _| px }.sum > 16 * knob_on.size,
      'the knob is not on the left when off and the right when on')
check(knob_off.map { |px, py| [32 - px, py] }.sort == knob_on.sort, 'the on knob is not the off knob mirrored in the track')
check(off_cells.all? { |g| (g.keys - %w[K L M]).empty? }, 'the capsule uses a pen other than FG, LINE and MIX')
check(off_cells.sum { |g| g['K'].size } > off_cells.sum { |g| g['L'].size }, 'the capsule has more knob than track')

# Chalk glyphs have no colour of their own; slate and capsule glyphs have no
# outline-less BG: nothing is both.
check(CHALK.all? { |i| SIZES.all? { |w, h| raster.fetch([[w, h], FIRST + i]).first == 'coverage' } }, 'a Chalk control is not coverage')

# Checkbox states differ. Off differs from on and mixed at every size; the
# three differ from each other wherever the box has an inside of 3 px or more
# for the marks, which is a box of 5 px.
SIZES.each do |w, h|
  [[0, 1, 2], [3, 4, 5]].each do |group|
    g = group.map { |i| rows(raster, w, h, i) }
    check(g[0] != g[1] && g[0] != g[2], "#{IDS[group[0]]}: off equals on or mixed at #{w}x#{h}")
    # The box's side, as the window boxes have it: the cell's width, or
    # the stripes' band less a line (n stripes, six at 22 px, at least 1).
    n = [[(6 * h + 11) / 22, (h - 4) / 2].min, 1].max
    side = [w, 2 * n - 1].min
    next if side < 5

    check(g[1] != g[2], "#{IDS[group[1]]}: on equals mixed at #{w}x#{h}")
  end
end

# Seams. A switch's cells join: the rows the track fills are the same either
# side of the seam, and the outline's top and bottom rows run across it.
SIZES.each do |w, h|
  [[6, 7], [8, 9], [10, 11], [12, 13]].each do |l, r|
    a = rows(raster, w, h, l)
    b = rows(raster, w, h, r)
    span = (0...h).select { |y| a[y][w - 1] != '.' }
    check(span == (0...h).select { |y| b[y][0] != '.' } && !span.empty?,
          "#{IDS[l]} and #{IDS[r]} do not join at #{w}x#{h}: #{span} against #{(0...h).select { |y| b[y][0] != '.' }}")
    top = span.first
    check(a[top][w - 1] != '.' && b[top][0] != '.', "#{IDS[l]}: the track's top does not cross the seam at #{w}x#{h}")
  end
  # The capsule's three cells: the track's rows at each seam match.
  [14, 17].each do |first|
    cells = (first..first + 2).map { |i| rows(raster, w, h, i) }
    [[0, 1], [1, 2]].each do |l, r|
      lrows = (0...h).select { |y| cells[l][y][w - 1] != '.' }
      rrows = (0...h).select { |y| cells[r][y][0] != '.' }
      check(lrows == rrows && !lrows.empty?, "#{IDS[first + l]} and #{IDS[first + r]} do not join at #{w}x#{h}")
    end
  end
end

# Scaling. The capsule keeps all three pens at every size, and its knob stays
# inside its track, whose rows are the 2/22 to 20/22 of the cell's height.
SIZES.each do |w, h|
  %w[off on].each_with_index do |state, k|
    cells = (0..2).map { |p| rows(raster, w, h, 14 + 3 * k + p) }
    all = cells.flat_map { |r| r.join.chars }.uniq
    check(%w[K L M].all? { |role| all.include?(role) }, "the capsule #{state} at #{w}x#{h} lacks a pen: #{all.sort}")
    drawn = (0...h).select { |y| cells.any? { |c| c[y] != '.' * w } }
    knob = (0...h).select { |y| cells.any? { |c| c[y].include?('L') } }
    check(knob.first >= drawn.first && knob.last <= drawn.last, "the capsule #{state} knob is outside its track at #{w}x#{h}")
    check(drawn.first.between?(h * 2 / 22 - 1, h * 2 / 22 + 1) && drawn.last.between?(h * 20 / 22 - 2, h * 20 / 22),
          "the capsule's track is not on 2/22 to 20/22 of #{h} px: rows #{drawn.first}-#{drawn.last}")
  end
end

puts "CONTROL_GLYPH_PASS glyphs=20 catalogue=named,next_free=U+10030A sizes=#{SIZES.map { |s| s.join('x') }.join(',')} " \
     'design=pixel-exact chalk=coverage slate,capsule=pen_planes mockup_11x22=held states=distinct seams=joined capsule=3_pens_every_size'
