#!/usr/bin/env ruby
require 'json'
require 'digest'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  source = JSON.parse(File.read('assets/fonts/fonts.source.json'))
  manifest = source.fetch('custom_icons')
  ui = source.fetch('ui_coverage')
  ui_path = "assets/fonts/#{ui.fetch('file')}"
  raise 'UI corpus identity mismatch' unless Digest::SHA256.file(ui_path).hexdigest == ui.fetch('sha256')
  required = JSON.parse(File.read(ui_path)).fetch('scalars')
  raise 'UI corpus count mismatch' unless required.size == ui.fetch('scalars') && required.uniq == required
  manifest.fetch('sources').each do |path, sha|
    raise "custom asset identity mismatch: #{path}" unless Digest::SHA256.file("assets/fonts/#{path}").hexdigest == sha
  end
  icons = JSON.parse(File.read("assets/fonts/#{manifest.fetch('catalogue')}"))
  # Folders have colourways; every other file kind keeps only its classic
  # slot, the rest being retired (ADR 0201, 0.3.9).
  folder = ->(k) { k == 'folder' || k == 'share' || k.start_with?('folder-') }
  slots = ->(kinds, kind_major) do
    pairs = kind_major ? kinds.product(%w[classic blue green amber]) : %w[classic blue green amber].product(kinds).map(&:reverse)
    pairs.select { |k, v| v == 'classic' || folder.call(k) }.flat_map { |k, v| ["file-#{k}-#{v}-left", "file-#{k}-#{v}-right"] }
  end
  expected = %w[blit hibr-pen wifi-1 wifi-2 wifi-3 wifi-4 bluetooth] +
    (0..4).map { |l| "battery-#{l}" } + (0..4).map { |l| "battery-#{l}-charging" } +
    %w[battery-ac] + (0..3).map { |l| "volume-#{l}" } + %w[hui-left hui-right power phone-off phone-wait phone-on phone-ring] +
    %w[phone-classic-off phone-classic-wait phone-classic-on phone-classic-ring] +
    slots.call(%w[folder document text pdf image audio video archive code application disk trash], false) +
    slots.call(%w[home boxer markdown spreadsheet diagram presentation font web terminal settings bundle link], true) +
    slots.call(%w[folder-git folder-config folder-local folder-system folder-desktop folder-downloads folder-documents folder-music folder-pictures folder-videos folder-apps folder-games rc history service key network cron readme license log kernel boot c asm makefile patch json vector rom ebook exe disc package library lock database usb share], true) +
    %w[jpg png gif].flat_map { |f| ["file-image-#{f}-left", "file-image-#{f}-right"] } +
    %w[hui-mark-left hui-mark-right wiretap-left wiretap-right wiretap-off-left wiretap-off-right wiretap-1 wiretap-1-off] +
    (1..4).map { |l| "wifi-signal-#{l}" } + %w[wifi-off] + (1..4).map { |l| "cellular-#{l}" } +
    ((1..4).map { |l| "wifi-signal-wide-#{l}" } + %w[wifi-off-wide] + (0..3).map { |l| "volume-wide-#{l}" } +
     %w[off wait on ring].map { |s| "phone-classic-wide-#{s}" } +
     (0..4).map { |l| "battery-wide-#{l}" } + (0..4).map { |l| "battery-wide-#{l}-charging" } + %w[battery-wide-ac])
      .flat_map { |i| ["#{i}-left", "#{i}-right"] }
  raise 'incomplete original catalogue' unless icons.fetch('icons').map { |i| i.fetch('id') } == expected
  # ADR 0214: the outer-edge frame is cell geometry at U+1002AE-1002B6, clear
  # of the icons and every retired range; a deprecated glyph names live,
  # non-deprecated replacements and keeps its place (it is not retired).
  geometry = icons.fetch('cell_geometry')
  frame = geometry.select { |g| g['codepoint'].to_i(16).between?(0x1002AE, 0x1002BA) }
  raise 'the outer-edge frame (U+1002AE-1002BA) is not cell geometry in the catalogue' unless
    frame.size == 13 && frame.map { |g| g["codepoint"].to_i(16) } == (0x1002AE..0x1002BA).to_a
  raise 'a cell-geometry glyph reuses an icon or retired codepoint' unless
    (geometry.map { |g| g['codepoint'].to_i(16) } &
     (icons.fetch('icons').map { |i| i['codepoint'].to_i(16) } +
      icons.fetch('retired').flat_map { |r| (r['from'].to_i(16)..r['to'].to_i(16)).to_a })).empty?
  ids = geometry.map { |g| g['id'] }
  deprecated = geometry.select { |g| g['deprecated'] }
  raise 'ADR 0214 deprecates 18 glyphs' unless deprecated.size == 18
  raise 'a deprecated glyph is not replaced by a live one' unless
    deprecated.all? { |g| g['replaced_by'].all? { |r| ids.include?(r) && !deprecated.map { |d| d['id'] }.include?(r) } }
  raise 'a deprecated glyph was retired early' unless deprecated.none? { |g| icons.fetch('retired').any? { |r| g['codepoint'].to_i(16).between?(r['from'].to_i(16), r['to'].to_i(16)) } }
  codepoints = icons.fetch('icons').map { |i| i.fetch('codepoint').to_i(16) }
  retired = icons.fetch('retired').flat_map { |r| (r.fetch('from').to_i(16)..r.fetch('to').to_i(16)).to_a }
  raise 'unexpected private mappings' unless codepoints == codepoints.sort.uniq && codepoints.all? { |c| c.between?(0x100000, 0x10fffd) }
  raise 'a retired codepoint is mapped again' unless (codepoints & retired).empty?
  Dir.mktmpdir('blit-custom-icons') do |work|
    binary = File.join(work, 'gate')
    input = File.join(work, 'ui.txt')
    File.write(input, required.join("\n")+"\n")
    # One codepoint per distinct file drawing (ADR 0201): a kind's colour
    # slots may share one drawing, but no two drawings may look alike.
    live = File.join(work, 'live.txt')
    File.write(live, icons.fetch('icons').map { |i| i['codepoint'] }.join("\n") + "\n")
    gone = File.join(work, 'retired.txt')
    File.write(gone, retired.map { |c| c.to_s(16) }.join("\n") + "\n")
    distinct = File.join(work, 'distinct.txt')
    File.write(distinct, icons.fetch('icons').select { |i| i['id'].start_with?('file-') && i['column'] == 0 }
                              .uniq { |i| i['source'] }.map { |i| i['codepoint'] }.join("\n") + "\n")
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/custom_icon_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-o', binary)
    # ADR 0195: every shipped text font, through the glyph stack.
    # ADR 0227: a directory in text/ is one font, of masters; it is opened
    # as one, and its rule picks the master for each size.
    source.fetch('text').map { |t| File.dirname(t.fetch('file')) == 'text' ? t.fetch('file') : File.dirname(t.fetch('file')) }.uniq.each do |face|
      command = [binary, 'assets/fonts/Blit-Terminus16.psf', "assets/fonts/#{face}", 'assets/fonts/NotoColorEmoji.ttf', input, distinct, live, gone]
      command = ['valgrind', '--error-exitcode=99', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect', *command] if ENV['BLIT_FONT_VALGRIND'] == '1'
      raise "custom glyph raster failed: #{face}" unless system(*command)
    end
  end
end
