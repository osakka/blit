#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #23 / ADR 0062. A wide character must reach the framebuffer as itself.
#
# The engine already got the state right -- the width table gives a CJK
# ideograph two cells and the API reports width 2 -- while the pixels showed
# the '?' substitute, because the primary font is 8x16 and has no CJK. That
# combination is the dangerous one: every API-level check passed.
#
# So this asserts both halves, and asserts them against each other: each
# sample must occupy two cells in the canonical state, and must render as a
# shape distinct from '?' and from the other samples. Four identical shapes is
# exactly what substitution looks like.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

ROOT = File.expand_path('../..', __dir__)
PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '19262'), 10)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
WIDE_FONT = ENV.fetch('BLIT_GATE_WIDE_FONT',
                      File.join(ROOT, 'assets/fonts/Blit-Hibr-Unifont16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
FB_WIDTH = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_HEIGHT = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)

# Single-quoted deliberately: this must reach the shell's printf as a
# backslash escape. A double-quoted Ruby string would turn it into a real ESC
# byte, JSON.dump would encode that as \u001b, and the server's argument
# parser drops the backslash and hands the child the literal text `u001b`
# (see the json_string_array issue). The cursor has to be hidden because it is
# a filled block that would land beside the '?' reference span.
HIDE_CURSOR = '\033[?25l'
SAMPLES = [
  ['CJK ideograph', "中"],
  ['Hiragana', "あ"],
  ['Hangul syllable', "한"],
  ['fullwidth form', "Ａ"]
].freeze

abort("wide-glyph gate: missing #{BINARY}") unless File.executable?(BINARY)
abort("wide-glyph gate: missing #{WIDE_FONT}") unless File.file?(WIDE_FONT)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "wide-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("wide-glyph gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

def pixel(bmp, x, y)
  stride = (FB_WIDTH * 3 + 3) & ~3
  offset = 54 + (FB_HEIGHT - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

# The lit pixels of a two-cell span, as a comparable shape.
def shape(bmp, inset, column)
  lit = []
  (0...CELL_H).each do |dy|
    (0...(CELL_W * 2)).each do |dx|
      lit << [dx, dy] if pixel(bmp, inset + column * CELL_W + dx, inset + dy) != 0
    end
  end
  lit
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                 'BLIT_PSF_WIDE_FONT' => WIDE_FONT,
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

inset = json('GET', '/v1/diagnostics').fetch('render').fetch('surface_inset_px')
failures = []

# Each sample at a known column, two cells apart, then '?' for comparison.
text = SAMPLES.map { |_, glyph| glyph }.join + '?'
created = json('POST', '/v1/terminal-sessions',
               'command' => '/bin/sh',
               'arguments' => ['-c', "printf '#{HIDE_CURSOR}%s' '#{text}'; " \
                                     'sleep 60'],
               'display_id' => 'display-1',
               'surface' => { 'width' => 30, 'height' => 4 })
sleep 2.0
state = json('GET', "/v1/surfaces/#{created.fetch('surface_id')}/cells")
capture = request('GET', '/v1/diagnostics/screenshots/display-1')
abort("wide-glyph gate: screenshot #{capture.code}") unless capture.code == '200'
request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")

grid = {}
state.fetch('cells').each { |c| grid[[c.fetch('x', 0), c.fetch('y', 0)]] = c }
if ENV['BLIT_GATE_DEBUG']
  warn '  row 0: ' + state.fetch('cells').select { |c| c.fetch('y', 0) == 0 }
                          .sort_by { |c| c.fetch('x', 0) }
                          .map { |c| "x#{c['x']}:w#{c['width']}:#{c['text'].inspect}" }
                          .join(' ')
end

shapes = {}
SAMPLES.each_with_index do |(name, glyph), index|
  column = index * 2
  cell = grid[[column, 0]]
  # The state half: two cells, and the codepoint the application sent.
  if cell.nil?
    failures << "#{name}: no cell at column #{column}"
    next
  end
  if cell['width'] != 2
    failures << "#{name}: the state says width #{cell['width'].inspect}, not 2"
  end
  if cell['text'] != glyph
    failures << "#{name}: the state holds #{cell['text'].inspect}, not " \
                "#{glyph.inspect}"
  end
  shapes[name] = shape(capture.body, inset, column)
  failures << "#{name}: drew nothing" if shapes[name].empty?
end

substitute = shape(capture.body, inset, SAMPLES.length * 2)
failures << 'the "?" reference drew nothing' if substitute.empty?

# The pixel half. A substituted glyph is the '?' shape, and all four
# substituted glyphs are identical to each other.
shapes.each do |name, lit|
  next if lit.empty?

  if lit == substitute
    failures << "#{name} renders as the same shape as \"?\", so it was " \
                'substituted rather than drawn'
  end
end
pairs = shapes.keys.combination(2).reject { |a, b| shapes[a].empty? || shapes[b].empty? }
pairs.each do |a, b|
  next unless shapes[a] == shapes[b]

  failures << "#{a} and #{b} render as the same shape"
end

unless failures.empty?
  failures.each { |f| warn "wide-glyph gate: #{f}" }
  exit 1
end

widths = shapes.transform_values { |lit| lit.map(&:first).max.to_i + 1 }
puts "WIDE_GLYPH_PASS samples=#{shapes.length} " \
     "distinct_shapes=#{shapes.values.uniq.length} " \
     "substitute_px=#{substitute.length} " \
     "span_px=#{widths.values.inspect}"
