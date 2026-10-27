#!/usr/bin/env ruby
# Display-independent workload of the production cache; does not change fonts.
require 'json'
require 'open3'
require 'digest'
require 'fileutils'
abort 'usage: font_cache_workload.rb [--check]' unless ARGV.empty? || ARGV == ['--check']
root = File.expand_path('../..', __dir__)
Dir.chdir(root)
out = 'build/tests/font-cache-workload'
FileUtils.mkdir_p(File.dirname(out))
sources = %w[tools/probes/font_cache_workload.c src/blit_font.c src/blit_raster.c]
command = ['tcc', '-O2', '-std=c99',
           "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', 'build/deps/prefix/include/freetype2')}",
           *sources,
           ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
           ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),
           '-o', out]
abort 'TCC probe build failed' unless system(*command, out: $stderr)
ui = JSON.parse(File.read('assets/fonts/hibr-ui-glyphs.json')).fetch('scalars').map { |s| s.to_i(16) }
workloads = {
  'ascii' => (32..126).to_a,
  'ascii_and_hibr_ui' => ((32..126).to_a + ui).uniq,
  # Same hash bucket in the released 128-slot direct-mapped cache. All use
  # one cell to isolate replacement policy, not terminal width negotiation.
  'three_colliding_keys' => [0x41, 0xfec1, 0x1f341]
}
# ADR 0195: plain text fonts with the stack; one Arabic companion.
results = { 'Retro' => '3270-Regular.ttf', 'Modern' => 'Hack-Regular.ttf' }.flat_map do |profile, text|
  workloads.map do |name, keys|
    args = [out, 'assets/fonts/Blit-Terminus16.psf',
            "assets/fonts/text/#{text}", 'assets/fonts/NotoColorEmoji.ttf',
            'assets/fonts/BlitArabicModern.ttf']
    stdout, stderr, status = Open3.capture3(*args, stdin_data: keys.map { |k| k.to_s(16) }.join("\n") + "\n")
    abort "#{profile}/#{name} failed (#{status.exitstatus}): #{stderr}" unless status.success?
    JSON.parse(stdout).merge('profile' => profile, 'workload' => name)
  end
end
if ARGV == ['--check']
  abort 'warm-cache regression or memory bound exceeded' unless results.all? do |r|
    r.fetch('warm_rasterizations').zero? && r.fetch('cached_bytes') <= 4 * 1024 * 1024
  end
end
files = sources + %w[src/blit_raster.h src/blit_font.h src/blit_cell_cluster.h
  assets/fonts/hibr-ui-glyphs.json assets/fonts/Blit-Terminus16.psf
  assets/fonts/text/3270-Regular.ttf assets/fonts/text/Hack-Regular.ttf
  assets/fonts/BlitArabicModern.ttf assets/fonts/NotoColorEmoji.ttf]
puts JSON.pretty_generate({
  'scope' => 'local shared-raster cache only; not live drawing, SDL atlas, or display FPS',
  'cell_pixels' => [11, 22], 'columns' => 1,
  'sha256' => files.to_h { |f| [f, Digest::SHA256.file(f).hexdigest] },
  'results' => results
})
