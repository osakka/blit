#!/usr/bin/env ruby
require 'fileutils'
require_relative 'build_flags'
root=File.expand_path('../..',__dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
out="build/tests/graphics-ingestion-#{Process.pid}"
at_exit { File.delete(out) if File.file?(out) }
abort 'usage: graphics_ingestion_gate.rb [--observe]' unless ARGV.empty? || ARGV==['--observe']
abort 'build failed' unless system('tcc','-O2','-std=c99',
  *BuildFlags.cflags('server'),'tools/tests/graphics_ingestion_gate.c',
  *BuildFlags.server_sources,
  *BuildFlags.libs('server'),'-o',out)
runner=ENV['BLIT_GRAPHICS_VALGRIND']=='1' ? %w[valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite,indirect] : []
abort 'graphics ingestion failed' unless system(*runner,out,*ARGV)
