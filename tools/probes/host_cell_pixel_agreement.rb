#!/usr/bin/env ruby
# frozen_string_literal: true

# The trail-class invariant, measured on the live host: what the cell snapshot
# says and what the panel paints have to agree.
#
# Issue #72 was found by eye and then mismeasured three times before it was
# measured properly. Each mistake produced a confident, wrong number, so the
# corrections are the point of this file:
#
#   1. **Sample every pixel of a cell, not one.** A single probe pixel per cell
#      reported `painted=0` on a screen a full scan saw as 1536 cells. Glyph
#      content is thin; a cell is painted if *any* of its 8x16 pixels is.
#   2. **Bracket the capture with two cell reads.** They are separate requests,
#      so on a surface that is actively drawing every sample shows some cells
#      stale and about as many missing -- the signature of the sampling, not of
#      the renderer. Only a cell blank in *both* reads yet painted is stale.
#      `churn` reports how much moved between the two, so a reading taken
#      during heavy change is visible as such rather than silently noisy.
#   3. **Exclude the software pointer.** It is drawn after the scene, every
#      frame, and is not a cell -- so it is legitimately painted where the cell
#      data says blank. Three "stale" cells that survived a full redraw, a
#      fresh capture and deleting every surface turned out to be the mouse
#      arrow.
#
# A real trail looks nothing like any of those: stale greatly exceeds missing
# and grows. The original measurement was 8407 stale against 37 missing,
# climbing at about 96 cells a second.

require 'json'
require 'net/http'
require 'set'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
SAMPLES = Integer(ENV.fetch('BLIT_SAMPLES', '6'), 10)
INTERVAL = Float(ENV.fetch('BLIT_INTERVAL', '8'))
CELL_W = 8
CELL_H = 16

def get(path, raw: false)
  uri = BASE + path
  req = Net::HTTP::Get.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 90) { |h| h.request(req) }
  abort("GET #{path} -> #{res.code}") unless res.code == '200'
  raw ? res.body : JSON.parse(res.body)
end

def drawn_set(doc)
  out = Set.new
  doc.fetch('cells').each do |c|
    style = c['style'] || {}
    if (c['text'] || ' ') != ' ' || style['background'].to_s != '#000000'
      out << [c['x'], c['y']]
    end
  end
  cursor = doc['cursor'] || {}
  [out, (cursor['visible'] ? [cursor['x'], cursor['y']] : nil)]
end

# Every pixel of every cell. The inset is where the surface starts on the panel.
def painted_set(bmp, inset, columns, rows)
  width = bmp[18, 4].unpack1('l<')
  height = bmp[22, 4].unpack1('l<')
  stride = (width * 3 + 3) & ~3
  lit = Set.new
  (0...rows).each do |cy|
    (0...columns).each do |cx|
      found = false
      (0...CELL_H).each do |dy|
        y = inset + cy * CELL_H + dy
        next if y >= height

        (0...CELL_W).each do |dx|
          x = inset + cx * CELL_W + dx
          next if x >= width

          offset = 54 + (height - 1 - y) * stride + x * 3
          next if offset.negative? || offset + 2 >= bmp.bytesize

          if bmp.getbyte(offset).to_i + bmp.getbyte(offset + 1).to_i +
             bmp.getbyte(offset + 2).to_i > 0
            found = true
            break
          end
        end
        break if found
      end
      lit << [cx, cy] if found
    end
  end
  lit
end

def pointer_cells
  pointer = get('/v1/diagnostics').fetch('render').fetch('pointer')
  return Set.new unless pointer['valid']

  cells = Set.new
  [[pointer['x'], pointer['y']],
   [pointer['previous_x'], pointer['previous_y']]].each do |x, y|
    cx = ((x - 4) / CELL_W).floor
    cy = ((y - 4) / CELL_H).floor
    (0..2).each { |dx| (0..2).each { |dy| cells << [cx + dx, cy + dy] } }
  end
  cells
end

surface = ARGV[0] || abort('usage: host_cell_pixel_agreement.rb SURFACE_ID')
inset = get('/v1/diagnostics').fetch('render').fetch('surface_inset_px')
failures = []
worst = 0
SAMPLES.times do |sample|
  before, cursor_a = drawn_set(get("/v1/surfaces/#{surface}/cells"))
  bmp = get('/v1/diagnostics/screenshots/display-1?cached=1', raw: true)
  after, cursor_b = drawn_set(get("/v1/surfaces/#{surface}/cells"))
  geometry = get("/v1/surfaces/#{surface}/summary")
  lit = painted_set(bmp, inset, geometry.fetch('width'), geometry.fetch('height'))

  excluded = pointer_cells
  excluded << cursor_a if cursor_a
  excluded << cursor_b if cursor_b
  stale = lit - (before | after) - excluded
  missing = (before & after) - lit - excluded
  worst = [worst, stale.size].max
  puts format('%2d drawn=%-6d painted=%-6d stale=%-5d missing=%-5d churn=%d',
              sample + 1, before.size, lit.size, stale.size, missing.size,
              (before ^ after).size)
  if stale.any?
    xs = stale.map(&:first)
    ys = stale.map(&:last)
    puts format('   stale box x %d..%d y %d..%d e.g. %s',
                xs.min, xs.max, ys.min, ys.max, stale.to_a.sort[0, 6].inspect)
  end
  failures << "sample #{sample + 1}: #{stale.size} stale" if stale.size > 8
  sleep INTERVAL unless sample == SAMPLES - 1
end

if failures.empty?
  puts "LIVE_CELL_PIXEL_PASS samples=#{SAMPLES} worst_stale=#{worst}"
else
  failures.each { |f| warn "cell/pixel: #{f}" }
  exit 1
end
