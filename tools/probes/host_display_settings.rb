#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #69 on the live host. Three things cannot be measured in a gate:
#
#   1. whether SDL's published DRM master fd really yields a connector, and
#      what that connector's power state is. Locally the dummy video driver
#      publishes no fd at all, so `state.power` and `state.connector` are
#      absent by construction and the code path is never entered.
#   2. whether the rotated present works on a real renderer. The gate runs
#      against SDL's software renderer under the dummy driver; the host runs
#      opengles2 on KMSDRM, and SDL_RenderTextureRotated is not the same code
#      there.
#   3. whether a rotation survives the real desktop coming back -- the Hibr
#      session's grid comes from the display's rectangle, so a 1920x2160 panel
#      at 90 should hand it 135 columns and 240 rows instead of 240 and 135.
#
# Everything it touches is restored: rotation 0 and the desktop relaunched.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
DESKTOP = '/usr/share/blit/hibr/blit-session.hibr'

def call(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  req['Idempotency-Key'] = rand(1 << 30).to_s if %w[POST PATCH].include?(method)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 120) do |http|
    http.request(req)
  end
  [res.code.to_i, res.body.to_s]
end

def json(method, path, body = nil)
  code, text = call(method, path, body)
  [code, (text.empty? ? nil : (JSON.parse(text) rescue text))]
end

failures = []
def display
  json('GET', '/v1/displays').last.fetch('displays').first
end

# There is no route that lists sessions -- `/v1/diagnostics` reports the count
# and nothing else -- so a clean slate is an orderly restart rather than a
# sweep. That is not a workaround: it is also the only way to measure that a
# stored rotation is applied at boot on real hardware, which is what step 3
# uses it for.
def restart
  before = json('GET', '/v1/version').last.to_h['revision']
  code, = json('POST', '/v1/control/restart')
  abort "restart -> #{code}" unless code == 202
  sleep 2
  Timeout.timeout(60) do
    loop do
      begin
        break if json('GET', '/v1/version').first == 200 &&
                 json('GET', '/v1/diagnostics').last.to_h
                   .dig('resources', 'sessions').to_i.zero?
      rescue StandardError
        nil
      end
      sleep 1
    end
  end
  before
end

def desktop(label)
  code, created = json('POST', '/v1/terminal-sessions',
                       { 'command' => DESKTOP,
                         'arguments' => ['--session', "#{label}-#{rand(1 << 20)}",
                                         '--name', 'display-1'],
                         'display_id' => 'display-1',
                         'surface_mode' => 'terminal' })
  abort "#{label} desktop -> #{code} #{created.inspect}" unless code == 201
  sleep 6
  cells = json('GET', "/v1/surfaces/#{created.fetch('surface_id')}/cells").last
  printable = cells.fetch('cells').count { |c| (c['text'] || ' ') != ' ' }
  puts "#{label} desktop #{cells['width']}x#{cells['height']} " \
       "printable=#{printable}"
  [created, cells, printable]
end

# The capture is the panel's own size whatever the rotation, so the header is
# read rather than assumed.
def bmp_size(bmp)
  [bmp[18, 4].unpack1('l<'), bmp[22, 4].unpack1('l<')]
end

def pixel(bmp, width, height, x, y)
  stride = (width * 3 + 3) & ~3
  offset = 54 + (height - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

def ink_in(bmp, width, height, rect)
  left, top, right, bottom = rect
  count = 0
  (top...bottom).each do |y|
    (left...right).each do |x|
      next if x.negative? || y.negative? || x >= width || y >= height

      count += 1 unless pixel(bmp, width, height, x, y).zero?
    end
  end
  count
end

# Where the first cell lands on the panel, by rotation. Same arithmetic as
# tools/tests/display_settings_gate.rb, with the host's own panel and cell.
def first_cell_rect(rotation, inset, panel_w, panel_h, cell_w, cell_h)
  near_x = inset
  far_x = inset + cell_w
  near_y = inset
  far_y = inset + cell_h
  case rotation
  when 90 then [panel_w - far_y, near_x, panel_w - near_y, far_x]
  when 180 then [panel_w - far_x, panel_h - far_y, panel_w - near_x,
                 panel_h - near_y]
  when 270 then [near_y, panel_h - far_x, far_y, panel_h - near_x]
  else [near_x, near_y, far_x, far_y]
  end
end

item = display
render = json('GET', '/v1/diagnostics').last.fetch('render')
inset = render.fetch('surface_inset_px')
cell = json('GET', '/v1/fonts').last.fetch('active').fetch('cell_pixels')
cell_w = cell.fetch('width')
cell_h = cell.fetch('height')
panel_w = item.dig('state', 'physical', 'width')
panel_h = item.dig('state', 'physical', 'height')
puts "display #{item['id']} name=#{item['name']} " \
     "bounds=#{item.dig('bounds', 'width')}x#{item.dig('bounds', 'height')} " \
     "physical=#{panel_w}x#{panel_h} cell=#{cell_w}x#{cell_h} inset=#{inset}"
puts "capabilities #{item.fetch('capabilities').join(',')}"
puts "state power=#{item.dig('state', 'power').inspect} " \
     "connector=#{item.dig('state', 'connector').inspect} " \
     "backlight=#{item.dig('state', 'backlight').inspect}"
puts "configuration #{item['configuration'].inspect}"

# 1. The connector was matched through SDL's published master fd, and its
#    power state read. This is the whole of what 0.1.456 claims about power.
failures << "no connector matched: #{item['state'].inspect}" if
  item.dig('state', 'connector').nil?
failures << "connector #{item.dig('state', 'connector').inspect} is not the " \
            "display's name #{item['name'].inspect}" unless
  item.dig('state', 'connector') == item['name']
failures << "no power state read: #{item['state'].inspect}" if
  item.dig('state', 'power').nil?
failures << 'power must not be claimed as a capability until it can be set' if
  item.fetch('capabilities').include?('power')
failures << 'this host has no backlight, so brightness must be absent' if
  item.fetch('capabilities').include?('brightness')

# 2. The rotated present on the real renderer, all four rotations, each one
#    asserted in the corner the contract names and in none of the others.
restart
counts = {}
[0, 90, 180, 270].each do |rotation|
  code, updated = json('PATCH', '/v1/displays/display-1',
                       { 'configuration' => { 'rotation' => rotation } })
  failures << "rotate to #{rotation} -> #{code} #{updated.inspect}" unless code == 200
  next unless code == 200

  want_w = rotation == 90 || rotation == 270 ? panel_h : panel_w
  failures << "bounds at #{rotation}: #{updated.dig('bounds', 'width')}" unless
    updated.dig('bounds', 'width') == want_w
  code, created = json('POST', '/v1/terminal-sessions',
                       { 'command' => '/bin/sh',
                         'arguments' => ['-c', "printf '\\342\\226\\210'; sleep 90"],
                         'display_id' => 'display-1',
                         'surface' => { 'width' => 20, 'height' => 5 } })
  failures << "probe session at #{rotation} -> #{code}" unless code == 201
  next unless code == 201

  sleep 3
  code, bmp = call('GET', '/v1/diagnostics/screenshots/display-1?cached=1')
  failures << "screenshot at #{rotation} -> #{code}" unless code == 200
  if code == 200
    width, height = bmp_size(bmp)
    failures << "capture at #{rotation} is #{width}x#{height}, not the " \
                "panel's #{panel_w}x#{panel_h}" unless
      width == panel_w && height == panel_h
    counts[rotation] = [0, 90, 180, 270].to_h do |candidate|
      [candidate,
       ink_in(bmp, width, height,
              first_cell_rect(candidate, inset, panel_w, panel_h, cell_w,
                              cell_h))]
    end
  end
  json('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
  sleep 2
end
counts.each do |rotation, lit|
  puts "  #{rotation}: #{lit.map { |k, v| "#{k}=#{v}" }.join(' ')}"
  failures << "at #{rotation} the first cell has #{lit[rotation]} lit pixels " \
              "where it should have #{cell_w * cell_h}" unless
    lit[rotation] == cell_w * cell_h
  (lit.keys - [rotation]).each do |elsewhere|
    failures << "at #{rotation} there are #{lit[elsewhere]} lit pixels in " \
                "the #{elsewhere} corner too" unless lit[elsewhere].zero?
  end
end

# 3. A stored rotation is applied at boot on real hardware, and a real desktop
#    on the rotated panel is handed the rotated grid.
json('PATCH', '/v1/displays/display-1',
     { 'configuration' => { 'rotation' => 90 } })
sleep 1.5
restart
booted = display
puts "after a restart bounds=#{booted.dig('bounds', 'width')}x" \
     "#{booted.dig('bounds', 'height')} " \
     "configuration=#{booted['configuration'].inspect}"
failures << "a stored rotation did not survive the restart: " \
            "#{booted['configuration'].inspect}" unless
  booted.dig('configuration', 'rotation') == 90
failures << "bounds after the restart: #{booted['bounds'].inspect}" unless
  booted.dig('bounds', 'width') == panel_h

created, cells, printable = desktop('rotated')
failures << "a rotated desktop should be #{panel_h / cell_w} columns, not " \
            "#{cells['width']}" unless cells['width'] == panel_h / cell_w
failures << "a rotated desktop should be #{panel_w / cell_h} rows, not " \
            "#{cells['height']}" unless cells['height'] == panel_w / cell_h
failures << 'a rotated desktop painted nothing' unless printable.positive?

# 4. Restore: the desktop goes, rotation returns to 0, the stored entry is
#    removed rather than written as a zero, and a fresh desktop is upright.
json('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
sleep 2
code, restored = json('PATCH', '/v1/displays/display-1',
                      { 'configuration' => { 'rotation' => 0 } })
failures << "restore rotation -> #{code} #{restored.inspect}" unless code == 200
failures << "bounds not restored: #{restored.to_h['bounds'].inspect}" unless
  restored.to_h.dig('bounds', 'width') == panel_w
sleep 1.5
stored = json('GET', '/v1/settings').last.to_h.dig('document', 'displays')
failures << "the stored entry should be gone: #{stored.inspect}" unless
  stored.to_h[item['name']].nil?
_, cells, printable = desktop('upright')
failures << "the restored desktop is #{cells['width']}x#{cells['height']}" unless
  cells['width'] == panel_w / cell_w && cells['height'] == panel_h / cell_h
failures << 'the restored desktop painted nothing' unless printable.positive?

diagnostics = json('GET', '/v1/diagnostics').last
puts "errors=#{diagnostics.fetch('errors').inspect} " \
     "warnings=#{diagnostics.fetch('warnings').inspect}"

if failures.empty?
  puts 'LIVE_DISPLAY_PASS connector=matched power=read rotation=presented ' \
       'boot=stored_rotation_applied desktop=rotated_grid restored=yes'
else
  failures.each { |f| warn "live display: #{f}" }
  exit 1
end
