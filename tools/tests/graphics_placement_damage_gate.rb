#!/usr/bin/env ruby
require 'fileutils'
require 'tmpdir'
require_relative 'build_flags'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
out = "build/tests/graphics-placement-damage-#{Process.pid}"
native = "build/tests/graphics-scene-native-#{Process.pid}"
at_exit { [out, native].each { |path| File.delete(path) if File.file?(path) } }
runner = ENV['BLIT_GRAPHICS_VALGRIND'] == '1' ? %w[valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite,indirect] : []
abort 'build failed' unless system('tcc', '-O2', '-std=c99',
  *BuildFlags.cflags('server'), 'tools/tests/graphics_placement_damage_gate.c',
  'src/blit_kms.c', 'src/blit_vt.c', 'src/blit_font.c', 'src/blit_raster.c',
  *BuildFlags.libs('server'), '-o', out)
abort 'graphics placement damage failed' unless system(*runner, out)
abort 'native build failed' unless system('tcc', '-O2', '-std=c99',
  *BuildFlags.cflags('drm', 'zlib'),
  'tools/tests/graphics_scene_native_gate.c', 'src/blit_font.c',
  *BuildFlags.libs('drm', 'zlib'), '-o', native)
Dir.mktmpdir('blit-graphics-scene') do |work|
  abort 'native graphics scene failed' unless system(*runner, native, work)
end
