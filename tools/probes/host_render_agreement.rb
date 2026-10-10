#!/usr/bin/env ruby
# Operator tooling. Not installed in the Debian package.
#
# Renderer-versus-state agreement against a LIVE Blit, over its API. The gates
# in tools/tests spawn their own server and so cannot be pointed at a host;
# this can, which is how #49 was found on the real Hibr desktop.
#
#   ruby tools/probes/host_render_agreement.rb HOST:PORT TOKEN [SURFACE] [TRIES]
#
# Issue #29, done-when 3: state the screensaver-trail report as pass or fail.
#
# A blank cell must render as a solid block of its background colour; anything
# else in that block is a pixel the cell state does not account for, which is
# what a trail looks like. That comparison is only meaningful if the pixels and
# the state describe the same frame, which is what the capture headers are for:
# a sample whose presented revision moved under it is discarded, not failed.
require 'json'; require 'net/http'; require 'uri'
HOST = ARGV[0] || '192.168.10.54:8080'; TOKEN = ARGV[1] || 'hellomonkeyboy'
SURFACE = ARGV[2] || 'surface-6'
ATTEMPTS = Integer(ARGV[3] || '12')
BASE = URI("http://#{HOST}"); CW = 8; CH = 16
def req(m, p)
  u = BASE + p; r = { 'GET' => Net::HTTP::Get }.fetch(m).new(u)
  r['Authorization'] = "Bearer #{TOKEN}"
  Net::HTTP.start(u.hostname, u.port, read_timeout: 120) { |h| h.request(r) }
end
def json(p) = JSON.parse(req('GET', p).body)
diag = json('/v1/diagnostics'); INSET = diag['render']['surface_inset_px']
disp = json('/v1/displays')['displays'][0]
W = disp['bounds']['width']; H = disp['bounds']['height']
def capture
  r = req('GET', "/v1/diagnostics/screenshots/#{'display-1'}")
  raise "capture #{r.code}" unless r.code == '200'
  revs = {}
  (r['X-Blit-Capture-Revisions'] || '').split(',').each { |p| i, v = p.split('='); revs[i] = v.to_i if i && v }
  { body: r.body, revs: revs, presented: r['X-Blit-Capture-Presented'], frame: r['X-Blit-Capture-Frame'] }
end
def px(b, x, y)
  stride = (W * 3 + 3) & ~3; o = 54 + (H - 1 - y) * stride + x * 3
  [b.getbyte(o + 2), b.getbyte(o + 1), b.getbyte(o)]
end
checked = 0; mismatched = []; discarded = 0; used = nil
ATTEMPTS.times do |i|
  cap = capture
  state = json("/v1/surfaces/#{SURFACE}/cells")
  reported = cap[:revs][SURFACE]
  if reported.nil?
    abort "the capture reported no revision for #{SURFACE} (headers: #{cap[:revs].inspect})"
  end
  if cap[:presented] != 'true' || reported != state['revision']
    # The sample straddled a repaint, or those pixels never reached a screen.
    # Discard it rather than reporting the skew as a defect -- three earlier
    # attempts at this comparison on a live host produced three findings that
    # were all skew, which is what #29 was filed about.
    discarded += 1
    next
  end
  grid = {}
  state['cells'].each { |c| grid[[c.fetch('x', 0), c.fetch('y', 0)]] = c }
  checked = 0; mismatched = []
  grid.each do |(x, y), cell|
    next unless (cell['text'] || ' ') == ' '
    next if (cell['style'] || {})['background'].nil?
    hex = cell['style']['background'].delete('#')
    want = [hex[0, 2].to_i(16), hex[2, 2].to_i(16), hex[4, 2].to_i(16)]
    bad = 0
    (0...CH).each do |dy|
      (0...CW).each do |dx|
        ax = INSET + x * CW + dx; ay = INSET + y * CH + dy
        next if ax >= W || ay >= H
        bad += 1 if px(cap[:body], ax, ay) != want
      end
    end
    checked += 1
    mismatched << [x, y, bad] if bad.positive?
  end
  used = { frame: cap[:frame], presented: cap[:presented], revision: reported, attempt: i + 1 }
  break
end
if used.nil?
  puts "TRAIL_INCONCLUSIVE discarded=#{discarded}/#{ATTEMPTS} " \
       'every sample straddled a repaint; the surface is mutating faster than a 12MB capture'
  exit 2
end
puts "sample: attempt=#{used[:attempt]} frame=#{used[:frame]} presented=#{used[:presented]} " \
     "revision=#{used[:revision]} discarded=#{discarded}"
if mismatched.empty?
  puts "TRAIL_PASS blank_cells=#{checked} no_unaccounted_pixels=yes"
else
  puts "TRAIL_FAIL blank_cells=#{checked} with_unaccounted_pixels=#{mismatched.length} " \
       "worst=#{mismatched.max_by { |m| m[2] }.inspect} sample=#{mismatched.first(6).inspect}"
  exit 1
end
