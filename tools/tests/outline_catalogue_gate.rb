#!/usr/bin/env ruby
require 'json'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  manifest = JSON.parse(File.read('assets/fonts/modern-fonts.source.json'))
  catalogue = JSON.parse(File.read(File.join('assets/fonts', manifest.fetch('catalogue').fetch('file'))))
  scalars = catalogue.values.grep(Hash).filter_map { |v| v['code'] }.map { |v| v.to_i(16) }.uniq.sort
  raise 'catalogue size changed' unless scalars.size == 10_386
  headers = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")
  library = ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6')
  Dir.mktmpdir('blit-outline-catalogue') do |work|
    input = File.join(work, 'catalogue.txt')
    File.write(input, scalars.map { |v| v.to_s(16) }.join("\n"))
    binary = File.join(work, 'gate')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall', "-I#{headers}",
      'tools/tests/outline_catalogue_gate.c', 'src/blit_font.c', 'src/blit_raster.c', library,
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-o', binary)
    %w[BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf].each do |font|
      raise "raster catalogue failed: #{font}" unless system(binary,
        'assets/fonts/Blit-Hibr-Terminus16.psf', "assets/fonts/#{font}", 'assets/fonts/NotoColorEmoji.ttf', input)
    end
  end
end
