#!/usr/bin/env ruby
# ADR 0195: every font file Blit ships is the one fonts.source.json pins,
# with its licence: the text fonts in text/ (and nothing else there), the
# glyph stack, the emoji faces, the Arabic companion, the Nerd catalogue
# and Blit's icon sources. The text fonts are shipped unmodified, so their
# hashes are the upstream release's files.
require 'json'
require 'digest'
root = ENV.fetch('BLIT_GATE_FONT_ROOT', File.expand_path('../../assets/fonts', __dir__))
manifest = JSON.parse(File.read(File.join(root, 'fonts.source.json')))
def check(root, file, hash, what)
  path = File.join(root, file)
  raise "#{what} identity mismatch: #{file}" unless File.file?(path) && Digest::SHA256.file(path).hexdigest == hash
end
text = manifest.fetch('text')
text.each do |entry|
  check(root, entry.fetch('file'), entry.fetch('sha256'), 'text font')
  check(root, entry.fetch('license_file'), entry.fetch('license_sha256'), 'text font licence')
end
shipped = Dir.children(File.join(root, 'text')).sort
pinned = text.map { |e| File.basename(e.fetch('file')) }.sort
raise "text/ holds unpinned files: #{shipped - pinned}" unless shipped == pinned
stack = manifest.fetch('stack')
check(root, stack.fetch('icons').fetch('file'), stack.fetch('icons').fetch('sha256'), 'Blit icons')
([stack.fetch('nerd')] + stack.fetch('symbols') + stack.fetch('unifont')).each do |entry|
  check(root, entry.fetch('file'), entry.fetch('sha256'), 'stack face')
  check(root, entry.fetch('license_file'), entry.fetch('license_sha256'), 'stack licence')
end
%w[colour text_emoji].each do |kind|
  entry = manifest.fetch(kind)
  check(root, entry.fetch('file'), entry.fetch('sha256'), kind)
  check(root, entry.fetch('license_file'), entry.fetch('license_sha256'), "#{kind} licence")
end
ui = manifest.fetch('ui_coverage')
check(root, ui.fetch('file'), ui.fetch('sha256'), 'UI coverage')
manifest.fetch('custom_icons').fetch('sources').each { |file, hash| check(root, file, hash, 'custom icon') }
catalogue = manifest.fetch('catalogue')
check(root, catalogue.fetch('file'), catalogue.fetch('sha256'), 'catalogue')
scalars = JSON.parse(File.read(File.join(root, catalogue.fetch('file')))).values.grep(Hash).filter_map { |v| v['code'] }.uniq
raise 'catalogue count mismatch' unless scalars.size == catalogue.fetch('unique_codepoints')
manifest.fetch('icon_licenses').each { |entry| check(root, entry.fetch('file'), entry.fetch('sha256'), 'icon licence') }
arabic = JSON.parse(File.read(File.join(root, 'arabic-fonts.source.json')))
entry = arabic.fetch('modern')
check(root, entry.fetch('file'), entry.fetch('sha256'), 'Arabic companion')
check(root, entry.fetch('license_file'), entry.fetch('license_sha256'), 'Arabic licence')
check(root, entry.fetch('source_file'), entry.fetch('source_sha256'), 'Arabic source')
check(root, arabic.fetch('specimens_file'), arabic.fetch('specimens_sha256'), 'Arabic specimens')
puts "FONT_ASSETS_PASS text=#{text.size} stack=icons+nerd+#{stack.fetch('symbols').size}_symbols+unifont " \
     "nerd_codepoints=#{scalars.size} colour=Noto#{manifest.fetch('colour').fetch('version')} source_hashes=exact licences=included"
