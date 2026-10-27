#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0199: both pixel fonts keep their own glyphs and borrow the glyph
# stack for the rest, at every size from 8 to 64 px.
require 'json'
require 'digest'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  source = JSON.parse(File.read('assets/fonts/fonts.source.json'))
  ui = source.fetch('ui_coverage')
  ui_path = "assets/fonts/#{ui.fetch('file')}"
  raise 'UI corpus identity mismatch' unless Digest::SHA256.file(ui_path).hexdigest == ui.fetch('sha256')
  Dir.mktmpdir('blit-pixel-stack') do |work|
    binary = File.join(work, 'gate')
    input = File.join(work, 'ui.txt')
    File.write(input, JSON.parse(File.read(ui_path)).fetch('scalars').join("\n") + "\n")
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/pixel_font_stack_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-lm', '-o', binary)
    %w[Blit-Terminus16.psf Blit-Spleen16.psf].each do |face|
      raise "pixel font stack failed: #{face}" unless
        system(binary, "assets/fonts/#{face}", 'assets/fonts/NotoColorEmoji.ttf', input)
    end
  end
end
