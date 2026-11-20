#!/usr/bin/env ruby
# ADR 0241: Hibr's visual cells back to letters. For every pinned specimen,
# the runs' letters equal the specimen's logical text word for word, with no
# joiner in a whole word; a word cut at either end keeps its forms with a
# ZWJ, and two letters Hibr left apart get a ZWNJ. The key compares whole.
require 'json'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
abort 'forms table differs from its generator' unless
  system('python3.11', 'tools/fonts/build-arabic-forms.py', '/dev/stdout', out: File::NULL) &&
  `python3.11 tools/fonts/build-arabic-forms.py /dev/stdout 2>/dev/null`.sub(/\A.*?\/\*/m, '/*').sub(/\d+ forms, Unicode .*\n\z/, '') ==
  File.read('src/blit_arabic_forms.h') if system('python3.11', '-c', 'import unicodedata,sys; sys.exit(unicodedata.unidata_version!="14.0.0")')
samples = JSON.parse(File.read('assets/fonts/arabic-specimens.json')).fetch('samples')
def cells(visual)
  visual.codepoints.each_with_object([]) do |cp, list|
    if (((0x064b..0x065f).cover?(cp) || cp == 0x0670) && !list.empty?) then list.last << cp else list << [cp] end
  end
end
Dir.mktmpdir('blit-word-text') do |work|
  bin = File.join(work, 'gate')
  abort 'compile failed' unless system('tcc', '-std=c99', '-Wall', 'tools/tests/arabic_word_text_gate.c', '-o', bin)
  abort 'key equality' unless system(bin, 'keys', out: File::NULL)
  lines = samples.map { |s| cells(s.fetch('visual')).map { |c| c.map { |x| x.to_s(16) }.join(',') }.join(';') }
  # Edge cases: a word cut at both ends (medial forms only), two letters
  # Hibr left apart (isolated beh, isolated beh).
  lines << 'fe92;fe92;fe92'
  lines << 'fe8f;fe8f'
  out = IO.popen([bin], 'r+') { |io| io.write(lines.join("\n") + "\n"); io.close_write; io.read }
  groups = out.split("--\n").map { |g| g.lines.map { |l| k, *t = l.split; [k.to_i, t.map { |h| h.to_i(16) }] } }
  words = 0
  samples.each_with_index do |s, i|
    # Runs come left to right, visual order: the words read right to left.
    got = groups[i].map { |kind, text| text.pack('U*') }.reverse
    want = s.fetch('logical').split(' ')
    # Digits and Latin are not runs of letters; compare the Arabic words.
    want = want.select { |w| w.codepoints.any? { |c| (0x0600..0x06ff).cover?(c) } }
    abort "specimen #{i}: #{got.inspect} != #{want.inspect}" unless got == want
    abort "specimen #{i}: joiner in a whole word" if groups[i].any? { |_, t| t.include?(0x200d) || t.include?(0x200c) }
    words += got.size
  end
  cut = groups[samples.size].first[1]
  abort "cut word lost its joins: #{cut.map { |c| c.to_s(16) }}" unless cut.first == 0x200d && cut.last == 0x200d
  apart = groups[samples.size + 1].first[1]
  abort "letters left apart would join: #{apart.map { |c| c.to_s(16) }}" unless apart == [0x628, 0x200c, 0x628]
  puts "ARABIC_WORD_TEXT_PASS specimens=#{samples.size} words=#{words} cut=zwj apart=zwnj keys=whole"
end
