#!/usr/bin/env ruby
# Operator tooling. Not installed in the Debian package.
#
# Renderer-versus-state agreement against a LIVE Blit, over its API. The gates
# in tools/tests spawn their own server and so cannot be pointed at a host;
# this can, which is how #49 was found on the real Hibr desktop.
#
#   ruby tools/probes/host_render_agreement.rb HOST:PORT TOKEN [SURFACE] [TRIES]
#
# If SURFACE is omitted, the first surface in /v1/topology is used. The
# physical display is always discovered from that same topology; it is not
# assumed to be display-1.
#
# Issue #29, done-when 3: state the screensaver-trail report as pass or fail.
#
# Blank cells and undecorated full/half blocks have exactly known colours.
# Retained graphics are refused; styled glyphs, terminal cursor and a bounded
# pointer neighbourhood are excluded. Run with the pointer stationary. This
# comparison is only meaningful if the pixels and
# the state describe the same frame, which is what the capture headers are for:
# a sample whose presented revision moved under it is discarded, not failed.
require 'json'; require 'net/http'; require 'uri'
HOST = ARGV[0] || '192.168.10.54:8080'; TOKEN = ARGV[1] || ENV.fetch('BLIT_AUTH_TOKEN')
SURFACE_ARG = ARGV[2]
ATTEMPTS = Integer(ARGV[3] || '12')
BASE = URI("http://#{HOST}")
def req(m, p)
  u = BASE + p; r = { 'GET' => Net::HTTP::Get }.fetch(m).new(u)
  r['Authorization'] = "Bearer #{TOKEN}"
  Net::HTTP.start(u.hostname, u.port, read_timeout: 120) { |h| h.request(r) }
end
def json(p) = JSON.parse(req('GET', p).body)
topology = json('/v1/topology')
SURFACE = SURFACE_ARG || topology.fetch('panes').filter_map { |candidate|
  candidate['surface_id']
}.first
abort 'no live surface was reported by /v1/topology' unless SURFACE
DISPLAY = topology.fetch('outputs').first.fetch('display_id')
diag = json('/v1/diagnostics'); INSET = diag['render']['surface_inset_px']
DIRECT_KMS = diag.dig('render', 'direct_kms') == true
disp = json('/v1/displays').fetch('displays').find { |item| item['id'] == DISPLAY }
abort "display #{DISPLAY} is missing from /v1/displays" unless disp
W = disp['bounds']['width']; H = disp['bounds']['height']
pane = topology.fetch('panes').find { |candidate| candidate['surface_id'] == SURFACE }
abort "surface #{SURFACE} has no topology pane" unless pane
grid = pane.fetch('cell_grid')
CW = grid.fetch('cell_width')
CH = grid.fetch('cell_height')
# The topology's cell-grid origin is already in display coordinates. The
# pixel_region is a containing rectangle, not another offset to add.
ORIGIN_X = grid.fetch('origin_x') + INSET
ORIGIN_Y = grid.fetch('origin_y') + INSET
def capture
  r = req('GET', "/v1/diagnostics/screenshots/#{DISPLAY}")
  raise "capture #{r.code}" unless r.code == '200'
  revs = {}
  (r['X-Blit-Capture-Revisions'] || '').split(',').each { |p| i, v = p.split('='); revs[i] = v.to_i if i && v }
  { body: r.body, revs: revs, presented: r['X-Blit-Capture-Presented'], frame: r['X-Blit-Capture-Frame'] }
end
def px(b, x, y)
  bits = b.getbyte(28) | (b.getbyte(29) << 8)
  bytes = bits / 8
  raise "unsupported BMP depth #{bits}" unless [3, 4].include?(bytes)
  stride = (W * bytes + 3) & ~3; o = 54 + (H - 1 - y) * stride + x * bytes
  [b.getbyte(o + 2), b.getbyte(o + 1), b.getbyte(o)]
end
sessions = json('/v1/terminal-sessions').fetch('sessions')
session = sessions.find { |candidate| candidate['surface_id'] == SURFACE }
abort 'cell-only agreement cannot judge retained graphics' if session && session.fetch('graphics', []).any?
checked = 0; half_blocks = 0; checked_pixels = 0
mismatched = []; discarded = 0; used = nil
ATTEMPTS.times do |i|
  pointer = json('/v1/diagnostics').fetch('render').fetch('pointer')
  cap = capture
  state = json("/v1/surfaces/#{SURFACE}/cells")
  pointer_after = json('/v1/diagnostics').fetch('render').fetch('pointer')
  reported = cap[:revs][SURFACE]
  if reported.nil?
    abort "the capture reported no revision for #{SURFACE} (headers: #{cap[:revs].inspect})"
  end
  presented_ok = cap[:presented] == 'true' ||
                 (DIRECT_KMS && cap[:presented].nil?)
  if !presented_ok || reported != state['revision'] || pointer != pointer_after
    # The sample straddled a repaint, or those pixels never reached a screen.
    # Discard it rather than reporting the skew as a defect -- three earlier
    # attempts at this comparison on a live host produced three findings that
    # were all skew, which is what #29 was filed about.
    discarded += 1
    next
  end
  pointer_positions = pointer['valid'] ? [[pointer['x'],pointer['y']],
                                         [pointer['previous_x'],pointer['previous_y']]] : []
  grid = {}
  state['cells'].each { |c| grid[[c.fetch('x', 0), c.fetch('y', 0)]] = c }
  checked = 0; mismatched = []; half_blocks = 0; checked_pixels = 0
  cursor = state['cursor'] || {}
  grid.each do |(x, y), cell|
    glyph = cell['text'] || ' '
    next unless [' ', "\u2580", "\u2584", "\u2588"].include?(glyph)
    style = cell['style'] || {}
    next if style['background'].nil? || style['foreground'].nil?
    next if %w[bold dim italic underline blink inverse strike concealed].any? { |key| style[key] }
    next if cursor['visible'] && cursor['x'] == x && cursor['y'] == y
    left = ORIGIN_X + x * CW; top = ORIGIN_Y + y * CH
    next if pointer_positions.any? { |px, py| px && py &&
      left < px + 64 && left + CW > px - 64 && top < py + 64 && top + CH > py - 64 }
    colours = %w[foreground background].map do |key|
      hex = style.fetch(key).delete('#')
      [hex[0, 2].to_i(16), hex[2, 2].to_i(16), hex[4, 2].to_i(16)]
    end
    bad = 0
    (0...CH).each do |dy|
      ink = glyph == "\u2588" || (glyph == "\u2580" && dy < CH / 2) ||
            (glyph == "\u2584" && dy >= CH / 2)
      want = colours[ink ? 0 : 1]
      (0...CW).each do |dx|
        ax = ORIGIN_X + x * CW + dx; ay = ORIGIN_Y + y * CH + dy
        next if ax >= W || ay >= H
        bad += 1 if px(cap[:body], ax, ay) != want
        checked_pixels += 1
      end
    end
    checked += 1
    half_blocks += 1 if glyph != ' '
    mismatched << [x, y, bad] if bad.positive?
  end
  used = { frame: cap[:frame], presented: cap[:presented], direct_kms: DIRECT_KMS,
           revision: reported, attempt: i + 1 }
  break
end
if used.nil?
  puts "TRAIL_INCONCLUSIVE discarded=#{discarded}/#{ATTEMPTS} " \
       'every sample straddled a repaint or pointer motion; no stable capture'
  exit 2
end
puts "sample: attempt=#{used[:attempt]} frame=#{used[:frame]} presented=#{used[:presented]} " \
     "revision=#{used[:revision]} discarded=#{discarded}"
if mismatched.empty?
  abort 'cell-colour comparison was vacuous' if checked == 0
  puts "TRAIL_PASS checked_cells=#{checked} block_cells=#{half_blocks} pixels=#{checked_pixels} no_unaccounted_pixels=yes"
else
  puts "TRAIL_FAIL checked_cells=#{checked} with_unaccounted_pixels=#{mismatched.length} " \
       "worst=#{mismatched.max_by { |m| m[2] }.inspect} sample=#{mismatched.first(6).inspect}"
  exit 1
end
