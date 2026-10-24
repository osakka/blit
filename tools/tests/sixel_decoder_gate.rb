#!/usr/bin/env ruby
# frozen_string_literal: true

# Issues #8, #9, #10, #11. The sixel decoder's four gaps all had the same
# shape: a stream that a VT340 draws produced either nothing at all or
# something visibly wrong, and the state API could not tell the difference
# because a rejected image and an image full of white blocks both leave the
# session looking healthy. So this gate reads the framebuffer: state records
# what was asked, pixels record what happened.
#
#   #8  the bound was a literal 512 and overflow discarded the whole image
#   #9  HLS colour (Pu=1) discarded the whole image
#   #10 every default colour register was white
#   #11 P2 was never read, the cursor never moved, 80/8452 did not exist
#
# Each scenario is its own session: BLIT_GRAPHICS_MAX_IMAGES is 8, and a
# scenario that scrolls the screen 249 times should not be sharing a surface
# with one that is about to read pixels.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19253'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
FB_W = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
FB_H = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)
CELL_W = 8
CELL_H = 16
COLUMNS = 60
ROWS = 12

# The VT340's default colour registers, as percentages scaled the way the
# server scales them. Before #10 every one of these read as white.
VT340_BLUE = [51, 51, 204].freeze
VT340_RED = [204, 33, 33].freeze
VT340_GREEN = [51, 204, 51].freeze
RED = [255, 0, 0].freeze
GREEN = [0, 255, 0].freeze
BLUE = [0, 0, 255].freeze
YELLOW = [255, 255, 0].freeze
WHITE = [255, 255, 255].freeze
GREY = [128, 128, 128].freeze
BLACK = [0, 0, 0].freeze

abort("sixel-decoder gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "sixel-decoder-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 60) { |h| h.request(req) }
end

def json(path)
  JSON.parse(request('GET', path).body)
end

def capture
  uri = BASE + '/v1/diagnostics/screenshots/display-1'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 60) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  abort("sixel-decoder gate: screenshot #{response.code}") unless response.code == '200'
  response.body
end

def pixel(bmp, x, y)
  gate_pixel(bmp, x, y)
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "sixel-decoder gate: FAIL #{message}"
end

# A PASS line has to be earned: printing it unconditionally after a block
# that may have failed is how a gate ends up reading green while its own
# output above says otherwise.
def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

server = spawn({ **gate_display_env, 'SDL_RENDERER' => 'software',
                 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end
Timeout.timeout(20) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end
INSET = json('/v1/diagnostics').fetch('render').fetch('surface_inset_px')

def cup(row, column)
  "\\033[#{row};#{column}H"
end

# A sixel image of solid `colour` eight pixels wide and six tall: every bit
# of every byte set, so the extent has no unset pixel and P2 cannot change
# what is asserted.
def solid_sixel(introducer, body)
  "\\033P#{introducer}q#{body}~~~~~~~~\\033\\\\"
end

# Runs one scenario in its own session and yields the framebuffer, the
# session document and the surface summary.
def scenario(name, escapes, images:, columns: COLUMNS, rows: ROWS)
  Dir.mktmpdir("sixel-#{name}") do |dir|
    File.write(File.join(dir, 'a'), "\\033[?25l#{escapes}")
    created = JSON.parse(request('POST', '/v1/terminal-sessions',
      'command' => '/bin/sh',
      'arguments' => ['-c', "printf '%b' \"$(cat #{dir}/a)\"; sleep 120"],
      'surface' => { 'width' => columns, 'height' => rows },
      'display_id' => 'display-1').body)
    abort("sixel-decoder gate: #{name} create failed #{created}") unless created['id']
    session = created.fetch('id')
    surface = created.fetch('surface_id')
    begin
      Timeout.timeout(30) do
        sleep 0.1 until json("/v1/terminal-sessions/#{session}")['graphics']
                          .to_a.length >= images
      end
    rescue Timeout::Error
      state = json("/v1/terminal-sessions/#{session}")
      check(false, "#{name}: expected #{images} image(s), got " \
                   "#{state['graphics'].to_a.length} " \
                   "#{state['graphics_diagnostics'].inspect}")
    end
    sleep 1.5
    state = json("/v1/terminal-sessions/#{session}")
    summary = json("/v1/surfaces/#{surface}/summary")
    cells = json("/v1/surfaces/#{surface}/cells")
    yield capture, state, summary, cells
    request('DELETE', "/v1/terminal-sessions/#{session}")
  end
end

def at(bmp, x, y)
  pixel(bmp, INSET + x, INSET + y)
end

# --- Issue #10: the default colour registers, and the palette's lifetime ---
#
# Nothing in these streams defines colours 1, 2 or 3; a VT340 answers blue,
# red and green. Register 5 is defined in one DCS and used by the next: the
# palette belongs to the session, not to the string.
verdict('sixel-palette-defaults-and-persistence') do
  scenario('palette-defaults', [
    cup(1, 1), solid_sixel('', '#1'),
    cup(1, 3), solid_sixel('', '#2'),
    cup(1, 5), solid_sixel('', '#3'),
    cup(3, 1), solid_sixel('', '#5;2;100;100;0'),
    cup(3, 3), solid_sixel('', '#5')
  ].join, images: 5) do |bmp, state, _summary, _cells|
    check(at(bmp, 0, 0) == VT340_BLUE,
          "#1 undefined is VT340 blue, got #{at(bmp, 0, 0).inspect}")
    check(at(bmp, 16, 0) == VT340_RED,
          "#2 undefined is VT340 red, got #{at(bmp, 16, 0).inspect}")
    check(at(bmp, 32, 0) == VT340_GREEN,
          "#3 undefined is VT340 green, got #{at(bmp, 32, 0).inspect}")
    check(at(bmp, 0, 32) == YELLOW,
          "a defined register is applied, got #{at(bmp, 0, 32).inspect}")
    check(at(bmp, 16, 32) == YELLOW,
          'a register defined by an earlier DCS survives into the next, got ' \
          "#{at(bmp, 16, 32).inspect}")
    check(state['graphics_diagnostics'].nil?,
          "no image was rejected, got #{state['graphics_diagnostics'].inspect}")
  end
end

# --- Issue #9: HLS, and clamping instead of discarding ---
#
# DEC's hue origin is blue at 0 degrees, so 120 is red and 240 is green --
# the rotation is the whole point of the vector. Saturation 0 is grey
# whatever the hue. An out-of-range RGB component clamps, and a colour space
# Blit does not model leaves the register as it was found.
verdict('sixel-hls-colour') do
  scenario('hls-colour', [
    cup(1, 1), solid_sixel('', '#0;1;120;50;100'),
    cup(1, 3), solid_sixel('', '#0;1;0;50;100'),
    cup(1, 5), solid_sixel('', '#0;1;240;50;100'),
    cup(1, 7), solid_sixel('', '#0;1;99;50;0'),
    cup(3, 1), solid_sixel('', '#0;2;200;200;200'),
    cup(3, 3), solid_sixel('', '#0;7;100;0;0')
  ].join, images: 6) do |bmp, state, _summary, _cells|
    check(at(bmp, 0, 0) == RED,
          "HLS 120;50;100 is red, got #{at(bmp, 0, 0).inspect}")
    check(at(bmp, 16, 0) == BLUE,
          "HLS 0;50;100 is blue (DEC's hue origin), got #{at(bmp, 16, 0).inspect}")
    check(at(bmp, 32, 0) == GREEN,
          "HLS 240;50;100 is green, got #{at(bmp, 32, 0).inspect}")
    check(at(bmp, 48, 0) == GREY,
          "HLS saturation 0 is grey, got #{at(bmp, 48, 0).inspect}")
    check(at(bmp, 0, 32) == WHITE,
          "an RGB component past 100 clamps, got #{at(bmp, 0, 32).inspect}")
    check(at(bmp, 16, 32) == WHITE,
          'an unmodelled colour space leaves the register alone rather than ' \
          "discarding the image, got #{at(bmp, 16, 32).inspect}")
    check(state['graphics_diagnostics'].nil?,
          "no image was rejected, got #{state['graphics_diagnostics'].inspect}")
  end
end

# --- Issue #8: the bound, and clipping at it ---
#
# `!1920~` is the most natural thing an encoder emits for a flat
# terminal-width region. It used to be refused twice over: by the repeat
# cap and by the 512-pixel scratch buffer. The surface here is 480 pixels
# wide, so the image is larger than the screen and must be clipped, not
# discarded.
verdict('sixel-wide-image-clips') do
  scenario('wide-image', [
    cup(1, 1), "\\033Pq\"1;1;1920;6#0;2;0;100;0!1920~\\033\\\\",
    cup(3, 1), "\\033Pq\"1;1;5000;1#0;2;0;0;100~\\033\\\\"
  ].join, images: 2) do |bmp, state, _summary, _cells|
    wide = state.fetch('graphics').find { |g| g.fetch('width') == 1920 }
    check(!wide.nil?,
          "a 1920-pixel run decodes to a 1920-wide image, got " \
          "#{state.fetch('graphics').map { |g| g['width'] }.inspect}")
    if wide
      check(wide.fetch('placement_width') == COLUMNS * CELL_W,
            "the wide image is clipped to the screen, got " \
            "#{wide.fetch('placement_width')}")
    end
    check(at(bmp, 0, 0) == GREEN,
          "the wide image is drawn at its origin, got #{at(bmp, 0, 0).inspect}")
    check(at(bmp, COLUMNS * CELL_W - 1, 0) == GREEN,
          'the wide image is drawn to the last pixel of the screen, got ' \
          "#{at(bmp, COLUMNS * CELL_W - 1, 0).inspect}")
    clamped = state.fetch('graphics').find { |g| g.fetch('width') == 4096 }
    check(!clamped.nil?,
          'a raster declaration past the bound clamps to it rather than ' \
          "rejecting, got #{state.fetch('graphics').map { |g| g['width'] }.inspect}")
    check(state['graphics_diagnostics'].nil?,
          "no image was rejected, got #{state['graphics_diagnostics'].inspect}")
  end
end

# The last band at the bound. 682 band advances put the next sixel's first
# bit at y=4092; bits 3 and 4 then land on 4095 and 4096. The old check
# (`y + 5 >= 512`) refused a band whose first bit alone was in range, so the
# equivalent stream failed one band early and lost the whole image.
verdict('sixel-last-band-at-the-bound') do
  scenario('last-band', [
    cup(1, 1), "\\033Pq#0;2;100;0;100#{'-' * 682}W\\033\\\\"
  ].join, images: 1) do |_bmp, state, _summary, _cells|
    tall = state.fetch('graphics').first
    check(!tall.nil?, 'the last band at the bound decodes')
    if tall
      check(tall.fetch('height') == 4096,
            "the last in-range row is kept and the next clipped, got height " \
            "#{tall.fetch('height')}")
    end
    check(state['graphics_diagnostics'].nil?,
          "no image was rejected, got #{state['graphics_diagnostics'].inspect}")
  end
end

# --- Issue #11: P2 background select ---
#
# `~?~` leaves the middle column unset inside the extent. P2=0, which is
# what a bare `DCS q` means, paints it with the current SGR background;
# P2=1 leaves it transparent and the cell behind shows through. The scratch
# buffer was always calloc'd, so before this every image behaved as P2=1.
verdict('sixel-background-select') do
  scenario('background-select', [
    "\\033[48;2;0;0;255m",
    cup(1, 1), "\\033Pq#1;2;100;0;0~?~\\033\\\\",
    cup(3, 1), "\\033P0;1;0q#1;2;100;0;0~?~\\033\\\\"
  ].join, images: 2) do |bmp, state, _summary, _cells|
    check(at(bmp, 0, 0) == RED && at(bmp, 2, 0) == RED,
          "the set columns are drawn, got #{at(bmp, 0, 0).inspect} " \
          "#{at(bmp, 2, 0).inspect}")
    check(at(bmp, 1, 0) == BLUE,
          'P2=0 fills an unset pixel with the current background, got ' \
          "#{at(bmp, 1, 0).inspect}")
    check(at(bmp, 1, 32) == BLACK,
          'P2=1 leaves an unset pixel transparent, got ' \
          "#{at(bmp, 1, 32).inspect}")
    opaque = state.fetch('graphics').map { |g| g['width'] }
    check(opaque.length == 2, "both forms decode, got #{opaque.inspect}")
    check(state['graphics_diagnostics'].nil?,
          "no image was rejected, got #{state['graphics_diagnostics'].inspect}")
  end
end

# --- Issue #11: the cursor contract and the two modes ---
#
# Sixel scrolling is the default (DECSDM reset). The cursor lands at the
# start of the line below the image, so two images in sequence stack rather
# than sharing one origin -- which is what they did when the decoder placed
# an image and returned without touching the cursor.
verdict('sixel-cursor-advance') do
  scenario('cursor-advance', [
    cup(1, 1), solid_sixel('', '#1;2;100;0;0'),
    solid_sixel('', '#2;2;0;100;0')
  ].join, images: 2) do |bmp, _state, summary, _cells|
    check(at(bmp, 0, 0) == RED,
          "the first image is at the cursor, got #{at(bmp, 0, 0).inspect}")
    check(at(bmp, 0, CELL_H) == GREEN,
          'the second image is one row below the first rather than on top of ' \
          "it, got #{at(bmp, 0, CELL_H).inspect}")
    cursor = summary.fetch('cursor')
    check(cursor.fetch('x').zero? && cursor.fetch('y') == 2,
          "the cursor is at the start of the line below the second image, " \
          "got #{cursor.inspect}")
  end
end

# 8452 leaves the cursor to the right of the graphic instead, on the image's
# last row.
verdict('sixel-cursor-right-of-graphic') do
  scenario('cursor-right-of-graphic', [
    "\\033[?8452h", cup(2, 3), solid_sixel('', '#3;2;0;0;100')
  ].join, images: 1) do |bmp, _state, summary, _cells|
    check(at(bmp, 16, CELL_H) == BLUE,
          "the image is at the cursor, got #{at(bmp, 16, CELL_H).inspect}")
    cursor = summary.fetch('cursor')
    check(cursor.fetch('x') == 3 && cursor.fetch('y') == 1,
          'DECSET 8452 leaves the cursor right of the graphic on its own row, ' \
          "got #{cursor.inspect}")
  end
end

# DECSDM *set* is display mode: the image is homed and the cursor is not
# touched. This polarity is DEC's and current xterm's -- xterm carried the
# inverse for years, so a client written against either must get the same
# answer from DECRQM, which terminal_conformance.rb asserts.
verdict('sixel-display-mode') do
  scenario('display-mode', [
    "\\033[?80h", cup(4, 10), solid_sixel('', '#2;2;0;100;0')
  ].join, images: 1) do |bmp, _state, summary, _cells|
    check(at(bmp, 0, 0) == GREEN,
          "DECSDM homes the image, got #{at(bmp, 0, 0).inspect}")
    check(at(bmp, 72, 48) != GREEN,
          'DECSDM does not also draw at the cursor, got ' \
          "#{at(bmp, 72, 48).inspect}")
    cursor = summary.fetch('cursor')
    check(cursor.fetch('x') == 9 && cursor.fetch('y') == 3,
          "DECSDM leaves the cursor alone, got #{cursor.inspect}")
  end
end

# An image that does not fit below the cursor scrolls the screen, and the
# image's own origin moves up with it. Three bands are 18 pixels, which is
# two cell rows; placed on the last row of a twelve-row surface, the screen
# scrolls twice and the image starts two rows higher.
bands = (['~'] * 8).join
verdict('sixel-scroll-at-the-bottom') do
  scenario('scroll-at-the-bottom', [
    cup(1, 1), 'A',
    cup(ROWS, 1),
    "\\033Pq#1;2;100;0;0#{bands}-#{bands}-#{bands}\\033\\\\"
  ].join, images: 1) do |bmp, state, summary, cells|
    image = state.fetch('graphics').first
    check(!image.nil?, 'the image decodes')
    if image
      check(image.fetch('height') == 18,
            "three bands are eighteen pixels tall, got #{image.fetch('height')}")
      check(image.fetch('y') == (ROWS - 3) * CELL_H,
            'the image origin moves up by the rows the screen scrolled, got ' \
            "#{image.fetch('y')}")
    end
    check(at(bmp, 0, (ROWS - 3) * CELL_H) == RED,
          'the scrolled image is drawn where its origin says, got ' \
          "#{at(bmp, 0, (ROWS - 3) * CELL_H).inspect}")
    grid = cells.fetch('cells').to_a.each_with_object({}) do |cell, acc|
      acc[[cell.fetch('x'), cell.fetch('y')]] = cell
    end
    check(grid[[0, 0]].nil? || grid[[0, 0]]['text'] != 'A',
          'the screen scrolled, so the first row is gone')
    cursor = summary.fetch('cursor')
    check(cursor.fetch('x').zero? && cursor.fetch('y') == ROWS - 1,
          'the cursor stays on the last row because the screen moved instead, ' \
          "got #{cursor.inspect}")
  end
end

# An image placed on an existing text row follows a later text scroll. This
# is distinct from the scroll the image itself can cause above: it exercises
# the graphics/text ownership boundary after placement.
verdict('sixel-scroll-after-placement') do
  scenario('scroll-after-placement', [
    cup(2, 1), solid_sixel('', '#1;2;100;0;0'), "\033[1S"
  ].join, images: 1) do |bmp, state, _summary, _cells|
    image = state.fetch('graphics').first
    check(!image.nil?, 'the placed image remains in the graphics state')
    if image
      check(image.fetch('y').zero?,
            "the image follows one later SU row, got #{image.fetch('y')}")
    end
    check(at(bmp, 0, 0) == RED,
          'the later text scroll moves the placed image pixels')
  end
end

# The published bound is the enforced bound. A client sizes its image to
# what it was told, so these must be the numbers XTSMGRAPHICS answers with
# and the numbers the decoder clips at.
verdict('sixel-published-bound') do
  graphics = json('/v1/diagnostics')['graphics']
  if graphics.nil?
    check(false, 'diagnostics publish no graphics bounds at all')
  else
    check(graphics.fetch('sixel_max_width') == 4096 &&
          graphics.fetch('sixel_max_height') == 4096,
          "diagnostics publish the sixel bound, got #{graphics.inspect}")
    check(graphics.fetch('sixel_max_width') *
          graphics.fetch('sixel_max_height') * 4 ==
          graphics.fetch('max_bytes'),
          'a full-size sixel canvas is exactly the graphics byte budget, ' \
          "got #{graphics.inspect}")
    check(graphics.fetch('sixel_color_registers') == 256,
          "256 colour registers, got #{graphics['sixel_color_registers']}")
  end
end

unless $failures.empty?
  warn "sixel-decoder gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'SIXEL_DECODER_PASS'
