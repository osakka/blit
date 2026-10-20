#!/usr/bin/env ruby
require 'tmpdir'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-sdl-kms-policy-') do |work|
  exe=File.join(work,'fixture')
  raise 'compile failed' unless system('tcc','-std=c99',"-I#{root}/vendor/SDL3/include",
    File.join(root,'tools/tests/sdl_kms_policy_fixture.c'),"-L#{root}/build/sdl3-kms",'-lSDL3','-o',exe)
  env={'LD_LIBRARY_PATH'=>"#{root}/build/sdl3-kms",'SDL_KMSDRM_ATOMIC'=>nil}
  raise 'policy gate failed' unless system(env,exe)
  raise 'conflict gate failed' unless system(env.merge('SDL_KMSDRM_ATOMIC'=>'1'),exe,'conflict')
  discovery=File.join(work,'discovery')
  raise 'discovery fixture compile failed' unless system('tcc','-O2','-std=c99',
    "-I#{root}/vendor/SDL3/include","-I#{root}/build/deps/prefix/include",
    "-I#{root}/build/deps/prefix/include/libdrm","-I#{root}/build/deps/prefix/include/freetype2",
    File.join(root,'tools/tests/sdl_kms_policy_discovery_gate.c'),File.join(root,'src/blit_kms.c'),
    File.join(root,'src/blit_font.c'),File.join(root,'src/blit_raster.c'),
    '/lib/x86_64-linux-gnu/libfreetype.so.6',
    ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),"-L#{root}/build/sdl3-kms",'-lSDL3',
    "-L#{root}/build/deps/prefix/lib/x86_64-linux-gnu",'-ldrm','-lutil','-lz','-o',discovery)
  raise 'discovery default failed' unless system(env,discovery)
  raise 'discovery conflict failed' unless system(env.merge('SDL_KMSDRM_ATOMIC'=>'1'),discovery,'conflict')
  raise 'other-driver conflict changed' unless system(env.merge('SDL_KMSDRM_ATOMIC'=>'1'),discovery,'other')
end
