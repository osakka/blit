#!/usr/bin/env ruby
# frozen_string_literal: true

# Icon clip sweep: no fitted glyph loses ink at any font size.
#
# Every glyph the raster draws by fitting an outline into the cell (draw_fitted,
# ADR 0195) must land inside the cell. The glyph classes swept:
#   - every catalogue icon (single column, and each part of a spanning icon),
#     and the catalogue's text glyphs;
#   - the scalars Hibr's UI draws (assets/fonts/hibr-ui-glyphs.json);
#   - Nerd Symbols, the Noto symbol faces and Unifont, at a stride (the one
#     fitting rule is the same for all; BLIT_ICON_CLIP_STRIDE=1 sweeps all);
#   - Hack's overhanging arrows U+27F5-27FF.
# at every font size from 8 to 64 px (the server maximum, BLIT_FONT_SIZE_MAX_PX) and the cell Blit derives for it (the
# width rounds half up, as font_cell_dimension does, and also rounded down),
# and the owner's 11x22.
#
# A spanning icon's part may overhang toward its neighbour, which is the
# design (each part is the whole icon shifted by its column); nothing may be
# trimmed at the span's outer edges, the cell's top or bottom, and no glyph
# may fall back to the PSF's replacement because it could not be fitted.
#
# The harness appends icon_clip_gate.c to a copy of the raster source with
# one probe call inserted into draw_fitted (anchor: the loop that copies the
# placed bitmap into the glyph, so the probe sees the final placement), so it sees the real bitmap against the real cell. Set
# BLIT_RASTER_SOURCE to sweep another copy of blit_raster.c (a proposed
# patch); BLIT_ICON_CLIP_FONTS=all adds Fira, Plex, 3270 and Spleen (its masters, ADR 0227); BLIT_ICON_CLIP_REPORT=1 prints the per-class table even on a pass.

require 'json'
require 'open3'
require 'tmpdir'

ROOT = File.expand_path('../..', __dir__)
ANCHOR = "\n    for (y = 0; y < (int)bitmap->rows; ++y) for (x = 0; x < (int)bitmap->width; ++x) {"
# name => [PSF, outline font ('-' is a pixel font: its raster borrows the stack)]
FONTS = { 'hack' => ['Blit-Terminus16.psf', 'text/Hack-Regular.ttf'],
          'jetbrains' => ['Blit-Terminus16.psf', 'text/JetBrainsMonoNL-Regular.ttf'],
          'fira' => ['Blit-Terminus16.psf', 'text/FiraCode-Regular.ttf'],
          'plex' => ['Blit-Terminus16.psf', 'text/IBMPlexMono-Regular.ttf'],
          '3270' => ['Blit-Terminus16.psf', 'text/3270-Regular.ttf'],
          'terminus' => ['Blit-Terminus16.psf', '-'],
          'spleen' => ['Blit-Terminus16.psf', 'text/spleen'] }.freeze

def fail!(why)
  abort "ICON_CLIP_FAIL #{why}"
end

Dir.chdir(ROOT)
source_path = ENV.fetch('BLIT_RASTER_SOURCE', 'src/blit_raster.c')
source = File.read(source_path)
fail!("anchor line not found exactly once in #{source_path}") unless source.scan(ANCHOR).length == 1
fail!('the harness needs src/blit_raster.h next to the source') unless File.exist?('src/blit_raster.h')

catalogue = JSON.parse(File.read('assets/fonts/icons/catalogue.json'))
spec = []
catalogue.fetch('icons').each do |i|
  columns = i.fetch('columns', 1)
  spec << ["icon-#{columns > 1 ? 'span' : 'single'}", i['codepoint'].to_i(16), columns, i.fetch('column', 0), i['id']]
end
catalogue.fetch('text_glyphs', []).each { |i| spec << ['text-glyph', i['codepoint'].to_i(16), 1, 0, i['id']] }
JSON.parse(File.read('assets/fonts/hibr-ui-glyphs.json')).fetch('scalars').each do |s|
  spec << ['hibr-ui', s.to_i(16), 1, 0, 'ui']
end

# Full-cell glyphs, exact geometry: the edges each must reach (1 left, 2 right,
# 4 top, 8 bottom; 16 the whole cell).
{ 0x2588 => 31, 0x2500 => 3, 0x2501 => 3, 0x2550 => 3, 0x2502 => 12, 0x2503 => 12, 0x2551 => 12,
  0x250C => 0, 0xE0B0 => 15, 0xE0B2 => 15, 0xE0B1 => 15, 0xE0B3 => 15,
  0x2580 => 7, 0x2584 => 11, 0x258C => 13, 0x2590 => 14 }.each { |cp, mask| spec << ['cell', cp, 1, mask, format('U%X', cp)] }
(0x10023A..0x10023C).to_a.concat((0x10025D..0x10025F).to_a).each { |cp| spec << ['cell', cp, 1, 15, format('chrome-%X', cp)] }
# ADR 0219: the pressed boxes reach all four edges as the boxes do; a grow cell its right,
# top and bottom (the frame's lines), and its left only where the box is the cell's width.
[0x1002BB, 0x1002BC, 0x1002BD, 0x1002C0, 0x1002C1, 0x1002C2].each { |cp| spec << ['cell', cp, 1, 15, format('chrome-%X', cp)] }
[0x1002BE, 0x1002BF, 0x1002C3, 0x1002C4].each { |cp| spec << ['cell', cp, 1, 14, format('chrome-%X', cp)] }
# ADR 0222: the minimise boxes, normal and pressed, reach all four edges as the boxes do.
(0x1002CD..0x1002D0).each { |cp| spec << ['cell', cp, 1, 15, format('chrome-%X', cp)] }
# ADR 0224: the full-screen and sticky boxes, as the minimise boxes.
(0x10030A..0x100313).each { |cp| spec << ['cell', cp, 1, 15, format('chrome-%X', cp)] }
cells = ([[11, 22]] + (8..64).flat_map { |s| [[(s + 1) / 2, s], [s / 2, s]] }).uniq
stride = ENV.fetch('BLIT_ICON_CLIP_STRIDE', '48').to_i
freetype = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{ROOT}/build/deps/prefix/include/freetype2")
fonts = ENV['BLIT_ICON_CLIP_FONTS'] == 'all' ? FONTS : FONTS.slice('hack', 'jetbrains', 'terminus')
bad = []
swept = 0
Dir.mktmpdir('blit-icon-clip') do |work|
  text = source.sub(ANCHOR) do
    "\n    { extern void icon_clip_probe(FT_Face, FT_Bitmap *, int, int, int, int, int);" \
      " icon_clip_probe(face, bitmap, left, top, w, h, colour); }#{ANCHOR}"
  end
  text = text.gsub('"../vendor/', "\"#{ROOT}/vendor/")
  File.write(File.join(work, 'probe.c'),
             "#{text}\n#include \"#{ROOT}/tools/tests/icon_clip_gate.c\"\n")
  File.write(File.join(work, 'spec'), spec.map { |k, c, n, col, l| "#{k} #{c.to_s(16)} #{n} #{col} #{l}\n" }.join)
  binary = File.join(work, 'gate')
  fail!('the harness does not compile') unless system('tcc', '-O2', '-std=c99', "-I#{freetype}", "-I#{ROOT}/src",
    File.join(work, 'probe.c'), "#{ROOT}/src/blit_font.c",
    ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
    ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-lm', '-o', binary)
  fonts.each do |name, (psf, font)|
    font = "assets/fonts/#{font}" unless font == '-'
    out, err, status = Open3.capture3(binary, "assets/fonts/#{psf}", font,
                                      'assets/fonts/NotoColorEmoji.ttf', File.join(work, 'spec'), stride.to_s,
                                      *cells.flatten.map(&:to_s))
    fail!("the harness failed (#{name}): #{err.strip}") unless status.success?
    out.each_line do |l|
      if l.start_with?('BAD open')
        fail!(l.strip)
      elsif l.start_with?('BAD')
        f = l.split
        m = f[4].match(/(\d+)x(\d+)/)
        bad << { font: name, kind: f[1], cp: f[2], label: f[3], w: m[1].to_i, h: m[2].to_i,
                 lost: f[5][/\d+/].to_i, sides: f[6].sub('sides=', ''), fallback: f[7].end_with?('1'), line: l.strip }
      elsif l.start_with?('SWEPT')
        swept += l.split[1].to_i
      end
    end
  end
end
File.write(ENV['BLIT_ICON_CLIP_RAW'], bad.map { |b| b[:line] }.join("\n")) if ENV['BLIT_ICON_CLIP_RAW']

if ENV['BLIT_ICON_CLIP_REPORT'] || !bad.empty?
  bad.group_by { |b| [b[:font], b[:kind]] }.sort.each do |(font, kind), list|
    first = list.min_by { |b| [b[:h], b[:w]] }
    sides = list.map { |b| b[:sides] }.join.chars.uniq.sort.join
    sizes = list.map { |b| b[:h] }.uniq.sort
    puts format('%-9s %-14s first=%dx%d glyph/size pairs=%-6d glyphs=%-5d sides=%-4s sizes=%s', font, kind,
                first[:w], first[:h], list.length, list.map { |b| b[:cp] }.uniq.length, sides,
                sizes.length > 12 ? "#{sizes.first(12).join(',')},... (#{sizes.length})" : sizes.join(','))
  end
end
unless bad.empty?
  bad.first(12).each { |b| puts "  #{b[:line]}" }
  fail!("#{bad.length} of #{swept} fitted glyphs lose ink or fall back")
end
puts "ICON_CLIP_PASS glyph_renders=#{swept} cells=#{cells.length} fonts=#{fonts.length} stride=#{stride} ink=whole"
