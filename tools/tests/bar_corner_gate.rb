#!/usr/bin/env ruby
require 'tmpdir'
require_relative 'build_flags'
root=File.expand_path('../..',__dir__)
Dir.chdir(root) do
  Dir.mktmpdir('blit-bar-corner') do |work|
    binary=File.join(work,'gate')
    raise 'compile failed' unless system('tcc','-O2','-std=c99','-Wall',
      *BuildFlags.cflags('font'),
      'tools/tests/bar_corner_gate.c','src/blit_font.c','src/blit_raster.c',
      *BuildFlags.libs('font'),'-o',binary)
    %w[text/Hack-Regular.ttf text/3270-Regular.ttf].each do |face|
      command=[binary,'assets/fonts/Blit-Terminus16.psf',"assets/fonts/#{face}",'assets/fonts/NotoColorEmoji.ttf']
      command=['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      raise "bar corners failed: #{face}" unless system(*command)
    end
  end
end
