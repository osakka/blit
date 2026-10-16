#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #69, epic #64 stage 5. A display's `configuration` was `{}`, and ADR
# 0064 recorded that as a decision rather than an omission: a mode change
# resizes every surface, and a Hibr desktop's cells come from its owner, so it
# needs issue #60's resize protocol first. What is left is the settings that
# resize nothing, and they divide by who is permitted to perform them.
#
# Measured on the live host before any of this was designed:
#
#   /sys/class/drm/card0-DP-1/dpms  mode 444, root write -> EACCES
#   one fd on /dev/dri/card0, held by blit (SDL has DRM master)
#   /sys/class/backlight            exists, empty
#
# So brightness has no hardware to act on there, and power cannot go through
# sysfs on any backend. Rotation is the one that is Blit's own -- a render
# transform, needing no DRM master -- and it is the one this gate is mostly
# about.
#
# The assertions that matter are the ones a self-consistent server would still
# fail:
#
#   * `capabilities` is derived from the backend and the hardware. Fabricate a
#     backlight and `brightness` appears; take it away and it does not.
#   * a rotation exchanges the display's rectangle but not its mode, and the
#     cell grid a new session receives follows the rectangle
#   * the rotation reaches the panel. A glyph drawn at logical (0, 0) is read
#     back out of a screenshot at the physical top-right, which is the one
#     assertion a server that stored the number and presented unrotated cannot
#     pass.
#   * it is refused while a surface is present, because that surface's owner
#     is the one that decides its size (ADR 0063)
#   * it survives a restart, and returning it to 0 removes the stored entry
#     instead of leaving it behind

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19392'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
# The dummy video driver's single display, and the cell box of the gate font.
PANEL_W = 1024
PANEL_H = 768
CELL_W = 8
CELL_H = 16

abort("display gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "display-#{rand(1 << 30)}" if %w[POST PATCH].include?(method)
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) do |http|
    http.request(req)
  end
  [response.code.to_i, (response.body.to_s.empty? ? nil : response.body)]
end

def json(method, path, body = nil)
  code, text = request(method, path, body)
  [code, (text.nil? ? nil : (JSON.parse(text) rescue text))]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "display gate: FAIL #{message}"
end

def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

$server = nil
def start(env)
  $server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                    'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') }
                    .merge(env),
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').first == 200
      rescue StandardError
        nil
      end
      sleep 0.2
    end
  end
end

def stop
  return if $server.nil?

  Process.kill('TERM', $server) rescue nil
  Process.wait($server) rescue nil
  $server = nil
end
at_exit { stop }

def display
  json('GET', '/v1/displays').last.to_h.fetch('displays', []).first.to_h
end

def patch(configuration, headers = {})
  json('PATCH', '/v1/displays/display-1', { 'configuration' => configuration })
end

def session(script, body = {})
  code, created = json('POST', '/v1/terminal-sessions',
                       { 'command' => '/bin/sh', 'arguments' => ['-c', script],
                         'display_id' => 'display-1' }.merge(body))
  abort("display gate: session create -> #{code} #{created.inspect}") unless code == 201
  created
end

# A BMP pixel, bottom-up, 24-bit. The capture is always the panel's own size:
# rotation changes what is in it, not how big it is.
def pixel(bmp, x, y)
  stride = (PANEL_W * 3 + 3) & ~3
  offset = 54 + (PANEL_H - 1 - y) * stride + x * 3
  (bmp.getbyte(offset + 2) << 16) | (bmp.getbyte(offset + 1) << 8) |
    bmp.getbyte(offset)
end

def screenshot
  code, body = request('GET', '/v1/diagnostics/screenshots/display-1?cached=1')
  abort("display gate: screenshot -> #{code}") unless code == 200
  body
end

# The ink inside a rectangle of the capture. A full block glyph fills its
# whole cell, so the count is the cell's area less the sampling step.
def ink_in(bmp, left, top, right, bottom)
  count = 0
  (top...bottom).each do |y|
    (left...right).each do |x|
      next if x.negative? || y.negative? || x >= PANEL_W || y >= PANEL_H

      count += 1 unless pixel(bmp, x, y).zero?
    end
  end
  count
end

# Where the first cell of a surface lands on the panel, by rotation. The
# surface is inset by INSET and a cell is CELL_W x CELL_H, so at 0 the cell
# occupies logical (4, 4)..(12, 20); the rest is that rectangle under the
# transform, written out rather than computed so that a change of convention
# has to be made here on purpose.
#
#   90   x = panel_width  - logical_y,  y = logical_x
#   180  x = panel_width  - logical_x,  y = panel_height - logical_y
#   270  x = logical_y,                 y = panel_height - logical_x
def first_cell_rect(rotation, inset)
  near_x = inset
  far_x = inset + CELL_W
  near_y = inset
  far_y = inset + CELL_H
  case rotation
  when 90 then [PANEL_W - far_y, near_x, PANEL_W - near_y, far_x]
  when 180 then [PANEL_W - far_x, PANEL_H - far_y, PANEL_W - near_x,
                 PANEL_H - near_y]
  when 270 then [near_y, PANEL_H - far_x, far_y, PANEL_H - near_x]
  else [near_x, near_y, far_x, far_y]
  end
end

Dir.mktmpdir('blit-display') do |root|
  settings = File.join(root, 'settings.json')
  backlight = File.join(root, 'backlight')
  Dir.mkdir(backlight)
  Dir.mkdir(File.join(backlight, 'intel_backlight'))
  # 937 steps, so the percentage conversion is in its "more raw values than
  # percentages" class -- the class where percent -> raw -> percent is exact
  # and the reverse cannot be, which blit_audio_control.h already pins.
  File.write(File.join(backlight, 'intel_backlight', 'max_brightness'), "937\n")
  File.write(File.join(backlight, 'intel_backlight', 'brightness'), "468\n")
  File.write(File.join(backlight, 'intel_backlight', 'actual_brightness'),
             "468\n")

  # ---- capabilities are derived from the hardware, not declared -----------
  start('BLIT_SETTINGS_PATH' => settings)
  verdict 'a-display-without-a-backlight-has-no-brightness' do
    item = display
    check(item.fetch('capabilities', []).include?('mode-report'),
          "mode-report is missing: #{item['capabilities'].inspect}")
    check(item.fetch('capabilities', []).include?('rotation'),
          "the SDL path must offer rotation: #{item['capabilities'].inspect}")
    check(!item.fetch('capabilities', []).include?('brightness'),
          'brightness is claimed with no backlight on the host')
    check(item.fetch('configuration', {}) == { 'rotation' => 0 },
          "configuration should carry rotation alone: #{item['configuration'].inspect}")
    code, problem = json('PATCH', '/v1/displays/display-1',
                         { 'configuration' => { 'brightness_percent' => 50 } })
    check(code == 422, "brightness with no backlight -> #{code}")
    check(problem.to_h['code'] == 'display_brightness_unsupported',
          "brightness refusal code: #{problem.inspect}")
    # A property that is not writable at all is a different refusal from one
    # the hardware cannot do, and the difference is the point: a controller
    # that sees 422 knows to look at `capabilities`, and 400 means it sent
    # something Blit will never accept.
    code, problem = json('PATCH', '/v1/displays/display-1',
                         { 'configuration' => { 'mode' => '1920x1080' } })
    check(code == 400, "a mode change -> #{code}")
    check(problem.to_h['code'] == 'invalid_display_property',
          "mode refusal code: #{problem.inspect}")
  end

  # ---- the rectangle rotates and the mode does not ------------------------
  verdict 'a-rotation-exchanges-the-rectangle-not-the-mode' do
    before = display
    check(before.dig('bounds', 'width') == PANEL_W &&
          before.dig('bounds', 'height') == PANEL_H,
          "the panel should start unrotated: #{before['bounds'].inspect}")
    code, after = patch('rotation' => 90)
    check(code == 200, "rotate to 90 -> #{code} #{after.inspect}")
    check(after.to_h.dig('bounds', 'width') == PANEL_H &&
          after.to_h.dig('bounds', 'height') == PANEL_W,
          "bounds after 90: #{after.to_h['bounds'].inspect}")
    check(after.to_h.dig('state', 'physical', 'width') == PANEL_W &&
          after.to_h.dig('state', 'physical', 'height') == PANEL_H,
          "the connector's mode must not move: #{after.to_h.dig('state', 'physical').inspect}")
    check(after.to_h.dig('active_mode', 'width') == PANEL_W,
          "active_mode after 90: #{after.to_h['active_mode'].inspect}")
    check(after.to_h.dig('configuration', 'rotation') == 90,
          "configuration after 90: #{after.to_h['configuration'].inspect}")
    # Two documents report a display's rectangle, and before #69 the topology
    # one printed a literal zero for rotation.
    topology_document = json('GET', '/v1/topology').last.to_h
    topology = topology_document.fetch('outputs', []).first.to_h
    check(topology['rotation'] == 90,
          "topology rotation: #{topology.inspect}")
    check(topology.dig('bounds', 'width') == PANEL_H,
          "topology bounds: #{topology['bounds'].inspect}")
    pane = topology_document.fetch('panes', []).first.to_h
    check(pane.dig('bounds', 'width') == PANEL_H / CELL_W &&
          pane.dig('bounds', 'height') == PANEL_W / CELL_H,
          "pane grid after 90: #{pane['bounds'].inspect}")
    code, problem = patch('rotation' => 45)
    check(code == 400, "rotation 45 -> #{code}")
    check(problem.to_h['code'] == 'invalid_display_rotation',
          "45 refusal code: #{problem.inspect}")
    code, = patch('rotation' => 0)
    check(code == 200, "rotate back to 0 -> #{code}")
    topology_document = json('GET', '/v1/topology').last.to_h
    pane = topology_document.fetch('panes', []).first.to_h
    check(pane.dig('bounds', 'width') == PANEL_W / CELL_W &&
          pane.dig('bounds', 'height') == PANEL_H / CELL_H,
          "pane grid after 0: #{pane['bounds'].inspect}")
    code, = patch('rotation' => 90)
    check(code == 200, "restore rotation 90 -> #{code}")
  end

  # ---- the grid a session receives follows the rotated rectangle ----------
  verdict 'the-cell-grid-follows-the-rotation' do
    created = session('sleep 60')
    rotated = json('GET', "/v1/surfaces/#{created.fetch('surface_id')}/cells").last.to_h
    request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
    check(rotated['width'] == PANEL_H / CELL_W,
          "a session on the rotated panel should be #{PANEL_H / CELL_W} columns, " \
          "not #{rotated['width']}")
    check(rotated['height'] == PANEL_W / CELL_H,
          "a session on the rotated panel should be #{PANEL_W / CELL_H} rows, " \
          "not #{rotated['height']}")
  end

  # ---- refused while a surface is present --------------------------------
  verdict 'a-rotation-is-refused-under-a-live-surface' do
    created = session('sleep 60')
    code, problem = patch('rotation' => 180)
    check(code == 409, "rotate under a surface -> #{code} #{problem.inspect}")
    check(problem.to_h['code'] == 'display_rotation_in_use',
          "in-use refusal code: #{problem.inspect}")
    check(display.dig('configuration', 'rotation') == 90,
          'the refused rotation was applied anyway')
    # The same request is accepted once the surface is gone, which is what
    # makes the refusal a conflict rather than a prohibition.
    request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
    sleep 0.5
    code, = patch('rotation' => 180)
    check(code == 200, "rotate after the surface is gone -> #{code}")
    patch('rotation' => 90)
  end

  # ---- the rotation reaches the panel ------------------------------------
  #
  # This is the scenario the others cannot stand in for. Everything above is
  # satisfied by a server that records the number and presents the scene
  # unrotated; this one is not. A full block is drawn in the first cell and
  # the ink is located in the capture, which is always the panel's own
  # 1024x768 regardless of rotation.
  verdict 'the-rotation-reaches-the-panel' do
    inset = json('GET', '/v1/diagnostics').last.to_h.dig('render',
                                                         'surface_inset_px')
    check(inset.to_i.positive?, "no surface inset reported: #{inset.inspect}")
    shots = {}
    [0, 90, 180, 270].each do |rotation|
      code, = patch('rotation' => rotation)
      check(code == 200, "rotate to #{rotation} -> #{code}")
      created = session("printf '\\342\\226\\210'; sleep 60")
      sleep 2.0
      shots[rotation] = screenshot
      request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")
      sleep 0.5
    end
    # For each rotation the block must be in the corner the contract names,
    # and in none of the other three. The second half is what fails for a
    # server that stores the number and presents the scene unrotated: the ink
    # stays in the 0 rectangle for all four.
    counts = {}
    [0, 90, 180, 270].each do |rotation|
      counts[rotation] = [0, 90, 180, 270].to_h do |candidate|
        [candidate,
         ink_in(shots[rotation], *first_cell_rect(candidate, inset.to_i))]
      end
      # Exactly the cell's area, not merely most of it. A full block covers
      # its whole cell, so a near miss means the scene was *scaled* into the
      # rotated rectangle rather than rotated into it -- a scene texture left
      # at the renderer's output size loses eight rows here and would pass any
      # looser threshold.
      check(counts[rotation][rotation] == CELL_W * CELL_H,
            "at #{rotation} the first cell has #{counts[rotation][rotation]} " \
            "lit pixels where it should have #{CELL_W * CELL_H}")
      (counts[rotation].keys - [rotation]).each do |elsewhere|
        check(counts[rotation][elsewhere].zero?,
              "at #{rotation} there are #{counts[rotation][elsewhere]} lit " \
              "pixels in the #{elsewhere} corner as well")
      end
    end
    counts.each do |rotation, lit|
      puts "  #{rotation}: #{lit.map { |k, v| "#{k}=#{v}" }.join(' ')}"
    end
  end

  # ---- persistence -------------------------------------------------------
  verdict 'a-rotation-survives-a-restart-and-0-clears-it' do
    code, = patch('rotation' => 270)
    check(code == 200, "rotate to 270 -> #{code}")
    stored = json('GET', '/v1/settings').last.to_h
    check(stored.dig('document', 'displays', '1', 'rotation') == 270,
          "the document should hold the rotation: #{stored['document'].inspect}")
    stop
    start('BLIT_SETTINGS_PATH' => settings)
    item = display
    check(item.dig('configuration', 'rotation') == 270,
          "rotation after a restart: #{item['configuration'].inspect}")
    check(item.dig('bounds', 'width') == PANEL_H,
          "bounds after a restart: #{item['bounds'].inspect}")
    # Back to 0 and the entry goes, rather than being left behind as a 0 that
    # a later reader has to interpret.
    code, = patch('rotation' => 0)
    check(code == 200, "rotate back to 0 -> #{code}")
    sleep 1.0
    stored = json('GET', '/v1/settings').last.to_h
    check(stored.dig('document', 'displays').to_h['1'].nil?,
          "0 should clear the stored entry: #{stored.dig('document', 'displays').inspect}")
    check(File.read(settings).include?('"displays"'),
          'the document should still carry a displays section')
  end
  stop

  # ---- a fabricated backlight --------------------------------------------
  verdict 'a-backlight-makes-brightness-a-capability' do
    start('BLIT_SETTINGS_PATH' => settings, 'BLIT_BACKLIGHT_ROOT' => backlight)
    item = display
    check(item.fetch('capabilities', []).include?('brightness'),
          "brightness should appear with a backlight: #{item['capabilities'].inspect}")
    check(item.dig('state', 'backlight') == 'intel_backlight',
          "the backlight should be named: #{item['state'].inspect}")
    # 468 of 937 is 49.946%, which rounds to 50.
    check(item.dig('configuration', 'brightness_percent') == 50,
          "468/937 should read as 50%: #{item['configuration'].inspect}")
    code, after = patch('brightness_percent' => 75)
    check(code == 200, "brightness 75 -> #{code} #{after.inspect}")
    raw = File.read(File.join(backlight, 'intel_backlight', 'brightness')).strip
    check(raw == '703', "75% of 937 should be 703 on disk, not #{raw}")
    # The read comes from actual_brightness, which a real driver updates and a
    # fabricated one does not -- so the item still reports the old percentage
    # until the sidecar is updated. That is the honest behaviour, and asserting
    # it keeps the two attributes from being quietly conflated.
    check(display.dig('configuration', 'brightness_percent') == 50,
          'the item should report actual_brightness, not the request')
    File.write(File.join(backlight, 'intel_backlight', 'actual_brightness'),
               "#{raw}\n")
    check(display.dig('configuration', 'brightness_percent') == 75,
          "after the panel caught up: #{display['configuration'].inspect}")
    code, problem = patch('brightness_percent' => 101)
    check(code == 400, "brightness 101 -> #{code} #{problem.inspect}")
  end
  stop
end

if $failures.empty?
  puts 'DISPLAY_SETTINGS_PASS capabilities=derived rotation=presented ' \
       'refusal=in-use persistence=connector-name brightness=seam'
else
  warn "display gate: #{$failures.length} failure(s)"
  exit 1
end
