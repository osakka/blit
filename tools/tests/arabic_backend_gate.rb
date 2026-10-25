#!/usr/bin/env ruby
require 'tmpdir'
root=File.expand_path('../..',__dir__)
# Native KMS is the only presentation path (ADR 0184); the harness draws
# Arabic through the KMS compositor's own cell path.
Dir.chdir(root) do
  Dir.mktmpdir('blit-arabic-backend') do |work|
    binary=File.join(work,'gate')
    extra=case ENV['BLIT_ARABIC_NEGATIVE']
    when nil then []
    when 'damage' then ['-DBLIT_TEST_NO_INK_DAMAGE']
    else abort 'negative control: damage'
    end
    sources=['tools/tests/arabic_backend_gate.c','src/blit_font.c','src/blit_raster.c','src/blit_vt.c']
    raise 'compile failed' unless system('tcc','-O2','-std=c99',*extra,
      '-Ibuild/deps/prefix/include','-Ibuild/deps/prefix/include/libdrm',
      '-Ibuild/deps/prefix/include/freetype2',*sources,
      '/lib/x86_64-linux-gnu/libfreetype.so.6','/lib/x86_64-linux-gnu/libharfbuzz.so.0',
      '-Lbuild/deps/prefix/lib/x86_64-linux-gnu','-ldrm','-lutil','-lz','-o',binary)
    env={'LD_LIBRARY_PATH'=>"#{root}/build/deps/prefix/lib/x86_64-linux-gnu"}
    %w[Retro Modern].each do |profile|
      command=[binary,"assets/fonts/Blit#{profile}Nerd-Regular.ttf",
        'assets/fonts/NotoColorEmoji.ttf',"assets/fonts/BlitArabic#{profile}.ttf"]
      command=['valgrind','--error-exitcode=99','--leak-check=full',
        '--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      raise "#{profile} failed" unless system(env,*command)
    end
  end
end
