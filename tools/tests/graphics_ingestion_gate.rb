#!/usr/bin/env ruby
require 'fileutils'
root=File.expand_path('../..',__dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
out="build/tests/graphics-ingestion-#{Process.pid}"
at_exit { File.delete(out) if File.file?(out) }
abort 'usage: graphics_ingestion_gate.rb [--observe]' unless ARGV.empty? || ARGV==['--observe']
abort 'build failed' unless system('tcc','-O2','-std=c99',
  '-Ibuild/deps/prefix/include','-Ibuild/deps/prefix/include/libdrm',
  '-Ibuild/deps/prefix/include/freetype2','tools/tests/graphics_ingestion_gate.c',
  'src/blit_kms.c','src/blit_vt.c','src/blit_font.c','src/blit_raster.c',
  '/lib/x86_64-linux-gnu/libfreetype.so.6','/lib/x86_64-linux-gnu/libharfbuzz.so.0',
  '-Lbuild/deps/prefix/lib/x86_64-linux-gnu','-ldrm','-lutil','-lz','-o',out)
env={'LD_LIBRARY_PATH'=>"#{root}/build/deps/prefix/lib/x86_64-linux-gnu"}
runner=ENV['BLIT_GRAPHICS_VALGRIND']=='1' ? %w[valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite,indirect] : []
abort 'graphics ingestion failed' unless system(env,*runner,out,*ARGV)
