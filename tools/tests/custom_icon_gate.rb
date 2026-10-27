#!/usr/bin/env ruby
require 'json'
require 'digest'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  source = JSON.parse(File.read('assets/fonts/modern-fonts.source.json'))
  manifest = source.fetch('custom_icons')
  ui = source.fetch('ui_coverage')
  ui_path = "assets/fonts/#{ui.fetch('file')}"
  raise 'UI corpus identity mismatch' unless Digest::SHA256.file(ui_path).hexdigest == ui.fetch('sha256')
  required = JSON.parse(File.read(ui_path)).fetch('scalars')
  raise 'UI corpus count mismatch' unless required.size == ui.fetch('scalars') && required.uniq == required
  manifest.fetch('sources').each do |path, sha|
    raise "custom asset identity mismatch: #{path}" unless Digest::SHA256.file("assets/fonts/#{path}").hexdigest == sha
  end
  icons = JSON.parse(File.read("assets/fonts/#{manifest.fetch('catalogue')}"))
  expected = %w[blit hibr-pen wifi-1 wifi-2 wifi-3 wifi-4 bluetooth]
  raise 'incomplete original catalogue' unless icons.fetch('icons').map { |i| i.fetch('id') } == expected
  raise 'unexpected private mappings' unless icons.fetch('icons').map { |i| i.fetch('codepoint').to_i(16) } == (0x100000..0x100006).to_a
  Dir.mktmpdir('blit-custom-icons') do |work|
    binary = File.join(work, 'gate')
    input = File.join(work, 'ui.txt')
    File.write(input, required.join("\n")+"\n")
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/custom_icon_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-o', binary)
    %w[BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf].each do |face|
      command = [binary, 'assets/fonts/Blit-Terminus16.psf', "assets/fonts/#{face}", 'assets/fonts/NotoColorEmoji.ttf', input]
      command = ['valgrind', '--error-exitcode=99', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect', *command] if ENV['BLIT_FONT_VALGRIND'] == '1'
      raise "custom glyph raster failed: #{face}" unless system(*command)
    end
  end
end
