#!/usr/bin/env ruby
# ADR 0241: Arabic words through the raster's word cache, every shipped
# text font, heights 8-64 (tools/tests/arabic_word_slice_gate.c).
require 'json'
require 'tmpdir'
require_relative 'build_flags'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
samples = JSON.parse(File.read('assets/fonts/arabic-specimens.json')).fetch('samples')
def cells(visual)
  visual.codepoints.each_with_object([]) do |cp, list|
    if (((0x064b..0x065f).cover?(cp) || cp == 0x0670) && !list.empty?) then list.last << cp else list << [cp] end
  end
end
lines = samples.map { |s| cells(s.fetch('visual')).map { |c| c.map { |x| x.to_s(16) }.join(',') }.join(';') }
lines << 'fe92;fe92;fe92;20;fe8f;fe8f;20;fe8f,301'          # cut word, letters apart, a mark the face lacks
lines << (['fe92'] * 40).join(';')                            # a run longer than 32 cells
manifest = JSON.parse(File.read('assets/fonts/arabic-fonts.source.json'))
naskh = "assets/fonts/#{manifest.fetch('naskh').fetch('file')}"
# Every shipped text font: the outline fonts, Spleen's masters, and the
# pixel font's own raster.
fonts = Dir['assets/fonts/text/*-Regular.ttf'].sort + ['assets/fonts/text/spleen', 'pixel']
fonts = [ARGV[0]] if ARGV[0]
Dir.mktmpdir('blit-word-slice') do |work|
  bin = File.join(work, 'gate')
  abort 'compile failed' unless system('tcc', '-std=c99', '-Wall', *BuildFlags.cflags('font'),
    'tools/tests/arabic_word_slice_gate.c', 'src/blit_raster.c', 'src/blit_font.c', *BuildFlags.libs('font'), '-o', bin)
  out = IO.popen({ 'BLIT_FONT_ROOT' => File.join(root, 'assets/fonts') },
                 [bin, 'assets/fonts/Blit-Terminus16.psf', 'assets/fonts/NotoColorEmoji.ttf', naskh, *fonts],
                 'r+', err: [:child, :out]) { |io| io.write(lines.join("\n") + "\n"); io.close_write; io.read }
  puts out.lines.reject { |l| l.start_with?('blit:') }
  exit $?.success? ? 0 : 1
end
