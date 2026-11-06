#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0212: a catalogue a client can map dynamically. Every `cell_geometry`
# entry (the bar corners, the window chrome, the controls) says, as data,
# what it is and which pens an application sets for it:
#   part        what it is a piece of
#   cells       {span, position}: the whole piece's width, this glyph's place
#   background  what the cell's background should be
#   foreground  what the cell's foreground is
#   line        what SGR 58 is
# - Every entry has all five, each from the vocabulary written out here, and
#   no other field than the ones the schema names.
# - The vocabulary is the OpenAPI schema's enums, word for word, and every
#   word is documented in assets/fonts/icons/README.md and used by an entry.
# - The fields are coherent: a multi-cell piece's cells are consecutive
#   codepoints, positions run from 0, a switch spans two cells and a capsule
#   three; Chalk (`pens: foreground`) has no line pen and Slate or the
#   capsule (`pens: cell`) has one unless it is only frame lines.
# - ADR 0214: a glyph a later one replaces says so, as data: `deprecated:
#   true` and `replaced_by`, the ids that replace it. Exactly the old frame
#   glyphs (every frame piece beside the content, in Chalk and Slate) and
#   stripe-first/-last are deprecated; each replacement exists and is not
#   itself deprecated; nothing else carries either field. The one frame set
#   (`set: frame`, U+1002AE-1002B2, on a `panel`, the window's face) and the
#   title-end cells exist in the vocabulary the schema and the README give.
# - The catalogue is exactly what the glyph tables generate, structured
#   fields included: both design scripts' --check pass.
# - The catalogue stays within the API's 256 KiB, and its pin is its hash.

require 'digest'
require 'json'
require 'open3'
require 'yaml'

ROOT = File.expand_path('../..', __dir__)
Dir.chdir(ROOT)

PARTS = %w[bar-corner title-bar note-close frame dialog-border button-cap checkbox switch capsule].freeze
BACKGROUNDS = %w[bar panel wallpaper dialog-face button-face].freeze
FOREGROUNDS = %w[ink track bar].freeze
LINES = %w[none highlight box-face dialog-face button-face knob].freeze
SETS = %w[chalk slate-light slate capsule frame].freeze
KEYS = %w[id codepoint colour shape pens set part cells background foreground line deprecated replaced_by].freeze
# ADR 0214: what is deprecated, and the one frame set that replaces it.
OLD_FRAME = %w[frame-left frame-right-top frame-right frame-bottom-left frame-bottom-first frame-bottom
               frame-bottom-right stripe-first stripe-last].freeze
DEPRECATED = %w[chalk slate-light].flat_map { |set| OLD_FRAME.map { |n| "#{set}-#{n}" } }.freeze
NEW_FRAME = %w[frame-left frame-right frame-bottom-left frame-bottom frame-bottom-right].freeze

def check(ok, why)
  abort "CATALOGUE_FIELDS_FAIL #{why}" unless ok
end

path = 'assets/fonts/icons/catalogue.json'
text = File.read(path)
catalogue = JSON.parse(text)
entries = catalogue.fetch('cell_geometry')
check(!entries.empty?, 'the catalogue has no cell_geometry')
check(entries.map { |e| e['id'] }.uniq.size == entries.size, 'cell_geometry ids are not unique')
codes = entries.map { |e| e['codepoint'].to_i(16) }
check(codes == codes.sort.uniq, 'cell_geometry is not in codepoint order')

entries.each do |e|
  id = e['id']
  extra = e.keys - KEYS
  check(extra.empty?, "#{id} has fields the schema does not name: #{extra}")
  %w[id codepoint colour shape part cells background foreground line].each do |k|
    check(e.key?(k), "#{id} has no #{k}")
  end
  check(e['colour'] == false, "#{id}: colour is not false")
  check(PARTS.include?(e['part']), "#{id}: part #{e['part'].inspect} is not in the vocabulary")
  check(BACKGROUNDS.include?(e['background']), "#{id}: background #{e['background'].inspect} is not in the vocabulary")
  check(FOREGROUNDS.include?(e['foreground']), "#{id}: foreground #{e['foreground'].inspect} is not in the vocabulary")
  check(LINES.include?(e['line']), "#{id}: line #{e['line'].inspect} is not in the vocabulary")
  c = e['cells']
  check(c.is_a?(Hash) && c.keys.sort == %w[position span] && c['span'].is_a?(Integer) && c['position'].is_a?(Integer),
        "#{id}: cells is not {span, position}")
  check(c['span'].between?(1, 3) && c['position'].between?(0, c['span'] - 1), "#{id}: cells #{c} is out of range")
  check(e.key?('pens') == e.key?('set'), "#{id}: pens and set go together")
  next unless e.key?('pens')

  check(%w[foreground cell].include?(e['pens']), "#{id}: pens #{e['pens'].inspect}")
  check(SETS.include?(e['set']), "#{id}: set #{e['set'].inspect}")
  check((e['pens'] == 'foreground') == %w[chalk frame].include?(e['set']), "#{id}: only Chalk and the frame are drawn in the foreground alone")
  check(e['pens'] != 'foreground' || e['line'] == 'none', "#{id}: a one-colour glyph has no line pen to set")
  expected_span = { 'switch' => 2, 'capsule' => 3 }.fetch(e['part'], 1)
  check(c['span'] == expected_span, "#{id}: a #{e['part']} spans #{expected_span} cells, not #{c['span']}")
  check(e['part'] != 'capsule' || (e['foreground'] == 'track' && e['background'] == 'panel' && e['line'] == 'knob'),
        "#{id}: a capsule's track is the foreground, its knob the line, on a panel")
  check(e['foreground'] != 'track' || e['part'] == 'capsule', "#{id}: only the capsule has a track")
end
# Deprecation (ADR 0214).
by_id = entries.to_h { |e| [e['id'], e] }
entries.each do |e|
  check(e.key?('deprecated') == e.key?('replaced_by'), "#{e['id']}: deprecated and replaced_by go together")
  next unless e.key?('deprecated')

  check(e['deprecated'] == true, "#{e['id']}: deprecated is not true (omit it if not deprecated)")
  r = e['replaced_by']
  check(r.is_a?(Array) && !r.empty? && r.all?(String) && r.uniq == r, "#{e['id']}: replaced_by is not a list of ids")
  r.each do |to|
    check(by_id.key?(to), "#{e['id']}: replaced_by #{to} is not in the catalogue")
    check(!by_id[to].key?('deprecated'), "#{e['id']}: replaced_by #{to}, which is itself deprecated")
  end
end
check(entries.select { |e| e.key?('deprecated') }.map { |e| e['id'] } == DEPRECATED.sort_by { |id| by_id.fetch(id)['codepoint'] },
      "the deprecated entries are #{entries.select { |e| e.key?('deprecated') }.map { |e| e['id'] }.inspect}, not the old frame glyphs and stripe-first/-last")
check(NEW_FRAME.each_with_index.all? { |n, i| by_id[n] && by_id[n]['codepoint'].to_i(16) == 0x1002AE + i && by_id[n]['set'] == 'frame' &&
                                         by_id[n]['part'] == 'frame' && by_id[n]['background'] == 'panel' && by_id[n]['line'] == 'none' },
      'the one frame set (frame-left ... frame-bottom-right, U+1002AE-1002B2, a frame on a panel) is not in the catalogue')
check(%w[chalk slate-light].all? { |s| %w[left right].all? { |d| by_id["#{s}-title-end-#{d}"]&.values_at('part', 'background', 'cells') == ['title-bar', 'bar', { 'span' => 1, 'position' => 0 }] } },
      'the title-end cells are not title-bar pieces on the bar')
check(%w[chalk-frame-left slate-light-frame-right-top].map { |id| by_id[id]['replaced_by'] } ==
      [%w[frame-left chalk-title-end-left], %w[slate-light-title-end-right]], 'frame-left or frame-right-top is not replaced as ADR 0214 says')
check(entries.select { |e| e['part'] == 'bar-corner' }.all? { |e| e['foreground'] == 'bar' && e['background'] == 'wallpaper' },
      "the bar corners' fill is the bar, on the wallpaper")

# A multi-cell piece's cells are consecutive codepoints, from position 0.
entries.each_with_index do |e, i|
  c = e['cells']
  next if c['position'].zero?

  prev = entries[i - 1]
  check(prev['codepoint'].to_i(16) + 1 == e['codepoint'].to_i(16) && %w[part set pens].all? { |k| prev[k] == e[k] } &&
        prev['cells'] == { 'span' => c['span'], 'position' => c['position'] - 1 },
        "#{e['id']}: cell #{c['position']} of #{c['span']} does not follow cell #{c['position'] - 1}")
end
entries.each_with_index do |e, i|
  c = e['cells']
  check(i + (c['span'] - c['position']) - 1 < entries.size && entries[i + c['span'] - c['position'] - 1]['cells']['position'] == c['span'] - 1,
        "#{e['id']}: the piece does not run to its last cell")
end

# The vocabulary is the schema's and the README's, and each word is used.
schema = YAML.load_file('openapi/blit-0.1.yaml').dig('components', 'schemas', 'CellGeometryGlyph', 'properties')
{ 'part' => PARTS, 'background' => BACKGROUNDS, 'foreground' => FOREGROUNDS, 'line' => LINES }.each do |field, words|
  check(schema.dig(field, 'enum') == words, "the OpenAPI #{field} enum is #{schema.dig(field, 'enum')}, not #{words}")
  used = entries.map { |e| e[field] }.uniq
  check((words - used).empty?, "#{field} words no entry uses: #{words - used}")
  readme = File.read('assets/fonts/icons/README.md')
  missing = words.reject { |w| readme.match?(/^\| `#{field}` \|.*`#{Regexp.escape(w)}`/) }
  check(missing.empty?, "assets/fonts/icons/README.md does not document #{field}: #{missing}")
end
check(schema.dig('set', 'enum').sort == SETS.sort && schema.dig('pens', 'enum') == %w[foreground cell],
      'the OpenAPI set or pens enum drifted')
check(schema.dig('deprecated', 'type') == 'boolean' && schema.dig('replaced_by', 'type') == 'array' &&
      schema.dig('replaced_by', 'items', 'type') == 'string', 'the OpenAPI schema does not type deprecated and replaced_by')
readme = File.read('assets/fonts/icons/README.md')
%w[deprecated replaced_by].each do |field|
  check(readme.match?(/^\| `#{field}` \|/), "assets/fonts/icons/README.md does not document #{field}")
end
check(readme.match?(/`frame`.*the one frame set/m), 'assets/fonts/icons/README.md does not document the frame set')

# 0.3.55: an icon can be deprecated the same way. Exactly wifi-1..4 are: the
# bars are the cellular signal, cellular-1..4, the same drawing and level at
# new codepoints (one codepoint is one id); wifi-signal-1..4 are the Wi-Fi fan.
icons = catalogue.fetch('icons')
icon_by_id = icons.to_h { |e| [e['id'], e] }
icons.each do |e|
  check(e.key?('deprecated') == e.key?('replaced_by'), "#{e['id']}: deprecated and replaced_by go together")
  next unless e.key?('deprecated')

  check(e['deprecated'] == true, "#{e['id']}: deprecated is not true (omit it if not deprecated)")
  r = e['replaced_by']
  check(r.is_a?(Array) && !r.empty? && r.all? { |to| icon_by_id.key?(to) && !icon_by_id[to].key?('deprecated') },
        "#{e['id']}: replaced_by does not name live icons")
end
check(icons.select { |e| e.key?('deprecated') }.map { |e| [e['id'], e['replaced_by']] } == (1..4).map { |l| ["wifi-#{l}", ["cellular-#{l}"]] },
      'the deprecated icons are not exactly wifi-1..4, replaced by cellular-1..4')
check((1..4).all? { |l| icon_by_id["cellular-#{l}"]&.values_at('source', 'signal_level') == ['wifi-bars.svg', l] },
      'cellular-1..4 are not the bars at their levels')

# Generated, not hand-kept.
%w[draw-window-glyphs.py draw-control-glyphs.py].each do |script|
  out, status = Open3.capture2e('python3', "tools/fonts/#{script}", '--check')
  check(status.success?, "#{script} --check: #{out.strip}")
end

# The API's cap, and the pin.
check(text.bytesize < 262_144 - 1024, "the catalogue is #{text.bytesize} bytes, too near the API's 256 KiB cap")
pin = JSON.parse(File.read('assets/fonts/fonts.source.json')).dig('custom_icons', 'sources', 'icons/catalogue.json')
check(pin == Digest::SHA256.hexdigest(text), "fonts.source.json pins #{pin}, not the catalogue's SHA-256")

puts "CATALOGUE_FIELDS_PASS entries=#{entries.size} deprecated=#{DEPRECATED.size}(replaced_by) fields=part,cells,background,foreground,line vocabulary=schema+README " \
     "generated=both_scripts bytes=#{text.bytesize} revision_pin=#{pin[0, 12]}"
