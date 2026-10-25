#!/usr/bin/env ruby
require 'tmpdir'
root=File.expand_path('../..',__dir__)
Dir.chdir(root) do
  Dir.mktmpdir('blit-rectangle-copy') do |work|
    binary=File.join(work,'gate')
    raise 'compile failed' unless system('tcc','-O2','-std=c99',
      '-Ibuild/deps/prefix/include',
      '-Ibuild/deps/prefix/include/libdrm','-Ibuild/deps/prefix/include/freetype2',
      'tools/tests/rectangle_copy_gate.c','src/blit_kms.c','src/blit_vt.c','src/blit_font.c','src/blit_raster.c',
      '/lib/x86_64-linux-gnu/libfreetype.so.6','/lib/x86_64-linux-gnu/libharfbuzz.so.0',
      '-Lbuild/deps/prefix/lib/x86_64-linux-gnu','-ldrm','-lutil','-lz','-o',binary)
    command=[binary]
    command=['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_RECTANGLE_VALGRIND']=='1'
    raise 'rectangle copy failed' unless system({'LD_LIBRARY_PATH'=>"#{root}/build/deps/prefix/lib/x86_64-linux-gnu"},*command)
  end
end
