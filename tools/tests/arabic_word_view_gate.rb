#!/usr/bin/env ruby
# ADR 0241: Arabic words through draw_view (tools/tests/arabic_word_view_gate.c).
require 'json'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
samples = JSON.parse(File.read('assets/fonts/arabic-specimens.json')).fetch('samples')
def cells(visual)
  visual.codepoints.each_with_object([]) do |cp, list|
    if (((0x064b..0x065f).cover?(cp) || cp == 0x0670) && !list.empty?) then list.last << cp else list << [cp] end
  end
end
lines = samples.map { |s| cells(s.fetch('visual')).map { |c| c.map { |x| x.to_s(16) }.join(',') }.join(';') }
lines << 'fe92;fe92;fe92;20;fe8f;fe8f;20;41;42;fe8f,301'
naskh = "assets/fonts/#{JSON.parse(File.read('assets/fonts/fonts.source.json')).fetch('arabic').fetch('naskh').fetch('file')}"
font = ARGV[0] || 'assets/fonts/text/Hack-Regular.ttf'
Dir.mktmpdir('blit-word-view') do |work|
  bin = File.join(work, 'gate')
  cflags = `tools/build-flags.sh --cflags server`.split
  libs = `tools/build-flags.sh --libs server`.split
  abort 'compile failed' unless system('tcc', '-O2', '-std=c99', *cflags, 'tools/tests/arabic_word_view_gate.c',
                                       'src/blit_font.c', 'src/blit_raster.c', *libs, '-o', bin)
  out = IO.popen({ 'BLIT_FONT_ROOT' => File.join(root, 'assets/fonts') },
                 [bin, 'assets/fonts/Blit-Terminus16.psf', font, 'assets/fonts/NotoColorEmoji.ttf', naskh],
                 'r+', err: [:child, :out]) { |io| io.write(lines.join("\n") + "\n"); io.close_write; io.read }
  puts out.lines.reject { |l| l.start_with?('blit:') }
  exit $?.success? ? 0 : 1
end
