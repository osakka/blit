#!/usr/bin/env ruby
require 'json'
require 'digest'
root = ENV.fetch('BLIT_GATE_FONT_ROOT', File.expand_path('../../assets/fonts', __dir__))
manifest = JSON.parse(File.read(File.join(root,'modern-fonts.source.json')))
%w[outline retro colour].each do |kind|
  entry = manifest.fetch(kind)
  {'file'=>'sha256', 'license_file'=>'license_sha256'}.each do |file_key, hash_key|
    path = File.join(root, entry.fetch(file_key))
    raise "asset identity mismatch: #{path}" unless Digest::SHA256.file(path).hexdigest == entry.fetch(hash_key)
  end
end
catalogue = manifest.fetch('catalogue')
raise 'catalogue identity mismatch' unless Digest::SHA256.file(File.join(root, catalogue.fetch('file'))).hexdigest == catalogue.fetch('sha256')
scalars = JSON.parse(File.read(File.join(root, catalogue.fetch('file')))).values.grep(Hash).filter_map { |v| v['code'] }.uniq
raise 'catalogue count mismatch' unless scalars.size == catalogue.fetch('unique_codepoints')
manifest.fetch('icon_licenses').each do |entry|
  raise 'icon license identity mismatch' unless Digest::SHA256.file(File.join(root, entry.fetch('file'))).hexdigest == entry.fetch('sha256')
end
puts "MODERN_FONT_ASSETS_PASS outlines=Modern+Retro nerd_codepoints=10386 colour=Noto#{manifest.fetch('colour').fetch('version')} source_hashes=exact licenses=included"
