#!/usr/bin/env ruby
require 'json'
require 'digest'
root = File.expand_path('../../assets/unicode/emoji-18.0', __dir__)
manifest = JSON.parse(File.read(File.join(root, 'source.json')))
%w[test_data license properties variations].each do |key|
  entry = manifest.fetch(key)
  raise "#{key} identity mismatch" unless Digest::SHA256.file(File.join(root, entry.fetch('file'))).hexdigest == entry.fetch('sha256')
end
rows = File.readlines(File.join(root, manifest.fetch('test_data').fetch('file'))).filter_map do |line|
  match = line.match(/^([0-9A-F ]+)\s*;\s*([a-z-]+)\s*#/)
  match && [match[1].split.map { |v| v.to_i(16) }, match[2]]
end
raise 'corpus counts changed' unless rows.group_by(&:last).transform_values(&:size) == manifest.fetch('entries')
raise 'sequence bound changed' unless rows.map { |r| r[0].size }.max == manifest.fetch('max_sequence_scalars')
raise 'duplicate corpus sequences' unless rows.map(&:first).uniq.size == rows.size
[
  [0x2764, 0xfe0f], # emoji presentation
  [0x31, 0xfe0f, 0x20e3], # keycap
  [0x1f1ec, 0x1f1e7], # flag
  [0x1f44d, 0x1f3fd], # modifier
  [0x1f469, 0x200d, 0x1f4bb], # profession
  [0x1f468, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466], # family
  [0x1f3f4, 0xe0067, 0xe0062, 0xe0065, 0xe006e, 0xe0067, 0xe007f] # tag flag
].each { |s| raise "missing category: #{s}" unless rows.include?([s, 'fully-qualified']) }
counts = manifest.fetch('entries')
rgi = counts.fetch('fully-qualified') + counts.fetch('component')
variants = counts.fetch('minimally-qualified') + counts.fetch('unqualified')
puts "EMOJI_CORPUS_PASS unicode=#{manifest.fetch('unicode_emoji_version')} rgi=#{rgi} qualification_variants=#{variants} max_scalars=#{manifest.fetch('max_sequence_scalars')} scope=data_identity_not_renderer_acceptance"
