#!/usr/bin/env ruby
require 'json'
require 'digest'
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
Dir.chdir(root) do
  manifest = JSON.parse(File.read('assets/unicode/emoji-18.0/source.json'))
  input = File.join('assets/unicode/emoji-18.0', manifest.fetch('test_data').fetch('file'))
  raise 'unreviewed corpus' unless Digest::SHA256.file(input).hexdigest == manifest.fetch('test_data').fetch('sha256')
  rows = File.readlines(input).filter_map { |line| (m = line.match(/^([0-9A-F ]+)\s*;/)) && m[1].split }
  raise 'incomplete corpus' unless rows.size == manifest.fetch('entries').values.sum
  vectors = rows.map { |r| "#{r.size} #{r.join(' ')}" }.join("\n")
  Dir.mktmpdir('blit-emoji-raster') do |work|
    binary = File.join(work, 'gate')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      "-I#{ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{root}/build/deps/prefix/include/freetype2")}",
      'tools/tests/emoji_cluster_raster_gate.c', 'src/blit_font.c', 'src/blit_raster.c',
      ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-o', binary)
    %w[BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf].each do |profile|
      command = [binary, 'assets/fonts/Blit-Terminus16.psf', "assets/fonts/#{profile}",
                 'assets/fonts/NotoColorEmoji.ttf', rows.size.to_s]
      command = ['valgrind', '--error-exitcode=99', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect', *command] if ENV['BLIT_FONT_VALGRIND'] == '1'
      out, err, status = Open3.capture3(*command, stdin_data: vectors)
      warn err unless err.empty?
      raise "cluster raster failed: #{profile}" unless status.success?
      puts "#{profile}: #{out}"
    end
  end
end
