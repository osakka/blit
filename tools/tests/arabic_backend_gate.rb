#!/usr/bin/env ruby
require 'tmpdir'
require 'json'
require_relative 'build_flags'
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
      *BuildFlags.cflags('server'),*sources,*BuildFlags.libs('server'),'-o',binary)
    # ADR 0241, 0243: the one Arabic face, Noto Naskh Arabic's words,
    # beside every text font.
    naskh="assets/fonts/#{JSON.parse(File.read('assets/fonts/fonts.source.json')).fetch('arabic').fetch('naskh').fetch('file')}"
    %w[Hack-Regular.ttf 3270-Regular.ttf].each do |profile|
      command=[binary,"assets/fonts/text/#{profile}",
        'assets/fonts/NotoColorEmoji.ttf',naskh]
      command=['valgrind','--error-exitcode=99','--leak-check=full',
        '--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_FONT_VALGRIND']=='1'
      raise "#{profile} failed" unless system(*command)
    end
  end
end
