#!/usr/bin/env ruby
require 'json'
require 'digest'
require 'tmpdir'
require 'open3'
require 'yaml'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  schemas = YAML.load_file('openapi/blit-0.1.yaml').dig('components','schemas')
  policy = schemas.fetch('TextLayout')
  raise 'schema policy drift' unless policy['enum'] == %w[scalar emoji] && policy['default'] == 'scalar'
  %w[CellUpdate CreateTerminalSession TerminalSession].each do |name|
    raise "policy reference drift: #{name}" unless schemas.dig(name,'properties','text_layout','$ref') == '#/components/schemas/TextLayout'
  end
  manifest = JSON.parse(File.read('assets/unicode/emoji-18.0/source.json'))
  input = File.join('assets/unicode/emoji-18.0', manifest.fetch('test_data').fetch('file'))
  raise 'corpus identity mismatch' unless Digest::SHA256.file(input).hexdigest == manifest.fetch('test_data').fetch('sha256')
  rows = File.readlines(input).filter_map { |line| (m = line.match(/^([0-9A-F ]+)\s*;/)) && m[1].split }
  vectors = rows.map { |r| "#{r.size} #{r.join(' ')}" }.join("\n")
  Dir.mktmpdir('blit-emoji-layout') do |work|
    generated = File.join(work, 'data.h')
    raise 'generation failed' unless system('ruby', 'tools/fonts/build-emoji-sequences.rb', generated)
    raise 'derived trie stale' unless File.binread(generated) == File.binread('src/blit_emoji_sequences_data.h')
    binary = File.join(work, 'gate')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99',
      '-Ivendor/SDL3/include', '-Ibuild/deps/prefix/include', '-Ibuild/deps/prefix/include/libdrm',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/emoji_layout_gate.c', 'src/blit_kms.c', 'src/blit_vt.c', 'src/blit_font.c', 'src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),
      '-Lbuild/sdl3-kms', '-lSDL3', '-Lbuild/deps/prefix/lib/x86_64-linux-gnu', '-ldrm', '-lutil', '-lz', '-o', binary)
    command = [binary, rows.size.to_s]
    command = ['valgrind', '--error-exitcode=99', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect', *command] if ENV['BLIT_EMOJI_VALGRIND'] == '1'
    out, err, status = Open3.capture3({'LD_LIBRARY_PATH'=>"#{root}/build/sdl3-kms"}, *command, stdin_data:vectors)
    warn err unless err.empty?
    raise 'layout contract failed' unless status.success?
    puts out
  end
end
