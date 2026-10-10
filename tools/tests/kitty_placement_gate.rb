#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #15 / ADR 0060. Kitty placement keys must change the pixels, not just
# be accepted. A client that sends w, h, X, Y or p used to get an OK and the
# whole uncropped image at the wrong place, so this gate reads the
# framebuffer rather than the state: state records what was asked, pixels
# record what happened.
#
# The source image is 8x8 in four 4x4 quadrants of distinct colours, so any
# crop, offset or wrong placement shows up as a specific colour in a specific
# place and nowhere else.

require 'base64'
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19241'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
CELL_W = 8
CELL_H = 16
RED = [255, 0, 0].freeze
GREEN = [0, 255, 0].freeze
BLUE = [0, 0, 255].freeze
YELLOW = [255, 255, 0].freeze

abort("kitty-placement gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "kitty-placement-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(path)
  JSON.parse(request('GET', path).body)
end

def capture
  uri = BASE + '/v1/diagnostics/screenshots/display-1'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 60) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  abort("kitty-placement gate: screenshot #{response.code}") unless response.code == '200'
  response.body
end

def pixel(bmp, width, height, x, y)
  stride = (width * 3 + 3) & ~3
  offset = 54 + (height - 1 - y) * stride + x * 3
  [bmp.getbyte(offset + 2), bmp.getbyte(offset + 1), bmp.getbyte(offset)]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "kitty-placement gate: FAIL #{message}"
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'SDL_RENDERER' => 'software',
                 'BLIT_PSF_FONT' => FONT,
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

quadrants = String.new
8.times do |row|
  8.times do |column|
    quadrants << (row < 4 ? (column < 4 ? RED : GREEN) : (column < 4 ? BLUE : YELLOW)).pack('C3')
  end
end
payload = Base64.strict_encode64(quadrants)
common = 'f=24,s=8,v=8,q=2'

def esc(keys, data = '')
  "\\033_G#{keys};#{data}\\033\\\\"
end

def cup(row, column)
  "\\033[#{row};#{column}H"
end

# Cell columns are 8 px wide: column 1 is x=0, column 11 is x=80, and so on.
phase_a = [
  "\\033[?25l",
  # crop: the green quadrant only, from source origin (4,0)
  cup(1, 1), esc("a=T,#{common},i=1,x=4,y=0,w=4,h=4,C=1", payload),
  # sub-cell offset: the blue quadrant placed 3 px right and 2 px down
  cup(1, 11), esc("a=T,#{common},i=2,x=0,y=4,w=4,h=4,X=3,Y=2,C=1", payload),
  # numbered placement 1 of image 3: red; placement 2 of it: yellow
  cup(1, 21), esc("a=T,#{common},i=3,p=1,x=0,y=0,w=4,h=4,C=1", payload),
  cup(1, 31), esc('a=p,i=3,p=2,x=4,y=4,w=4,h=4,C=1'),
  # no C=1: the cursor moves past the 1x1-cell image
  cup(5, 1), esc("a=T,#{common},i=4", payload)
].join
phase_b = [
  cup(8, 1), esc("a=T,#{common},i=5,C=1", payload),
  esc('a=d,d=i,i=3,p=1')
].join

Dir.mktmpdir('kitty-placement') do |dir|
  flag = File.join(dir, 'go')
  File.write(File.join(dir, 'a'), phase_a)
  File.write(File.join(dir, 'b'), phase_b)
  script = "printf '%b' \"$(cat #{dir}/a)\"; while [ ! -e #{flag} ]; do sleep 0.05; done; " \
           "printf '%b' \"$(cat #{dir}/b)\"; sleep 60"
  created = JSON.parse(request('POST', '/v1/terminal-sessions',
    'command' => '/bin/sh', 'arguments' => ['-c', script],
    'surface' => { 'width' => 60, 'height' => 12 },
    'display_id' => 'display-1').body)
  abort("kitty-placement gate: create failed #{created}") unless created['id']
  session = created.fetch('id')
  surface = created.fetch('surface_id')

  Timeout.timeout(10) do
    sleep 0.1 until json("/v1/terminal-sessions/#{session}")['graphics'].to_a.length >= 4
  end
  sleep 1.5

  inset = json('/v1/diagnostics').fetch('render').fetch('surface_inset_px')
  fb_w = Integer(ENV.fetch('BLIT_GATE_FB_WIDTH', '1024'), 10)
  fb_h = Integer(ENV.fetch('BLIT_GATE_FB_HEIGHT', '768'), 10)
  bmp = capture
  at = ->(x, y) { pixel(bmp, fb_w, fb_h, inset + x, inset + y) }
  block = lambda do |x0, y0, w, h|
    (y0...(y0 + h)).flat_map { |y| (x0...(x0 + w)).map { |x| at.call(x, y) } }
  end
  background = at.call(300, 150)
  solid = ->(x0, y0, colour) { block.call(x0, y0, 4, 4).all? { |p| p == colour } }

  # crop
  check(solid.call(0, 0, GREEN), 'w/h/x/y crop: 4x4 green at the cell origin')
  check(at.call(4, 0) == background && at.call(0, 4) == background,
        'crop: nothing drawn outside the 4x4 source rectangle')
  # sub-cell offset
  check(solid.call(80 + 3, 2, BLUE), 'X/Y: blue block 3 px right, 2 px down in its cell')
  check(at.call(80 + 2, 2) == background && at.call(80 + 3, 1) == background,
        'X/Y: nothing at the cell origin')
  # two placements of one image
  check(solid.call(160, 0, RED), 'p=1: red placement')
  check(solid.call(240, 0, YELLOW), 'p=2: yellow placement of the same image')

  state = json("/v1/terminal-sessions/#{session}")
  image3 = state.fetch('graphics').find { |g| g['id'] == 3 }
  check(image3 && image3.fetch('placements').map { |p| p['id'] }.sort == [1, 2],
        'diagnostics list both numbered placements')
  cursor = json("/v1/surfaces/#{surface}/summary").fetch('cursor')
  check(cursor['x'] == 1 && cursor['y'] == 5,
        "a=T without C=1 moves the cursor right 1 column and down 1 row, got #{cursor.inspect}")

  File.write(flag, '')
  Timeout.timeout(10) do
    sleep 0.1 until json("/v1/terminal-sessions/#{session}")['graphics']
                      .find { |g| g['id'] == 3 }.fetch('placements').length == 1
  end
  sleep 1.5
  cursor = json("/v1/surfaces/#{surface}/summary").fetch('cursor')
  check(cursor['x'] == 0 && cursor['y'] == 7,
        "C=1 leaves the cursor where the CUP put it, got #{cursor.inspect}")
  bmp = capture
  check(block.call(160, 0, 4, 4).none? { |p| p == RED },
        'a=d,d=i,i=3,p=1 removed the red placement')
  check(solid.call(240, 0, YELLOW), 'the other placement of image 3 survives')
  check(solid.call(0, 0, GREEN), 'unrelated images survive a placement delete')
  request('DELETE', "/v1/terminal-sessions/#{session}")
end

unless $failures.empty?
  warn "kitty-placement gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'KITTY_PLACEMENT_PASS'
