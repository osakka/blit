#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0204: pixel zoom per display, on whichever compositor BLIT_GATE_BACKEND
# picks (the suite runs it on both). Two mock outputs: display-1, 1021x769,
# is zoomed; display-2, 640x384, never is. 1021x769 is chosen so that at
# 150% the logical display, 680x512, is exactly 85x32 cells: its last
# logical column and row are cells in a known colour, and the last physical
# column and row (1020 and 768, which map one past them) show them only
# through the clamp.
#
# display-1's session paints a flat background and a 40x24 Kitty picture
# whose every pixel is a different colour, again on each SIGWINCH, and
# writes its TIOCGWINSZ to a file. From those the gate builds the whole
# logical frame and, by the formula, the physical frame: pixel (x, y) shows
# logical (min(x * 100 / zoom, w - 1), min(y * 100 / zoom, h - 1)).
#
#   * maths: at 200%, 150%, 175% and 225% every physical pixel of display-1
#     is that prediction, exactly -- at 200% each logical pixel an exact 2x2
#     block; at 150% the uneven 2,1 repeat and the last column and row
#     through the clamp; at 175% and 225%, a step only the slider reaches,
#     other uneven repeats. The same exact
#     prediction on both compositors is the parity: CPU and GLES draw the
#     same pixels
#   * size: the session's grid (surface summary) and its winsize follow the
#     logical size, and its pixels are columns x the unchanged 8x16 cell
#   * pointer: an absolute move is logical, a relative move of physical
#     counts moves the logical pointer 100 / zoom of them
#   * not enlarged: the pointer art and the performance panel are the same
#     pixels at 200% as at 100%, the panel in the physical corner
#   * no residue: after the pointer and the panel leave, the frame is the
#     prediction again
#   * two displays: display-2's grid, winsize and pixels are untouched by
#     display-1's zoom
#   * 100% again: byte-identical to the screenshot before zooming
#   * scanlines under curvature: drawn in the logical scene and enlarged,
#     each odd logical row two dark physical rows at 200%, on both
#     compositors (GLES draws them on the glass only when unzoomed)
#   * gles: the zoomed frames are composed by GLES, and presentation
#     effects are reported unavailable on the zoomed display
# BLIT_ZOOM_GATE_SHOT=<path> keeps the 150% screenshot.

require 'base64'
require 'json'
require 'net/http'
require 'rbconfig'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19581'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 1021
H = 769
W2 = 640
H2 = 384
CELL_W = 8
CELL_H = 16
BG = [30, 60, 90].freeze
BG2 = [90, 40, 20].freeze
IMAGE_W = 40
IMAGE_H = 24
IMAGE_ROW = 2
IMAGE_COLUMN = 3
PANEL_BORDER = [0x50, 0x50, 0x5a].freeze
PANEL_BACKGROUND = [0x10, 0x10, 0x14].freeze
PANEL_MARGIN = 16
GLES = GATE_BACKEND == 'gles'

abort("display zoom gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'PATCH' => Net::HTTP::Patch,
            'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s if revision
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "DISPLAY_ZOOM_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 10)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def screenshot_bytes(display = 'display-1')
  Net::HTTP.get(BASE + "/v1/diagnostics/screenshots/#{display}")
end

# A screenshot once two in a row agree: the frame a gate compares is not
# one the compositor is still settling.
def settled(display = 'display-1')
  previous = nil
  Timeout.timeout(10) do
    loop do
      sleep 0.15
      bytes = screenshot_bytes(display)
      return bytes if bytes == previous
      previous = bytes
    end
  end
rescue Timeout::Error
  check(false, "#{display}'s screenshots never settled")
end

# Each row of a 32-bit screenshot as 0xRRGGBB integers, top row first.
def rows_of(bytes)
  shot = GateBmp.new(bytes)
  offset = bytes.byteslice(10, 4).unpack1('V')
  top_down = bytes.byteslice(22, 4).unpack1('l<').negative?
  check(bytes.byteslice(28, 2).unpack1('v') == 32, 'the screenshot is not 32-bit')
  stride = shot.width * 4
  rows = (0...shot.height).map do |y|
    row = top_down ? y : shot.height - 1 - y
    bytes.byteslice(offset + row * stride, stride).unpack('V*').map { |v| v & 0xffffff }
  end
  [shot.width, shot.height, rows]
end

def rgb(value)
  [(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff]
end

def int(colour)
  (colour[0] << 16) | (colour[1] << 8) | colour[2]
end

# The picture: every pixel its own colour.
IMAGE = (0...IMAGE_H).map { |j| (0...IMAGE_W).map { |i| [5 * i + 20, 9 * j + 7, 200] } }.freeze
IMAGE_DATA = Base64.strict_encode64(IMAGE.flatten.pack('C*'))

# Run by the session: argv is the winsize file, the background, and a file
# holding the picture's escape, if any.
SESSION_SCRIPT = <<~'RUBY'
  path, colour, picture = ARGV
  picture = File.read(picture) if picture
  redraw = true
  Signal.trap('WINCH') { redraw = true }
  $stdout.sync = true
  loop do
    if redraw
      redraw = false
      out = +"\e[?25l\e_Ga=d,d=A,q=2\e\\\e[48;2;#{colour}m\e[2J\e[H"
      out << picture if picture
      $stdout.write(out)
      size = [0, 0, 0, 0].pack('S4')
      $stdout.ioctl(0x5413, size)
      File.write("#{path}.new", size.unpack('S4').join(' '))
      File.rename("#{path}.new", path)
    end
    sleep 0.05
  end
RUBY

# rows cols xpixel ypixel, as the session last read them.
def winsize(path)
  File.exist?(path) ? File.read(path).split.map(&:to_i) : nil
end

def display(id)
  get("/v1/displays/#{id}")
end

def output(id)
  get('/v1/diagnostics')['render']['outputs'].find { |o| o['display_id'] == id }
end

def set_zoom(zoom)
  code, item = request('PATCH', '/v1/displays/display-1', { 'configuration' => { 'zoom' => zoom } })
  check(code == 200 && item.dig('configuration', 'zoom') == zoom, "PATCH zoom #{zoom}: #{code} #{item}")
  item
end

# The pointer, kept on display-2 while display-1 is compared.
def park
  bounds = display('display-2')['bounds']
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: bounds['x'] + 600, y: bounds['y'] + 300 })
end

def save(member, value)
  Timeout.timeout(5) { sleep 0.05 until get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document[member] = value
  code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
  check(code == 200, "#{member} #{value}: #{code} #{reply}")
end

def hud(document_hud)
  save('hud', document_hud)
end

# A local effect blended to black, as the compositors blend it.
def darken(colour, alpha)
  colour.map { |c| (c * (255 - alpha) + 127) / 255 }
end

def near?(actual, wanted, tolerance = 2)
  actual.zip(wanted).all? { |a, b| (a - b).abs <= tolerance }
end

# display-1's logical frame at a size: the background over the cell grid,
# which is centred (the remainder split, the smaller half first), the
# picture at its cell, the pane's floor around the whole cells.
def logical_frame(width, height, inset, floor)
  columns = (width - 2 * inset) / CELL_W
  rows = (height - 2 * inset) / CELL_H
  left = inset + (width - columns * CELL_W) / 2
  top = inset + (height - rows * CELL_H) / 2
  x0 = left + (IMAGE_COLUMN - 1) * CELL_W
  y0 = top + (IMAGE_ROW - 1) * CELL_H
  background = int(BG)
  (0...height).map do |y|
    (0...width).map do |x|
      if x >= x0 && x < x0 + IMAGE_W && y >= y0 && y < y0 + IMAGE_H
        int(IMAGE[y - y0][x - x0])
      elsif x >= left && y >= top && x < left + columns * CELL_W && y < top + rows * CELL_H
        background
      else
        floor
      end
    end
  end
end

# The physical frame by ADR 0204's formula, the last column and row clamped.
def physical_frame(logical, zoom)
  height = logical.length
  width = logical.first.length
  columns = (0...W).map { |x| [x * 100 / zoom, width - 1].min }
  (0...H).map do |y|
    row = logical[[y * 100 / zoom, height - 1].min]
    columns.map { |c| row[c] }
  end
end

# Every pixel of a screenshot against a frame, exactly.
def frame_matches(bytes, expected, what)
  width, height, rows = rows_of(bytes)
  check(width == W && height == H, "#{what}: the screenshot is #{width}x#{height}, not the physical #{W}x#{H}")
  wrong = []
  rows.each_with_index do |row, y|
    next if row == expected[y]
    row.each_with_index do |value, x|
      wrong << [x, y, rgb(value), rgb(expected[y][x])] if value != expected[y][x]
    end
  end
  check(wrong.empty?, "#{what}: #{wrong.length} pixels are not the formula's, first " \
                      "#{wrong.first(6).map { |x, y, got, want| "(#{x},#{y}) #{got} not #{want}" }.join(', ')}")
  W * H
end

# The settled frame once it is the expected one; a session repaints after
# its SIGWINCH, so the first settled frame can still be the old grid's. A
# frame that is never right fails with its wrong pixels.
def frame_becomes(expected, what)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 10
  begin
    bytes = settled
    [bytes, frame_matches(bytes, expected, what)]
  rescue RuntimeError
    raise if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
    sleep 0.3
    retry
  end
end

# What the pointer changed around a physical point: offset => colour.
def pointer_art(bytes, expected, x, y)
  _, _, rows = rows_of(bytes)
  art = {}
  ((y - 48)...(y + 48)).each do |py|
    next if py.negative? || py >= H
    ((x - 48)...(x + 48)).each do |px|
      next if px.negative? || px >= W
      art[[px - x, py - y]] = rows[py][px] if rows[py][px] != expected[py][px]
    end
  end
  art
end

# The size display-1's session and display-2's should have, and have.
def sizes_follow(zoom, files, surfaces, inset)
  wl = W * 100 / zoom
  hl = H * 100 / zoom
  bounds = display('display-1')['bounds']
  check(bounds['width'] == wl && bounds['height'] == hl,
        "#{zoom}%: display-1's bounds #{bounds}, not #{wl}x#{hl}")
  columns = (wl - 2 * inset) / CELL_W
  rows = (hl - 2 * inset) / CELL_H
  want = [rows, columns, columns * CELL_W, rows * CELL_H]
  wait_until("#{zoom}%: the session's winsize is #{winsize(files[1]).inspect}, not #{want}") do
    winsize(files[1]) == want
  end
  summary = get("/v1/surfaces/#{surfaces[1]}/summary")
  check([summary['width'], summary['height']] == [columns, rows],
        "#{zoom}%: the session's grid is #{summary['width']}x#{summary['height']}, not #{columns}x#{rows}")
  columns2 = (W2 - 2 * inset) / CELL_W
  rows2 = (H2 - 2 * inset) / CELL_H
  check(winsize(files[2]) == [rows2, columns2, columns2 * CELL_W, rows2 * CELL_H],
        "#{zoom}%: display-2's session winsize changed: #{winsize(files[2]).inspect}")
  summary2 = get("/v1/surfaces/#{surfaces[2]}/summary")
  check([summary2['width'], summary2['height']] == [columns2, rows2],
        "#{zoom}%: display-2's grid changed: #{summary2['width']}x#{summary2['height']}")
  [wl, hl, want]
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x1', 'BLIT_AUTH_TOKEN' => '',
  'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'))
# A second output beside the first.
File.write(env['BLIT_DRM_MOCK_STATE'], "2 0 0 0\n1 #{W} #{H}\n2 #{W2} #{H2}\n")
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-display-zoom') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  check(get('/v1/displays')['displays'].map { |d| d['id'] }.sort == %w[display-1 display-2],
        "two displays: #{get('/v1/displays')['displays'].map { |d| d['id'] }}")
  script = File.join(root, 'session.rb')
  File.write(script, SESSION_SCRIPT)
  files = { 1 => File.join(root, 'winsize-1'), 2 => File.join(root, 'winsize-2') }
  surfaces = {}
  picture = File.join(root, 'picture')
  File.write(picture, "\e[#{IMAGE_ROW};#{IMAGE_COLUMN}H\e_Ga=T,f=24,s=#{IMAGE_W},v=#{IMAGE_H},i=1,q=2,C=1;" \
                      "#{IMAGE_DATA}\e\\")
  { 1 => [BG, picture], 2 => [BG2, nil] }.each do |n, (colour, picture_file)|
    arguments = [script, files[n], colour.join(';')]
    arguments << picture_file if picture_file
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: RbConfig.ruby, arguments: arguments, pane_id: "pane-display-#{n}-1" })
    check(code == 201, "session on display-#{n}: #{code} #{session}")
    surfaces[n] = session['surface_id']
  end
  inset = get('/v1/diagnostics')['render']['surface_inset_px']
  park
  backend = GLES ? 'gles' : 'cpu'
  wait_until("the #{backend} compositor is not presenting display-1") do
    output('display-1').dig('compositor', 'active') == backend
  end
  wait_until('the picture never arrived') do
    (get('/v1/diagnostics/screenshots/display-1') rescue nil) && winsize(files[1]) && winsize(files[2])
  end

  # 100%: the model holds, and this is the frame 100% must return to.
  _, _, want100 = sizes_follow(100, files, surfaces, inset)
  wait_until('the session never painted display-1') { GateBmp.new(screenshot_bytes).pixel(W / 2, H / 2) == BG }
  floor = rows_of(settled)[2][0][W - 1]
  check(floor != int(BG), 'display-1 has no floor past its last cell at 100%')
  logical100 = logical_frame(W, H, inset, floor)
  before, checked = frame_becomes(logical100, '100%')
  before2 = settled('display-2')

  # The pointer and the panel at 100%.
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 240, y: 220 })
  art100 = pointer_art(settled, logical100, 240, 220)
  check(!art100.empty?, 'no pointer drawn at 100%')
  park
  on = { 'enabled' => true, 'display_id' => 'display-1', 'corner' => 'top-right', 'hz' => 2,
         'sections' => %w[blit] }
  hud(on)
  wait_until('no panel at 100%') { !get('/v1/diagnostics')['hud']['panels'].empty? }
  panel100 = get('/v1/diagnostics')['hud']['panels'].first.values_at('x', 'y', 'width', 'height')
  hud({ 'enabled' => false })
  wait_until('the panel stayed at 100%') { get('/v1/diagnostics')['hud']['panels'].empty? }

  results = []
  pointer = nil
  panel200 = nil
  [200, 150, 175, 225].each do |zoom|
    item = set_zoom(zoom)
    check(item.dig('state', 'physical') == { 'width' => W, 'height' => H },
          "#{zoom}%: the physical size changed: #{item['state']}")
    wl, hl, want = sizes_follow(zoom, files, surfaces, inset)
    park
    frame = physical_frame(logical_frame(wl, hl, inset, floor), zoom)
    if GLES
      wait_until("#{zoom}%: display-1 is not composed by GLES: #{output('display-1')['compositor']}") do
        output('display-1').dig('compositor', 'active') == 'gles'
      end
      reason = output('display-1').dig('effects', 'presentation_unavailable')
      check(reason == 'zoomed display', "#{zoom}%: effects unavailable because #{reason.inspect}")
    end
    check(settled('display-2') == before2, "#{zoom}% on display-1 changed display-2's pixels")
    bytes, count = frame_becomes(frame, "#{zoom}%")
    File.binwrite(ENV['BLIT_ZOOM_GATE_SHOT'], bytes) if zoom == 150 && ENV['BLIT_ZOOM_GATE_SHOT']
    checked += count

    if zoom == 200
      # Absolute moves are logical; relative counts are physical.
      request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 100, y: 100 })
      at = get('/v1/diagnostics')['render']['pointer'].values_at('x', 'y')
      check(at == [100.0, 100.0], "200%: an absolute move to 100,100 put the pointer at #{at}")
      request('POST', '/v1/input/inject', { kind: 'pointer_relative', dx: 40, dy: 20 })
      at = get('/v1/diagnostics')['render']['pointer'].values_at('x', 'y')
      check(at == [120.0, 110.0], "200%: 40,20 physical counts from 100,100 put the pointer at #{at}")
      pointer = at
      art200 = pointer_art(settled, frame, 240, 220)
      check(art200 == art100, "200%: the pointer is not the 100% pointer at physical 240,220 " \
                              "(#{art200.length} pixels, #{art100.length} at 100%)")
      park
      hud(on)
      wait_until('no panel at 200%') { !get('/v1/diagnostics')['hud']['panels'].empty? }
      sleep 0.6
      panel200 = get('/v1/diagnostics')['hud']['panels'].first.values_at('x', 'y', 'width', 'height')
      check(panel200 == panel100 && panel200[0] == W - PANEL_MARGIN - panel200[2] && panel200[1] == PANEL_MARGIN,
            "200%: the panel is #{panel200}, at 100% #{panel100}")
      shot = GateBmp.new(screenshot_bytes)
      x, y, width, height = panel200
      check(shot.pixel(x, y) == PANEL_BORDER && shot.pixel(x + width - 1, y + height - 1) == PANEL_BORDER &&
            shot.pixel(x + 1, y + 1) == PANEL_BACKGROUND && shot.pixel(x + width - 2, y + height - 2) == PANEL_BACKGROUND &&
            shot.pixel(x - 1, y) == rgb(frame[y][x - 1]) && shot.pixel(x + width - 1, y + height) == rgb(frame[y + height][x + width - 1]),
            "200%: the panel is not drawn #{width}x#{height} at physical #{x},#{y}")
      hud({ 'enabled' => false })
      wait_until('the panel stayed at 200%') { get('/v1/diagnostics')['hud']['panels'].empty? }
      # Where they were is the enlarged frame again.
      checked += frame_becomes(frame, '200% after the pointer and the panel')[1]
      # Scanlines under curvature: the presentation pass that draws them on
      # the glass is not run on a zoomed display, so they are drawn in the
      # logical scene and enlarged -- each odd logical row two dark physical
      # rows -- on both compositors, not lost.
      connector = display('display-1').dig('state', 'connector')
      save('effects', { connector => { '1' => { 'curvature' => { 'amount' => 100 },
                                               'scanlines' => { 'intensity' => 50 } } } })
      dark = darken(BG, 50 * 255 / 100)
      wrong = nil
      20.times do
        shot = GateBmp.new(settled)
        wrong = (0...H).map { |py| [py, shot.pixel(W / 2, py)] }
                       .reject { |py, colour| near?(colour, [py * 100 / zoom, hl - 1].min.odd? ? dark : BG) }
        break if wrong.empty?
        sleep 0.25
      end
      check(wrong.empty?, "200%: no enlarged scanlines under curvature in column #{W / 2}: " \
                          "#{wrong.length} rows wrong, first #{wrong.first(4)}")
      save('effects', { connector => { '1' => {} } })
      checked += frame_becomes(frame, '200% after the scanlines')[1]
    end

    results << "#{zoom}=#{wl}x#{hl}:#{want[1]}x#{want[0]}:#{want[2]}x#{want[3]}px"
  end

  # 100% again: the frame from before, byte for byte.
  set_zoom(100)
  sizes_follow(100, files, surfaces, inset)
  park
  if GLES
    reason = output('display-1').dig('effects', 'presentation_unavailable')
    check(reason != 'zoomed display', "100%: effects still unavailable because #{reason.inspect}")
  end
  after, count = frame_becomes(logical100, '100% again')
  checked += count
  check(after == before, '100% again is not byte-identical to the screenshot before zooming')
  check(settled('display-2') == before2, "display-2's pixels changed")

  puts "DISPLAY_ZOOM_PASS backend=#{backend} sizes=100=#{W}x#{H}:#{want100[1]}x#{want100[0]} #{results.join(' ')} " \
       "formula_exact=#{checked}px(200=2x2_blocks,150=column+row_clamp,175+225=uneven) " \
       "pointer=absolute_logical+relative_#{pointer&.join(',')} pointer_art=#{art100.length}px_unenlarged " \
       "panel=#{panel200[2]}x#{panel200[3]}@#{panel200[0]},#{panel200[1]}_unenlarged " \
       'scanlines_under_curvature=enlarged display-2=untouched back_to_100=byte_identical' \
       "#{GLES ? ' effects=unavailable_zoomed' : ''}"
end
