#!/usr/bin/env ruby
require 'json'
require 'open3'
root = File.expand_path('../..', __dir__)
library = ENV.fetch('BLIT_GATE_HARFBUZZ', '/lib/x86_64-linux-gnu/libharfbuzz.so.0')
Dir.chdir(root) do
  command = ['python3', 'tools/fonts/audit-emoji.py', '--harfbuzz', library, '--require-complete']
  out, err, status = Open3.capture3(*command)
  raise "colour font audit failed: #{err}" unless status.success?
  report = JSON.parse(out)
  raise 'audit scope mislabelled' unless report.fetch('scope') == 'font_shaping_only_not_colour_raster_or_terminal_acceptance'
  raise 'RGI coverage gap' unless report.fetch('rgi_failures').zero? && report.fetch('failures').empty?
  raise 'wrong audit font' unless report.fetch('font_sha256') == JSON.parse(File.read('assets/fonts/modern-fonts.source.json')).fetch('colour').fetch('sha256')
  # An ordinary text font must fail the same complete-emoji requirement.
  out, _err, status = Open3.capture3(*command, '--font', 'assets/fonts/BlitModernNerd-Regular.ttf')
  raise 'missing-glyph negative fixture passed' if status.success?
  negative = JSON.parse(out)
  raise 'negative fixture did not expose missing/split emoji' unless negative.fetch('rgi_failures') > 0
  corpus = JSON.parse(File.read('assets/unicode/emoji-18.0/source.json')).fetch('entries')
  rgi = corpus.fetch('fully-qualified') + corpus.fetch('component')
  puts "EMOJI_FONT_AUDIT_PASS corpus=#{corpus.values.sum} rgi=#{rgi} shaper=#{report.fetch('harfbuzz_version')} negative=text_font_rejected scope=font_only_not_renderer"
end
