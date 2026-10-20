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
end
