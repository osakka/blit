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
  rows = File.readlines(input).filter_map do |line|
    match = line.match(/^([0-9A-F ]+)\s*;/)
    match && match[1].split
  end
  raise 'incomplete corpus' unless rows.size == manifest.fetch('entries').values.sum
  vectors = rows.map { |r| "#{r.size} #{r.join(' ')}" }.join("\n")
  Dir.mktmpdir('blit-cell-cluster') do |work|
    binary = File.join(work, 'gate')
    raise 'compile failed' unless system('tcc', '-O2', '-std=c99', '-Wall',
      'tools/tests/cell_cluster_gate.c', '-o', binary)
    out, err, status = Open3.capture3(binary, rows.size.to_s, stdin_data:vectors)
    warn err unless err.empty?
    raise 'cluster contract failed' unless status.success?
    puts out
  end
end
