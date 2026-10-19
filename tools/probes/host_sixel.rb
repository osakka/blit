#!/usr/bin/env ruby
# frozen_string_literal: true

# Issues #8, #9, #10, #11 on the live host. The pixel half runs on the
# topology-discovered physical output
# while nothing else owns it -- a supervised update has just restarted the
# server, so the desktop is down and the output is free. Run this before
# relaunching the desktop.

require 'json'
require 'net/http'
require 'uri'
require 'fileutils'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
CELL_W = 8
CELL_H = 16

def call(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{TOKEN}"
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  res = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 40) { |h| h.request(req) }
  [res.code.to_i, (res.body.to_s.empty? ? nil : (JSON.parse(res.body) rescue res.body))]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "live sixel: FAIL #{message}"
end

# Decoder scenarios use independent surfaces. The pixel scenario explicitly
# attaches to a discovered free pane, including nonzero split-pane origins.
#
# The escapes arrive as positional parameters rather than inside the script:
# create_session caps one argument at 256 bytes, and a scenario that sends
# seven images is longer than that. `sh -c SCRIPT sh A B C` makes A, B and C
# into $1, $2 and $3, so each stays well inside the cap.
def scenario(chunks, images:, columns: 60, rows: 12, display: nil,
             script: nil)
  parameters = Array(chunks)
  body = script ||
         "printf '%b' \"#{(1..parameters.length).map { |i| "$#{i}" }.join}\"; " \
         'sleep 90'
  code, created = call('POST', '/v1/terminal-sessions',
                       'command' => '/bin/sh',
                       'arguments' => ['-c', body, 'sh', *parameters],
                       'surface_mode' => 'terminal',
                       'surface' => { 'width' => columns, 'height' => rows },
                       **(display ? { 'pane_id' => PIXEL_PANE.fetch('id') } : {}))
  abort "session -> #{code} #{created.inspect}" unless code == 201
  session = created.fetch('id')
  surface = created.fetch('surface_id')
  begin
    raise 'pixel scenario attached to the wrong pane' if
      display && created['pane_id'] != PIXEL_PANE.fetch('id')
    deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 30
    loop do
      _, state = call('GET', "/v1/terminal-sessions/#{session}")
      break if state['graphics'].to_a.length >= images
      raise 'sixel images did not arrive before deadline' if
        Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline

      sleep 0.2
    end
    sleep 1.5
    _, state = call('GET', "/v1/terminal-sessions/#{session}")
    _, summary = call('GET', "/v1/surfaces/#{surface}/summary")
    summary['probe_surface_id'] = surface
    yield state, summary
  ensure
    call('DELETE', "/v1/terminal-sessions/#{session}")
  end
end

def pixel(bmp, width, height, x, y)
  bits = bmp.getbyte(28) | (bmp.getbyte(29) << 8)
  bytes = bits / 8
  raise "unsupported BMP depth #{bits}" unless [3, 4].include?(bytes)
  stride = (width * bytes + 3) & ~3
  stored_width, stored_height = bmp.byteslice(18, 8).unpack('l<2')
  raise 'capture geometry mismatch' unless stored_width == width && stored_height.abs == height
  raise 'sample outside framebuffer' unless x.between?(0,width-1) && y.between?(0,height-1)
  row = stored_height.positive? ? height - 1 - y : y
  offset = bmp.byteslice(10,4).unpack1('V') + row * stride + x * bytes
  [bmp.getbyte(offset + 2), bmp.getbyte(offset + 1), bmp.getbyte(offset)]
end

_, diagnostics = call('GET', '/v1/diagnostics')
graphics = diagnostics['graphics']
check(!graphics.nil?, 'diagnostics publish the graphics bounds')
if graphics
  puts "published bounds: #{graphics.inspect}"
  check(graphics['sixel_max_width'] == 4096 && graphics['sixel_max_height'] == 4096,
        "the sixel bound is 4096 square, got #{graphics.inspect}")
  check(graphics['sixel_max_width'] * graphics['sixel_max_height'] * 4 ==
        graphics['max_bytes'],
        'a full-size sixel canvas is exactly the byte budget')
end
topology = call('GET', '/v1/topology').last
PIXEL_PANE = topology.fetch('panes').find do |pane|
  pane['surface_id'].nil? && (!ENV['BLIT_SIXEL_PANE_ID'] || pane['id'] == ENV['BLIT_SIXEL_PANE_ID'])
end
raise 'requires a free pane; never overwrites the desktop' unless PIXEL_PANE
grid = PIXEL_PANE.fetch('cell_grid')
raise 'sixel probe requires canonical 8x16 cells' unless
  grid['cell_width'] == CELL_W && grid['cell_height'] == CELL_H
display_id = PIXEL_PANE.fetch('display_id')
output = diagnostics.fetch('render').fetch('outputs').find { |o| o['display_id'] == display_id }
inset = diagnostics.fetch('render').fetch('surface_inset_px')
fb_w = output.fetch('render_width')
fb_h = output.fetch('render_height')
origin_x = grid.fetch('origin_x') + inset
origin_y = grid.fetch('origin_y') + inset
puts "output #{fb_w}x#{fb_h} inset=#{inset}"

HIDE = '\\033[?25l'

# --- #8: a terminal-width run on this host's own 1920-pixel output ---
scenario([HIDE + '\\033[1;1H\\033Pq"1;1;1920;6#0;2;0;100;0!1920~\\033\\\\'],
         images: 1) do |state, _summary|
  image = state['graphics'].to_a.first
  check(!image.nil?,
        "a 1920-pixel run decodes, got #{state['graphics_diagnostics'].inspect}")
  if image
    puts "1920-run: width=#{image['width']} height=#{image['height']} " \
         "placement=#{image['placement_width']}x#{image['placement_height']}"
    check(image.fetch('width') == 1920, "decoded width #{image['width']}")
    check(image.fetch('placement_width') == 60 * CELL_W,
          "clipped to the surface, got #{image['placement_width']}")
  end
  check(state['graphics_diagnostics'].nil?,
        "nothing rejected, got #{state['graphics_diagnostics'].inspect}")
end

# The last band at the bound: 682 advances put the next sixel's first bit at
# y=4092, and bits 3 and 4 land on 4095 and 4096.
scenario([], images: 1, script: <<~SH) do |state, _summary|
  d=; i=0
  while [ $i -lt 682 ]; do d="$d-"; i=$((i+1)); done
  printf '%b' "\\033[?25l\\033Pq#0;2;100;0;100${d}W\\033\\\\"
  sleep 90
SH
  image = state['graphics'].to_a.first
  check(!image.nil?, 'the last band at the bound decodes')
  if image
    puts "last band: height=#{image['height']}"
    check(image.fetch('height') == 4096, "height #{image['height']}")
  end
  check(state['graphics_diagnostics'].nil?,
        "nothing rejected, got #{state['graphics_diagnostics'].inspect}")
end

# --- #11: the cursor contract and the two modes ---
solid = '~~~~~~~~'
scenario([HIDE + "\\033[1;1H\\033Pq#1;2;100;0;0#{solid}\\033\\\\",
          "\\033Pq#2;2;0;100;0#{solid}\\033\\\\"],
         images: 2) do |state, summary|
  cursor = summary.fetch('cursor')
  puts "cursor after two images: #{cursor.inspect}"
  check(cursor.fetch('x').zero? && cursor.fetch('y') == 2,
        "the cursor is below the second image, got #{cursor.inspect}")
  ys = state['graphics'].to_a.map { |g| g['y'] }.sort
  puts "image origins: #{ys.inspect}"
  check(ys == [0, CELL_H],
        "two images stack rather than sharing an origin, got #{ys.inspect}")
end

scenario([HIDE + "\\033[?8452h\\033[2;3H\\033Pq#3;2;0;0;100#{solid}\\033\\\\"],
         images: 1) do |_state, summary|
  cursor = summary.fetch('cursor')
  puts "cursor under 8452: #{cursor.inspect}"
  check(cursor.fetch('x') == 3 && cursor.fetch('y') == 1,
        "8452 leaves the cursor right of the graphic, got #{cursor.inspect}")
end

scenario([HIDE + "\\033[?80h\\033[4;10H\\033Pq#2;2;0;100;0#{solid}\\033\\\\"],
         images: 1) do |state, summary|
  cursor = summary.fetch('cursor')
  image = state['graphics'].to_a.first
  puts "DECSDM: cursor=#{cursor.inspect} origin=#{[image&.dig('x'), image&.dig('y')].inspect}"
  check(cursor.fetch('x') == 9 && cursor.fetch('y') == 3,
        "DECSDM leaves the cursor alone, got #{cursor.inspect}")
  check(image && image['x'].zero? && image['y'].zero?,
        'DECSDM homes the image')
end

# --- the pixel half, on the topology-discovered physical output ---
#
# Nothing in this stream defines colours 1, 2 or 3, and the HLS triples are
# the ones #9's ticket quotes. Every one of these used to be white or absent.
scenario([HIDE + "\\033[1;1H\\033Pq#1#{solid}\\033\\\\",
          "\\033[1;3H\\033Pq#2#{solid}\\033\\\\",
          "\\033[1;5H\\033Pq#3#{solid}\\033\\\\",
          "\\033[3;1H\\033Pq#0;1;120;50;100#{solid}\\033\\\\",
          "\\033[3;3H\\033Pq#0;1;0;50;100#{solid}\\033\\\\",
          "\\033[3;5H\\033Pq#0;1;240;50;100#{solid}\\033\\\\",
          "\\033[48;2;0;0;255m\\033[5;1H\\033Pq#1;2;100;0;0~?~\\033\\\\"],
         images: 7, display: display_id) do |state, summary|
  check(state['graphics'].to_a.length == 7,
        "all seven images decode, got #{state['graphics'].to_a.length} " \
        "#{state['graphics_diagnostics'].inspect}")
  shot = nil
  captured = {}
  coherent = false
  12.times do
    uri = BASE + "/v1/diagnostics/screenshots/#{display_id}"
    request = Net::HTTP::Get.new(uri)
    request['Authorization'] = "Bearer #{TOKEN}"
    shot = Net::HTTP.start(uri.hostname, uri.port, open_timeout:3, read_timeout:40) { |http| http.request(request) }
    raise "capture HTTP #{shot.code}" unless shot.code == '200'
    captured = (shot['X-Blit-Capture-Revisions'] || '').split(',').to_h { |pair| pair.split('=') }
    coherent = captured[summary.fetch('probe_surface_id')].to_i == summary.fetch('revision') &&
      (shot['X-Blit-Capture-Presented'] == 'true' ||
       (diagnostics.dig('render','direct_kms') && shot['X-Blit-Capture-Presented'].nil?))
    break if coherent
    sleep 0.02
  end
  raise 'sixel capture was not coherent and presented' unless coherent
  bmp = shot.body
  if (directory = ENV['BLIT_EVIDENCE_DIR'])
    FileUtils.mkdir_p(directory)
    File.binwrite(File.join(directory,'sixel-composition.bmp'),bmp)
    File.write(File.join(directory,'sixel-composition.json'),JSON.pretty_generate(
      {display_id:display_id,pane_id:PIXEL_PANE['id'],origin:[origin_x,origin_y],
       surface:summary,capture_revisions:captured,frame:shot['X-Blit-Capture-Frame']}))
  end
  at = ->(x, y) { pixel(bmp, fb_w, fb_h, origin_x + x, origin_y + y) }
  samples = {
    'VT340 #1 (blue)' => [at.call(0, 0), [51, 51, 204]],
    'VT340 #2 (red)' => [at.call(16, 0), [204, 33, 33]],
    'VT340 #3 (green)' => [at.call(32, 0), [51, 204, 51]],
    'HLS 120 (red)' => [at.call(0, 32), [255, 0, 0]],
    'HLS 0 (blue)' => [at.call(16, 32), [0, 0, 255]],
    'HLS 240 (green)' => [at.call(32, 32), [0, 255, 0]],
    'P2=0 fill' => [at.call(1, 64), [0, 0, 255]],
    'P2=0 set column' => [at.call(0, 64), [255, 0, 0]]
  }
  samples.each do |label, (got, want)|
    puts format('  %-20s %-15s want %s', label, got.inspect, want.inspect)
    check(got == want, "#{label}: got #{got.inspect}, want #{want.inspect}")
  end
end

if $failures.empty?
  puts "LIVE_SIXEL_PASS pane=#{PIXEL_PANE.fetch('id')} origin=#{origin_x},#{origin_y} bound=4096_square clip=1920_to_480 last_band=4096 " \
       'palette=vt340 hls=dec_hue_origin p2=filled cursor=advances ' \
       'decsdm=homes 8452=right_of_graphic'
else
  warn "live sixel: #{$failures.length} failure(s)"
  exit 1
end
