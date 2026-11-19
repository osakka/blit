#!/usr/bin/env ruby
# ADR 0237: the player glyphs at every cell size, through the raster.
require 'tmpdir'
require_relative 'build_flags'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  Dir.mktmpdir('blit-player-glyphs') do |work|
    binary = File.join(work, 'gate')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall', *BuildFlags.cflags('font'),
                                         'tools/tests/player_glyph_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
                                         *BuildFlags.libs('font'), '-lm', '-o', binary)
    exit(system(binary, 'assets/fonts/Blit-Terminus16.psf', 'assets/fonts/text/Hack-Regular.ttf',
                 'assets/fonts/NotoColorEmoji.ttf') ? 0 : 1)
  end
end
