#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #24. Two different files have been named Blit-Hibr-Terminus16.psf: the
# checked-in 929-glyph artifact and a stale 540-glyph copy in the build tree.
# The package ships the right one, so the divergence only ever appeared in
# local runs -- in the direction that hides a regression, because the missing
# glyphs render as '?' exactly where a coverage fault would.
#
# This asserts three things about whatever font the server actually loaded:
# that its identity matches the checked-in artifact, that the codepoints
# ADR 0020 exists to guarantee resolve to glyphs of their own rather than to
# the '?' substitute, and that those glyphs reach the framebuffer.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19261'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
ASSET = File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf')
WIDE_ASSET = File.join(ROOT, 'assets/fonts/Blit-Hibr-Unifont16.psf')
FONT = ENV.fetch('BLIT_GATE_FONT', ASSET)
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)

abort("font-identity gate: missing #{BINARY}") unless File.executable?(BINARY)
abort("font-identity gate: missing #{ASSET}") unless File.file?(ASSET)

# PSF2: magic, version, headersize, flags, length, charsize, height, width.
def psf2_identity(path)
  data = File.binread(path)
  magic = data[0, 4].unpack('C4')
  raise "#{path} is not PSF2 (magic #{magic.inspect})" unless
    magic == [0x72, 0xb5, 0x4a, 0x86]

  _version, header_size, _flags, length, charsize, height, width =
    data[4, 28].unpack('L<7')
  mapped = {}
  table_start = header_size + length * charsize
  if data.length > table_start
    # The Unicode table follows the bitmaps: one entry per glyph, terminated
    # by 0xFF. Within an entry, 0xFE separates the list of single codepoints
    # from any multi-codepoint sequences; only the singles are wanted here.
    glyph = 0
    entry = []
    data[table_start..].each_byte do |byte|
      if byte == 0xff
        singles = entry.take_while { |b| b != 0xfe }
        singles.pack('C*').force_encoding('UTF-8').each_char do |ch|
          mapped[ch.ord] = glyph if ch.valid_encoding?
        end
        glyph += 1
        entry = []
      else
        entry << byte
      end
    end
  end
  { glyphs: length, width: width, height: height, mapped: mapped }
end

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "font-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("font-identity gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

def pixel(bmp, x, y)
  stride = (FB_WIDTH * 3 + 3) & ~3
  offset = 54 + (FB_HEIGHT - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

def lit_cell(bmp, inset, column)
  lit = []
  (0...CELL_H).each do |dy|
    (0...CELL_W).each do |dx|
      lit << [dx, dy] if pixel(bmp, inset + column * CELL_W + dx, inset + dy) != 0
    end
  end
  lit
end

expected = psf2_identity(ASSET)
failures = []

# 0. Nothing in the build tree may share a name with a checked-in font while
# holding different bytes. That is the whole mechanism of this issue: the
# package overwrites bundle fonts from assets/, so assets/ is authoritative,
# and a same-named file that disagrees is a trap for anyone who runs out of
# the build tree or follows a stale invocation.
assets = Dir.glob(File.join(ROOT, 'assets/fonts/*.psf')).to_h do |path|
  [File.basename(path), File.binread(path)]
end
Dir.glob(File.join(ROOT, 'build/**/*.psf')).each do |path|
  authoritative = assets[File.basename(path)]
  next if authoritative.nil?
  next if File.binread(path) == authoritative

  failures << "#{path.sub("#{ROOT}/", '')} shares its name with " \
              "assets/fonts/#{File.basename(path)} but holds different bytes " \
              "(#{psf2_identity(path)[:glyphs]} glyphs against " \
              "#{expected[:glyphs]}). Refresh it from assets/fonts or delete " \
              'it; a run that loads it renders covered codepoints as "?"'
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

active = json('GET', '/v1/fonts').fetch('active')
inset = json('GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')

# 1. The loaded font is the checked-in artifact, by its own count of bitmaps.
if active.fetch('glyphs', nil) != expected[:glyphs]
  failures << "the server loaded a font with #{active.fetch('glyphs', 'no')} " \
              "glyphs; #{File.basename(ASSET)} has #{expected[:glyphs]}. " \
              "Active path: #{active['path']}"
end
if active.dig('cell_pixels', 'width') != expected[:width] ||
   active.dig('cell_pixels', 'height') != expected[:height]
  failures << "cell size #{active['cell_pixels'].inspect} does not match the " \
              "artifact's #{expected[:width]}x#{expected[:height]}"
end

# 2. The codepoints ADR 0020 names must be in the font, not substituted. A
# stale font maps neither and both render as '?'.
GUARANTEED = { 0x2593 => 'DARK SHADE', 0xFF76 => 'HALFWIDTH KATAKANA KA' }.freeze
GUARANTEED.each do |codepoint, name|
  next if expected[:mapped].key?(codepoint)

  failures << "#{File.basename(ASSET)} does not map U+%04X (%s), which " \
              'ADR 0020 exists to guarantee' % [codepoint, name]
end

# 3. And they must reach the framebuffer as themselves rather than as '?'.
text = GUARANTEED.keys.map { |c| [c].pack('U') }.join + '?'
created = json('POST', '/v1/terminal-sessions',
               'command' => '/bin/sh',
               'arguments' => ['-c', "printf '%s' '#{text}'; sleep 60"],
               'display_id' => 'display-1',
               'surface' => { 'width' => 20, 'height' => 4 })
sleep 2.0
capture = request('GET', '/v1/diagnostics/screenshots/display-1')
abort("font-identity gate: screenshot #{capture.code}") unless capture.code == '200'
shapes = (0..GUARANTEED.length).map { |column| lit_cell(capture.body, inset, column) }
request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")

substitute = shapes.last
GUARANTEED.keys.each_with_index do |codepoint, index|
  shape = shapes[index]
  if shape.empty?
    failures << 'U+%04X drew nothing' % codepoint
  elsif shape == substitute
    failures << 'U+%04X renders as the same shape as "?", so the font ' \
                'substituted it' % codepoint
  end
end

# 4. The wide font (ADR 0062) is a second artifact with its own identity. A
# stale or absent one returns wide cells to the '?' substitute, which is the
# condition #23 was about, so it is asserted here rather than left to the
# wide-glyph gate alone.
if File.file?(WIDE_ASSET)
  wide = psf2_identity(WIDE_ASSET)
  if wide[:width] != 16 || wide[:height] != 16
    failures << "the wide font is #{wide[:width]}x#{wide[:height]}; a wide " \
                'cell is two 8px columns and needs 16x16'
  end
  { 0x4E2D => 'CJK ideograph', 0x3042 => 'HIRAGANA A',
    0xD55C => 'HANGUL SYLLABLE HAN', 0xFF21 => 'FULLWIDTH A' }.each do |cp, name|
    next if wide[:mapped].key?(cp)

    failures << ('the wide font does not map U+%04X (%s)' % [cp, name])
  end
  if wide[:glyphs] < 40_000
    failures << "the wide font has only #{wide[:glyphs]} glyphs; the wide BMP " \
                'blocks need about fifty thousand'
  end
else
  failures << "#{WIDE_ASSET.sub("#{ROOT}/", '')} is missing, so wide cells " \
              'fall back to the primary font and render as "?"'
end

unless failures.empty?
  failures.each { |f| warn "font-identity gate: #{f}" }
  exit 1
end

puts "FONT_IDENTITY_PASS glyphs=#{active['glyphs']} " \
     "mappings=#{active['mappings']} cell=#{expected[:width]}x#{expected[:height]} " \
     "guaranteed_codepoints=#{GUARANTEED.length} " \
     "wide=#{File.file?(WIDE_ASSET) ? psf2_identity(WIDE_ASSET)[:glyphs] : 0}_glyphs_16x16"
