#!/usr/bin/env ruby
require 'tmpdir'
root=File.expand_path('../..',__dir__)
Dir.chdir(root) do
  Dir.mktmpdir('blit-powerline') do |work|
    binary=File.join(work,'gate')
    raise 'compile failed' unless system('tcc','-O2','-std=c99','-Wall',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE',"#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/powerline_geometry_gate.c','src/blit_font.c','src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY','/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY','/lib/x86_64-linux-gnu/libharfbuzz.so.0'),'-o',binary)
    %w[text/Hack-Regular.ttf text/3270-Regular.ttf].each do |face|
      command=[binary,'assets/fonts/Blit-Terminus16.psf',"assets/fonts/#{face}",'assets/fonts/NotoColorEmoji.ttf']
      command=['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      raise "geometry failed: #{face}" unless system(*command)
    end
  end
end
