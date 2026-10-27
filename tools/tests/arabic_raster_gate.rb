#!/usr/bin/env ruby
# Explicit inputs until companion candidates meet acceptance; no system font fallback.
require 'tmpdir'
require 'json'
require 'digest'
root = File.expand_path('../..', __dir__)
abort 'usage: arabic_raster_gate.rb [RETRO_ARABIC MODERN_ARABIC]' unless [0,2].include?(ARGV.size)
Dir.chdir(root) do
  manifest = JSON.parse(File.read('assets/fonts/arabic-fonts.source.json'))
  %w[retro modern].each do |profile|
    entry = manifest.fetch(profile)
    raise "wrong #{profile} font" unless Digest::SHA256.file("assets/fonts/#{entry.fetch('file')}").hexdigest == entry.fetch('sha256')
    raise 'license identity mismatch' unless Digest::SHA256.file("assets/fonts/#{entry.fetch('license_file')}").hexdigest == entry.fetch('license_sha256')
    if entry['source_file']
      raise 'preferred source identity mismatch' unless Digest::SHA256.file("assets/fonts/#{entry.fetch('source_file')}").hexdigest == entry.fetch('source_sha256')
    end
  end
  corpus = "assets/fonts/#{manifest.fetch('specimens_file')}"
  raise 'Arabic specimen identity mismatch' unless Digest::SHA256.file(corpus).hexdigest == manifest.fetch('specimens_sha256')
  samples = JSON.parse(File.read(corpus)).fetch('samples')
  cells = samples.flat_map do |s|
    s.fetch('visual').codepoints.each_with_object([]) do |cp, list|
      if ((0x064b..0x065f).cover?(cp) || cp == 0x0670) && !list.empty?
        list.last << cp
      else
        list << [cp]
      end
    end
  end
  inputs = ARGV.empty? ? %w[retro modern].map { |p| "assets/fonts/#{manifest.fetch(p).fetch('file')}" } : ARGV
  Dir.mktmpdir('blit-arabic-gate') do |work|
    binary = File.join(work, 'gate')
    input = File.join(work, 'cells.txt')
    File.write(input, cells.map { |cp| [cp.size,*cp.map { |c| c.to_s(16) }].join(' ') }.join("\n"))
    raise 'compile failed' unless system('tcc', '-std=c99', '-Wall',
      "-I#{root}/build/deps/prefix/include/freetype2",
      'tools/tests/arabic_raster_gate.c', 'src/blit_raster.c', 'src/blit_font.c',
      '/lib/x86_64-linux-gnu/libfreetype.so.6', '/lib/x86_64-linux-gnu/libharfbuzz.so.0', '-o', binary)
    passed = inputs.zip(%w[Retro Modern]).map do |arabic, profile|
      command = [binary, 'assets/fonts/Blit-Terminus16.psf',
        "assets/fonts/Blit#{profile}Nerd-Regular.ttf", arabic, input]
      command = ['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      system(*command)
    end
    raise 'Arabic candidate acceptance failed; do not enable in profiles' unless passed.all?
  end
end
