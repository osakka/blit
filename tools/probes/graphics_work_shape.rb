#!/usr/bin/env ruby
require 'json'
require 'open3'
require 'fileutils'
require 'digest'
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
FileUtils.mkdir_p('build/tests')
results = %w[native protocol_sdl].map do |mode|
  native = mode == 'native'
  out = "build/tests/graphics-work-shape-#{mode}"
  sources = %w[tools/probes/graphics_work_shape.c src/blit_font.c]
  sources += %w[src/blit_kms.c src/blit_vt.c src/blit_raster.c] unless native
  libraries = native ? [] : %w[/lib/x86_64-linux-gnu/libfreetype.so.6 /lib/x86_64-linux-gnu/libharfbuzz.so.0 -Lbuild/sdl3-kms -lSDL3 -lutil]
  command = ['tcc', '-O2', '-std=c99', *(['-DBLIT_PROBE_NATIVE'] if native),
    '-Ivendor/SDL3/include', '-Ibuild/deps/prefix/include',
    '-Ibuild/deps/prefix/include/libdrm', '-Ibuild/deps/prefix/include/freetype2',
    *sources, *libraries, '-Lbuild/deps/prefix/lib/x86_64-linux-gnu', '-ldrm', '-lz', '-o', out]
  abort "#{mode} build failed" unless system(*command, out: $stderr)
  stdout, stderr, status = Open3.capture3({ 'SDL_VIDEODRIVER' => 'dummy',
    'LD_LIBRARY_PATH' => "#{root}/build/sdl3-kms:#{root}/build/deps/prefix/lib/x86_64-linux-gnu" }, out)
  abort "#{mode}: exit=#{status.exitstatus} signal=#{status.termsig} #{stderr}" unless status.success?
  JSON.parse(stdout)
end
files = %w[tools/probes/graphics_work_shape.c src/blit_server.c src/blit_kms.c src/blit_graphics.h src/blit_graphics_scene.h]
puts JSON.pretty_generate({ 'scope' => 'current production work-shape observation, no live writes or FPS claims',
  'sha256' => files.to_h { |f| [f, Digest::SHA256.file(f).hexdigest] }, 'results' => results })
