#!/usr/bin/env ruby
require 'json'
require 'tmpdir'
require 'open3'
root=File.expand_path('../..',__dir__)
Dir.chdir(root) do
  manifest=JSON.parse(File.read('assets/unicode/emoji-18.0/source.json'))
  bases=File.readlines("assets/unicode/emoji-18.0/#{manifest.dig('variations','file')}").filter_map { |l| (m=l.match(/^([0-9A-F]+) FE0E\s*;/))&&m[1].to_i(16) }
  defaults=File.readlines("assets/unicode/emoji-18.0/#{manifest.dig('properties','file')}").flat_map do |l|
    m=l.match(/^([0-9A-F]+)(?:\.\.([0-9A-F]+))?\s*;\s*Emoji_Presentation\s*#/)
    m ? (m[1].to_i(16)..(m[2]||m[1]).to_i(16)).to_a : []
  end
  vectors=(bases.map{|cp|"0 #{cp.to_s(16)}"}+defaults.map{|cp|"1 #{cp.to_s(16)}"}).join("\n")
  Dir.mktmpdir('blit-presentation') do |work|
    data=File.join(work,'data.h')
    raise 'data generation failed' unless system('ruby','tools/fonts/build-emoji-presentation.rb',data)
    raise 'stale presentation data' unless File.binread(data)==File.binread('src/blit_emoji_presentation.h')
    binary=File.join(work,'gate')
    raise 'compile failed' unless system('tcc','-O2','-std=c99',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE',"#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/emoji_presentation_gate.c','src/blit_font.c','src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY','/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY','/lib/x86_64-linux-gnu/libharfbuzz.so.0'),'-o',binary)
    %w[BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf].each do |profile|
      sizes=ENV['BLIT_PRESENTATION_SIZE'] ? [ENV['BLIT_PRESENTATION_SIZE']]*2 : %w[8 64]
      command=[binary,'assets/fonts/Blit-Hibr-Terminus16.psf',"assets/fonts/#{profile}",'assets/fonts/NotoColorEmoji.ttf',*sizes]
      command=['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      out,err,status=Open3.capture3(*command,stdin_data:vectors)
      warn err unless err.empty?
      raise "presentation failed: #{profile}" unless status.success?
      puts "#{profile}: #{out}"
    end
  end
end
