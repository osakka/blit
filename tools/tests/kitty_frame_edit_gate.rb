#!/usr/bin/env ruby
# frozen_string_literal: true

# Kitty graphics: changing an image already on screen (ADR 0200).
#
#   * a=T with the id of a complete image replaces its pixels, keeps its
#     placement, and the screen shows the new pixels (it used to be refused,
#     and the refusal dropped the image)
#   * a=f,r=1 writes a rectangle into the root frame: chunked (m=1), and
#     zlib-compressed (o=z); RGBA blends by alpha, X=1 overwrites
#   * the screen shows each edit with no new a=p
#   * an edit outside the image, on an unknown image, or on another frame is
#     refused and leaves the image exactly as it was

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require 'zlib'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19555'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
SIZE = 16

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  [response.code.to_i, response.body]
end

def check(ok, why)
  raise "KITTY_FRAME_EDIT_FAIL #{why}" unless ok
end

def esc(keys, data = '')
  "\\033_G#{keys};#{data}\\033\\\\"
end

def solid(rgb, width, height)
  (rgb.pack('C*') * (width * height)).b
end

# The expected image, edited in Ruby the way the protocol says.
def paint(image, patch, x, y, width, patch_width, from, blend)
  out = image.dup
  patch.bytes.each_slice(from).each_with_index do |px, i|
    row, column = i.divmod(patch_width)
    at = ((y + row) * width + x + column) * 3
    alpha = from == 4 ? px[3] : 255
    3.times do |c|
      out.setbyte(at + c, !blend || alpha == 255 ? px[c] :
                          (px[c] * alpha + out.getbyte(at + c) * (255 - alpha) + 127) / 255)
    end
  end
  out
end

def image_state(session)
  _, body = request('GET', "/v1/terminal-sessions/#{session}")
  JSON.parse(body).fetch('graphics').find { |g| g['id'] == 9 }
end

def screen_pixel(display, x, y)
  _, bmp = request('GET', "/v1/diagnostics/screenshots/#{display}")
  offset = bmp[10, 4].unpack1('V')
  width, height = bmp[18, 8].unpack('l<l<')
  row = height.positive? ? height - 1 - y : y
  stride = (width * 4 + 3) & ~3
  b, g, r = bmp[offset + row * stride + x * 4, 3].bytes
  [r, g, b]
end

Dir.mktmpdir('blit-kitty-frame-edit') do |root|
  env = gate_display_env(width: 320, height: 192).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    display = JSON.parse(request('GET', '/v1/displays')[1]).fetch('displays').first.fetch('id')
    red = solid([255, 0, 0], SIZE, SIZE)
    blue = solid([0, 0, 255], SIZE, SIZE)
    green = solid([0, 255, 0], 8, 8)
    half = ([255, 255, 0, 128].pack('C*') * 16).b        # 4x4 yellow at half alpha
    over = ([255, 255, 255, 128].pack('C*') * 4).b       # 2x2 white, alpha ignored by X=1
    steps = [
      esc("a=T,f=24,s=#{SIZE},v=#{SIZE},i=9,q=2,C=1", Base64.strict_encode64(red)),
      esc("a=T,f=24,s=#{SIZE},v=#{SIZE},i=9,q=2,C=1", Base64.strict_encode64(blue)),
      # chunked: the green patch in two pieces
      esc('a=f,r=1,i=9,x=4,y=4,s=8,v=8,f=24,q=2,m=1', Base64.strict_encode64(green[0, 96])) +
        esc('m=0,q=2', Base64.strict_encode64(green[96..])),
      esc('a=f,r=1,i=9,x=0,y=0,s=4,v=4,f=32,o=z,q=2', Base64.strict_encode64(Zlib::Deflate.deflate(half))),
      esc('a=f,r=1,i=9,x=14,y=14,s=2,v=2,f=32,X=1,q=2', Base64.strict_encode64(over)),
      esc('a=f,r=1,i=9,x=12,y=12,s=8,v=8,f=24,q=2', Base64.strict_encode64(solid([1, 2, 3], 8, 8))) +
        esc('a=f,r=1,i=77,x=0,y=0,s=1,v=1,f=24,q=2', Base64.strict_encode64(solid([1, 2, 3], 1, 1))) +
        esc('a=f,r=2,i=9,x=0,y=0,s=1,v=1,f=24,q=2', Base64.strict_encode64(solid([1, 2, 3], 1, 1)))
    ]
    steps.each_with_index { |text, i| File.write(File.join(root, "step#{i}"), text) }
    script = steps.each_index.map do |i|
      "while [ ! -e #{root}/go#{i} ]; do sleep 0.05; done; printf '%b' \"$(cat #{root}/step#{i})\"; : > #{root}/done#{i}"
    end.join('; ') + '; sleep 60'
    File.write(File.join(root, 'run.sh'), "printf '\\033[?25l'; #{script}\n")
    code, body = request('POST', '/v1/terminal-sessions',
                         { command: '/bin/sh', arguments: [File.join(root, 'run.sh')],
                           surface: { width: 40, height: 12 }, display_id: display })
    check(code == 201, "session -> #{code} #{body}")
    session = JSON.parse(body).fetch('id')
    run = lambda do |i|
      File.write(File.join(root, "go#{i}"), '')
      Timeout.timeout(5) { sleep 0.05 until File.exist?(File.join(root, "done#{i}")) }
      sleep 0.4
      image_state(session)
    end
    first = run.call(0)
    check(first && first['complete'] && first['pixel_crc32'] == Zlib.crc32(red), "first image #{first}")
    check(screen_pixel(display, 2, 2) == [255, 0, 0], "screen shows #{screen_pixel(display, 2, 2)} not red")

    replaced = run.call(1)
    check(replaced && replaced['pixel_crc32'] == Zlib.crc32(blue),
          "replacement: #{replaced.inspect} (a refusal drops the image)")
    check(replaced['visible'] && replaced['x'] == first['x'] && replaced['y'] == first['y'],
          'the replacement lost its placement')
    check(screen_pixel(display, 2, 2) == [0, 0, 255], 'the screen still shows the old pixels')

    expected = paint(blue, green, 4, 4, SIZE, 8, 3, true)
    edited = run.call(2)
    check(edited['pixel_crc32'] == Zlib.crc32(expected), 'chunked edit wrote the wrong pixels')
    check(screen_pixel(display, 6, 6) == [0, 255, 0] && screen_pixel(display, 2, 2) == [0, 0, 255],
          'the screen does not show the edit')

    expected = paint(expected, half, 0, 0, SIZE, 4, 4, true)
    blended = run.call(3)
    check(blended['pixel_crc32'] == Zlib.crc32(expected), 'the compressed alpha edit did not blend')

    expected = paint(expected, over, 14, 14, SIZE, 2, 4, false)
    overwritten = run.call(4)
    check(overwritten['pixel_crc32'] == Zlib.crc32(expected), 'X=1 did not overwrite')

    rejections = lambda do
      JSON.parse(request('GET', "/v1/terminal-sessions/#{session}")[1]).dig('graphics_diagnostics', 'rejected').to_i
    end
    rejected_before = rejections.call
    refused = run.call(5)
    check(refused['pixel_crc32'] == Zlib.crc32(expected), 'a refused edit changed the image')
    check(rejections.call == rejected_before + 3,
          "out-of-bounds, unknown-id and other-frame edits were not all refused: #{rejections.call - rejected_before}")
    puts 'KITTY_FRAME_EDIT_PASS replace=pixels+placement+screen edit=chunked+zlib blend=alpha overwrite=X1 ' \
         'refused=bounds+id+frame image_intact=yes'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
