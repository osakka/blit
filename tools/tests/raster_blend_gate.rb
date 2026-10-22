#!/usr/bin/env ruby
require 'fileutils'
require 'open3'
root=File.expand_path('../..',__dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
out="build/tests/raster-blend-#{Process.pid}"
at_exit { File.delete(out) if File.file?(out) }
abort 'usage: raster_blend_gate.rb [--benchmark|--observe]' unless ARGV.empty? || [%w[--benchmark],%w[--observe]].include?(ARGV)
abort 'build failed' unless system('tcc','-O2','-std=c99',
  '-Ibuild/deps/prefix/include/freetype2','tools/tests/raster_blend_gate.c',
  'src/blit_raster.c','src/blit_font.c','/lib/x86_64-linux-gnu/libfreetype.so.6',
  '/lib/x86_64-linux-gnu/libharfbuzz.so.0','-o',out)
abort 'blend equivalence failed' unless system(out,*(ARGV==['--benchmark'] ? ARGV : []))
asm,err,status=Open3.capture3('objdump','-d','--disassemble=blit_raster_blend',out)
abort err unless status.success? && asm.include?('<blit_raster_blend>:')
divisions=asm.lines.count { |line| line.match?(/\s(?:i?div)[bwlq]?\s/) }
puts "RASTER_BLEND_WORK integer_divisions=#{divisions} compiler=tcc scope=production_function"
abort 'production blender still contains integer division' if divisions>0 && ARGV!=['--observe']
