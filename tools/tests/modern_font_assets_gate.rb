#!/usr/bin/env ruby
require 'json'
require 'digest'
root = ENV.fetch('BLIT_GATE_FONT_ROOT', File.expand_path('../../assets/fonts', __dir__))
manifest = JSON.parse(File.read(File.join(root,'modern-fonts.source.json')))
ui = manifest.fetch('ui_coverage')
raise 'UI coverage identity mismatch' unless Digest::SHA256.file(File.join(root, ui.fetch('file'))).hexdigest == ui.fetch('sha256')
manifest.fetch('custom_icons').fetch('sources').each do |file, hash|
  raise "custom icon identity mismatch: #{file}" unless Digest::SHA256.file(File.join(root, file)).hexdigest == hash
end
%w[outline retro colour text_emoji].each do |kind|
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
arabic = JSON.parse(File.read(File.join(root,'arabic-fonts.source.json')))
%w[retro modern].each do |kind|
  entry = arabic.fetch(kind)
  {'file'=>'sha256', 'license_file'=>'license_sha256', 'source_file'=>'source_sha256'}.each do |file_key, hash_key|
    next unless entry.key?(file_key)
    raise "Arabic #{kind} identity mismatch: #{file_key}" unless Digest::SHA256.file(File.join(root, entry.fetch(file_key))).hexdigest == entry.fetch(hash_key)
  end
end
raise 'Arabic specimens identity mismatch' unless Digest::SHA256.file(File.join(root, arabic.fetch('specimens_file'))).hexdigest == arabic.fetch('specimens_sha256')
puts "MODERN_FONT_ASSETS_PASS outlines=Modern+Retro nerd_codepoints=10386 colour=Noto#{manifest.fetch('colour').fetch('version')} text=Noto#{manifest.fetch('text_emoji').fetch('version')} source_hashes=exact licenses=included"
