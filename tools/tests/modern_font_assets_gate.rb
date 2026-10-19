#!/usr/bin/env ruby
require 'json'
require 'digest'
root = File.expand_path('../../assets/fonts', __dir__)
manifest = JSON.parse(File.read(File.join(root,'modern-fonts.source.json')))
%w[outline colour].each do |kind|
  entry = manifest.fetch(kind)
  {'file'=>'sha256', 'license_file'=>'license_sha256'}.each do |file_key, hash_key|
    path = File.join(root, entry.fetch(file_key))
    raise "asset identity mismatch: #{path}" unless Digest::SHA256.file(path).hexdigest == entry.fetch(hash_key)
  end
end
puts 'MODERN_FONT_ASSETS_PASS outline=Hack3.003 colour=Noto2.042 source_hashes=exact licenses=included'
