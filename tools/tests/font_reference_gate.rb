#!/usr/bin/env ruby
# Documentation/canonical-input consistency only; no display or Hibr changes.
require 'json'
require 'digest'
require 'pathname'
root = File.expand_path('../..', __dir__)
fonts = File.join(root, 'assets/fonts')
pages = File.join(root, 'docs/fonts')
nerd = JSON.parse(File.read(File.join(fonts, 'nerd-glyphnames-v3.4.0.json'))).reject { |k, _| k == 'METADATA' }
found = {}
Dir.glob(File.join(pages, '*.md')).each do |path|
  File.foreach(path) do |line|
    row = line.strip.match(/^\| `([^`]+)` \| `U\+([0-9A-F]+)` \| `(.*?)` \| `(.*?)` \|$/)
    next unless row
    name, hex, glyph, encoded = row.captures
    raise "duplicate glyph name #{name}" if found.key?(name)
    source = nerd.fetch(name)
    raise "wrong glyph row #{name}" unless hex.to_i(16) == source.fetch('code').to_i(16) && glyph == source.fetch('char') && JSON.parse(encoded) == glyph
    found[name] = hex
  end
end
raise 'Nerd reference incomplete' unless found.keys.sort == nerd.keys.sort
icons = JSON.parse(File.read(File.join(fonts, 'icons/catalogue.json')))
special = File.read(File.join(pages, 'special.md'))
icons.fetch('icons').each do |icon|
  hex = icon.fetch('codepoint')
  raise "missing special icon #{icon['id']}" unless special.include?("| `#{icon['id']}` | `U+#{hex}` |")
  row = special.lines.find { |l| l.start_with?("| `#{icon['id']}` |") }
  encoded = row.split(' | ')[2].delete_prefix('`').delete_suffix('`')
  raise 'private scalar JSON encoding incorrect' unless JSON.parse(encoded).codepoints == [hex.to_i(16)]
end
coverage = JSON.parse(File.read(File.join(pages, 'coverage.json')))
raise 'native inventory incomplete' unless coverage.fetch('faces').size == 6
coverage.fetch('faces').each_value do |entry|
  raise 'font hash drift' unless Digest::SHA256.file(File.join(fonts, entry.fetch('file'))).hexdigest == entry.fetch('sha256')
  last = -2
  count = entry.fetch('ranges').sum do |a, b|
    a, b = a.to_i(16), b.to_i(16)
    raise 'invalid/noncanonical cmap ranges' unless a > last + 1 && a <= b && b <= 0x10ffff && (b < 0xd800 || a > 0xdfff)
    last = b
    b - a + 1
  end
  raise 'cmap count mismatch' unless count == entry.fetch('scalar_count')
end
emoji = Dir.glob(File.join(pages, 'emoji-[0-9][0-9].md')).flat_map { |p| File.readlines(p).grep(/^\| .* \| (?:fully-qualified|component|minimally-qualified|unqualified) \|$/) }
data = File.read(File.join(root, 'assets/unicode/emoji-18.0/emoji-test.txt'))
source_sequences = data.lines.reject { |l| l.start_with?('#') || l.strip.empty? }.map { |l| l.split(';').first.split.map { |h| h.to_i(16) } }
actual_sequences = emoji.map do |line|
  # Each row's final code-formatted field is a complete JSON string.
  encoded = line.scan(/`([^`]*)`/).last.fetch(0)
  JSON.parse(encoded).codepoints
end
raise 'emoji reference incomplete/reordered' unless actual_sequences == source_sequences
documents = Dir.glob(File.join(pages, '*.md')) + [File.join(root, 'docs/font-reference.md')]
documents.each do |file|
  File.read(file).scan(/\]\(([^)]+)\)/).flatten.each do |href|
    next if href.start_with?('http:', 'https:', '#')
    target = Pathname.new(File.dirname(file)).join(href.split('#').first).cleanpath
    raise "broken local link #{file}: #{href}" unless target.file?
  end
end
puts "FONT_REFERENCE_PASS nerd_names=#{found.size} nerd_scalars=#{found.values.uniq.size} originals=#{icons['icons'].size} emoji_entries=#{emoji.size} native_faces=6 links=local_exact scope=documentation_not_renderer"
