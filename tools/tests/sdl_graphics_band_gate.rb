#!/usr/bin/env ruby
# frozen_string_literal: true

# Pixel gate for Kitty z-band composition and background transparency
# (Gitea #84). The contract, as kitty defines it:
#   z < -2^30          under cell backgrounds
#   -2^30 <= z < 0     over backgrounds, under text
#   a cell whose resolved background is the terminal default is transparent
#   to an image below the text; an explicit colour is opaque, including an
#   explicit black on a terminal whose default is not black.
# Hibr's wallpaper is z = -2^30 - 1 and relies on all of it.

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'
require_relative 'sdl_test_profile'

port = gate_port(Integer(ENV.fetch('BLIT_BAND_GATE_PORT', '19231')))
base = URI("http://127.0.0.1:#{port}")
binary = ENV.fetch('BLIT_GATE_BINARY', 'build/blit/blit-server')
log = "/tmp/blit-band-gate-#{Process.pid}.log"
env = SdlTestProfile.environment
pid = Process.spawn(env, binary, port.to_s, out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def request(base, method, path, body = nil)
  klass = { get: Net::HTTP::Get, post: Net::HTTP::Post,
            delete: Net::HTTP::Delete }.fetch(method)
  uri = base + path
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                             read_timeout: 10) do |http|
    req = klass.new(uri)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.start_with?('2')
  response.body.nil? || response.body.empty? ? nil : JSON.parse(response.body)
end

def screenshot(base)
  response = Net::HTTP.get_response(base + "/v1/diagnostics/screenshots/#{DISPLAY_ID}")
  raise "screenshot HTTP #{response.code}" unless response.is_a?(Net::HTTPSuccess)
  response.body
end

# Count pixels of one RGB colour in a BMP.
def count_colour(bmp, rgb)
  raise 'not a BMP' unless bmp.byteslice(0, 2) == 'BM'
  offset = bmp.byteslice(10, 4).unpack1('V')
  width = bmp.byteslice(18, 4).unpack1('l<')
  height = bmp.byteslice(22, 4).unpack1('l<').abs
  bytes = bmp.byteslice(28, 2).unpack1('v') / 8
  stride = ((width * bytes + 3) / 4) * 4
  want = rgb.reverse.pack('C*')
  total = 0
  height.times do |row|
    line = bmp.byteslice(offset + row * stride, width * bytes)
    width.times { |x| total += 1 if line.byteslice(x * bytes, 3) == want }
  end
  total
end

Timeout.timeout(8) do
  loop do
    begin
      request(base, :get, '/v1/version')
      break
    rescue StandardError
      sleep 0.05
    end
  end
end

RED = [255, 0, 0].freeze
SdlTestProfile.verify!(request(base, :get, '/v1/diagnostics'))
DISPLAY_ID = SdlTestProfile.display_id!(request(base, :get, '/v1/displays'))
BLUE = [0, 0, 255].freeze
FACE = [0x10, 0x18, 0x20].freeze
ESC = '\\033'

def kitty(z, id)
  rgb = (RED * (4 * 8)).pack('C*')
  "#{ESC}_Ga=T,f=24,s=4,v=8,c=2,r=2,z=#{z},i=#{id},m=0,q=2;" \
    "#{Base64.strict_encode64(rgb)}#{ESC}\\\\"
end

# Runs one scenario in a fresh session and returns the screenshot once the
# image is complete and the output has had time to present it.
def scenario(base, setup:, image:, id:, after: '')
  command = "printf '%b' \"$1\"; printf '%b' \"$2\"; printf '%b' \"$3\"; sleep 30"
  created = request(base, :post, '/v1/terminal-sessions',
                    'display_id' => DISPLAY_ID,
                    'command' => '/bin/sh',
                    'arguments' => ['-c', command, 'sh', setup, image, after],
                    'surface' => { 'width' => 20, 'height' => 8 })
  session = created.fetch('id')
  Timeout.timeout(10) do
    loop do
      state = request(base, :get, "/v1/terminal-sessions/#{session}")
      break if state.fetch('graphics', []).any? do |g|
        g.fetch('id') == id && g.fetch('complete')
      end
      sleep 0.02
    end
  end
  sleep 0.4
  [screenshot(base), session]
end

results = {}
sessions = []
begin
  # Home cursor, so the 2x2-cell image lands at the origin.
  home = "#{ESC}[H"

  # 0. Reference: the whole image with a black default, nothing over it.
  shot, session = scenario(base, setup: home, image: kitty(-1_073_741_825, 1), id: 1)
  sessions << session
  full = count_colour(shot, RED)
  raise "reference image is not visible: #{full} red pixels" unless full > 100
  results[:reference] = full
  request(base, :delete, "/v1/terminal-sessions/#{session}")

  # A. Non-black default (OSC 11), image below the backgrounds. Every
  #    unstyled cell is the default, so the image shows through all of it.
  face_set = "#{ESC}]11;#101820#{ESC}\\\\"
  shot, session = scenario(base, setup: face_set + home,
                           image: kitty(-1_073_741_825, 2), id: 2)
  sessions << session
  got = count_colour(shot, RED)
  raise "A: image under default-background cells hidden by an opaque " \
        "default quad: #{got} of #{full} red pixels" unless got >= full - 4
  raise 'A: default background colour is not the floor of the surface' unless
    count_colour(shot, FACE) > 1000
  results[:a_default_transparent] = got
  request(base, :delete, "/v1/terminal-sessions/#{session}")

  # B. Same, with one explicit black cell over the image: that cell is
  #    opaque black, not the image and not the default.
  explicit_black = "#{ESC}[48;2;0;0;0m #{ESC}[0m"
  shot, session = scenario(base, setup: face_set + home + explicit_black + home,
                           image: kitty(-1_073_741_825, 3), id: 3)
  sessions << session
  got = count_colour(shot, RED)
  lost = full - got
  raise "B: explicit black cell was let through (lost #{lost} red pixels, " \
        "expected one cell, 100..160)" unless lost.between?(100, 160)
  results[:b_explicit_black_opaque] = lost
  request(base, :delete, "/v1/terminal-sessions/#{session}")

  # C. z = -1 is over backgrounds and under text: an image there covers a
  #    coloured cell, and the glyph is drawn on top of the image.
  blue_x = "#{ESC}[44mX#{ESC}[0m"
  shot, session = scenario(base, setup: home + blue_x + home,
                           image: kitty(-1, 4), id: 4)
  sessions << session
  got = count_colour(shot, RED)
  blue = count_colour(shot, [0, 0, 0xee])
  raise "C: coloured cell background painted over a z=-1 image " \
        "(#{full - got} red pixels lost, blue=#{blue})" unless got >= full - 80
  results[:c_middle_band] = got
  request(base, :delete, "/v1/terminal-sessions/#{session}")

  # D. OSC 11 / OSC 10 queries are always answered.
  command = "stty -icanon -echo min 0 time 5; printf '\\033]11;?\\033\\\\'; " \
            "dd if=/dev/tty bs=1 count=24 2>/dev/null | od -An -c | tr -d ' \\n'"
  created = request(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh', 'arguments' => ['-c', command],
                    'surface' => { 'width' => 80, 'height' => 4 })
  session = created.fetch('id')
  sessions << session
  text = nil
  Timeout.timeout(10) do
    loop do
      cells = request(base, :get, "/v1/surfaces/surface-#{session}/cells")
      text = cells.fetch('cells').select { |c| c['y'] == 0 }
                  .sort_by { |c| c['x'] }.map { |c| c['text'].to_s }.join
      break if text.include?('rgb')
      sleep 0.05
    end
  end
  raise "D: OSC 11 query on an unconfigured session got no usable reply: #{text.inspect}" unless
    text.include?('rgb:0000/0000/0000')
  results[:d_osc11_reply] = true

  puts "SDL_GRAPHICS_BAND_PASS #{results.inspect}"
ensure
  sessions.each do |s|
    begin
      request(base, :delete, "/v1/terminal-sessions/#{s}")
    rescue StandardError
      nil
    end
  end
end
