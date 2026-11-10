#!/usr/bin/env ruby
require 'json'
require 'digest'
require 'tmpdir'
require_relative 'build_flags'
root = File.expand_path('../../vendor/harfbuzz', __dir__)
manifest = JSON.parse(File.read(File.join(root, 'source.json')))
raise 'unexpected API baseline' unless manifest.fetch('version') == '6.0.0' && manifest.fetch('runtime_minimum') == '6.0.0'
manifest.fetch('files').each do |file, digest|
  raise "header identity mismatch: #{file}" unless Digest::SHA256.file(File.join(root, file)).hexdigest == digest
end
raise 'unexpected header set' unless Dir.glob(File.join(root, '*.h')).map { |p| File.basename(p) }.sort == manifest.fetch('files').keys.grep(/\.h$/).sort
puts "HARFBUZZ_IDENTITY_PASS headers=#{manifest.fetch('files').size - 1} baseline=6.0.0 runtime=OS_maintained"
project = File.expand_path('../..', __dir__)
Dir.chdir(project) do
  raise 'package runtime baseline diverged' unless File.read('packaging/build-deb.sh').include?("libharfbuzz0b (>= #{manifest.fetch('runtime_minimum')})")
  Dir.mktmpdir('blit-harfbuzz-baseline') do |work|
    binary = File.join(work, 'gate')
    raise 'baseline compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      *BuildFlags.cflags('font'),
      'tools/tests/harfbuzz_baseline_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
      *BuildFlags.libs('font'), '-o', binary)
    raise 'baseline rejection failed' unless system(binary, 'assets/fonts/Blit-Terminus16.psf',
      'assets/fonts/text/Hack-Regular.ttf', 'assets/fonts/NotoColorEmoji.ttf')
  end
end
