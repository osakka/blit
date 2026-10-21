#!/usr/bin/env ruby
require 'fileutils'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
out = "build/tests/graphics-placement-damage-#{Process.pid}"
native = "build/tests/graphics-scene-native-#{Process.pid}"
at_exit { [out, native].each { |path| File.delete(path) if File.file?(path) } }
runner = ENV['BLIT_GRAPHICS_VALGRIND'] == '1' ? %w[valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite,indirect] : []
abort 'build failed' unless system('tcc', '-O2', '-std=c99',
  '-Ivendor/SDL3/include', '-Ibuild/deps/prefix/include', '-Ibuild/deps/prefix/include/libdrm',
  '-Ibuild/deps/prefix/include/freetype2', 'tools/tests/graphics_placement_damage_gate.c',
  'src/blit_kms.c', 'src/blit_vt.c', 'src/blit_font.c', 'src/blit_raster.c',
  '/lib/x86_64-linux-gnu/libfreetype.so.6', '/lib/x86_64-linux-gnu/libharfbuzz.so.0',
  '-Lbuild/sdl3-kms', '-lSDL3', '-Lbuild/deps/prefix/lib/x86_64-linux-gnu',
  '-ldrm', '-lutil', '-lz', '-o', out)
abort 'graphics placement damage failed' unless system({ 'SDL_VIDEODRIVER' => ENV.fetch('BLIT_TEST_VIDEO_DRIVER','dummy'), 'LD_LIBRARY_PATH' =>
  "#{root}/build/sdl3-kms:#{root}/build/deps/prefix/lib/x86_64-linux-gnu" }, *runner, out)
abort 'native build failed' unless system('tcc', '-O2', '-std=c99',
  '-Ivendor/SDL3/include', '-Ibuild/deps/prefix/include', '-Ibuild/deps/prefix/include/libdrm',
  'tools/tests/graphics_scene_native_gate.c', 'src/blit_font.c',
  '-Lbuild/deps/prefix/lib/x86_64-linux-gnu', '-ldrm', '-lz', '-o', native)
Dir.mktmpdir('blit-graphics-scene') do |work|
  abort 'native graphics scene failed' unless system({ 'LD_LIBRARY_PATH' =>
    "#{root}/build/deps/prefix/lib/x86_64-linux-gnu" }, *runner, native, work)
end
