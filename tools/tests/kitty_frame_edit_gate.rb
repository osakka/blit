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
#   * an edit damages only the cells its rectangle lands on, not the image's
#   * on the GPU compositor, an edited texture is refreshed only where it
#     changed (image_partial_uploads)
#   * Hibr's probe, exactly as it sends it: a q=0 a=t,t=d of a 1x1 image and
#     a q=0 a=f of it are each answered `i=4202;OK`, in order, and a q=2 a=f
#     is not answered at all (edits turn on at the two OKs)

require 'base64'
require 'json'
require 'socket'
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

# A transmission in 4096-byte base64 chunks, as Hibr sends them.
def chunked(keys, data)
  pieces = Base64.strict_encode64(data).scan(/.{1,4096}/)
  pieces.each_with_index.map do |piece, i|
    more = i < pieces.size - 1 ? 1 : 0
    esc(i.zero? ? "#{keys},m=#{more}" : "m=#{more},q=2", piece)
  end.join
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

def read_exact(socket, length, seconds)
  data = +''
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + seconds
  while data.bytesize < length
    remaining = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    raise Timeout::Error if remaining <= 0 || !IO.select([socket], nil, nil, remaining)
    data << socket.readpartial(length - data.bytesize)
  end
  data
end

def websocket_text(socket, seconds)
  header = read_exact(socket, 2, seconds).bytes
  length = header[1] & 0x7f
  length = read_exact(socket, 2, seconds).unpack1('n') if length == 126
  length = read_exact(socket, 8, seconds).unpack1('Q>') if length == 127
  read_exact(socket, length, seconds).force_encoding(Encoding::UTF_8)
end

def open_stream
  socket = TCPSocket.new('127.0.0.1', PORT)
  socket.write("GET /v1/stream HTTP/1.1\r\nHost: 127.0.0.1:#{PORT}\r\nConnection: Upgrade\r\n" \
               "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n" \
               "Sec-WebSocket-Key: #{['blit-frame-edit'].pack('m0')}\r\n\r\n")
  response = +''
  response << read_exact(socket, 1, 2) until response.include?("\r\n\r\n")
  raise 'websocket handshake failed' unless response.start_with?('HTTP/1.1 101')
  socket
end

# The largest damage, in cells, any surface_delta for `surface` reported
# while the stream was read.
def damage_seen(socket, surface, seconds)
  widest = [0, 0]
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + seconds
  loop do
    remaining = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    break if remaining <= 0
    text = websocket_text(socket, remaining) rescue break
    event = JSON.parse(text) rescue next
    next unless event['type'] == 'surface_delta' && event['surface_id'] == surface && event['damage']
    d = event['damage']
    widest = [d['width'], d['height']] if d['width'] * d['height'] > widest[0] * widest[1]
  end
  widest
end

def partial_uploads
  outputs = JSON.parse(request('GET', '/v1/diagnostics')[1]).dig('render', 'outputs') || []
  outputs.sum { |o| o.dig('compositor', 'image_partial_uploads').to_i }
end

def gpu_composing?
  outputs = JSON.parse(request('GET', '/v1/diagnostics')[1]).dig('render', 'outputs') || []
  outputs.any? { |o| o.dig('compositor', 'active') == 'gles' }
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
        esc('a=f,r=2,i=9,x=0,y=0,s=1,v=1,f=24,q=2', Base64.strict_encode64(solid([1, 2, 3], 1, 1))),
      # a 64x64 image at row 3: 8x4 cells of 8x16
      '\\033[4;1H' + chunked('a=T,f=24,s=64,v=64,i=10,q=2,C=1', solid([9, 9, 9], 64, 64)),
      # an 8x8 edit inside one cell's worth of it
      esc('a=f,r=1,i=10,x=40,y=36,s=8,v=8,f=24,q=2', Base64.strict_encode64(solid([200, 100, 50], 8, 8)))
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
    large = run.call(6)
    check(JSON.parse(request('GET', "/v1/terminal-sessions/#{session}")[1])['graphics']
            .any? { |g| g['id'] == 10 && g['complete'] }, "the 64x64 image did not arrive: #{large.inspect}")
    surface = JSON.parse(request('GET', "/v1/terminal-sessions/#{session}")[1]).fetch('surface_id')
    uploads_before = partial_uploads
    stream = open_stream
    File.write(File.join(root, 'go7'), '')
    damage = damage_seen(stream, surface, 2.0)
    stream.close
    check(File.exist?(File.join(root, 'done7')), 'the edit of the large image did not run')
    check(damage[0].positive? && damage[0] <= 2 && damage[1] <= 2,
          "an 8x8 edit of a 64x64 image damaged #{damage[0]}x#{damage[1]} cells; the image is 8x4")
    if gpu_composing?
      Timeout.timeout(5) { sleep 0.05 until partial_uploads > uploads_before }
    end
    # Hibr's probe, byte for byte, read back from a raw-mode session.
    replies = File.join(root, 'probe-replies')
    probe = esc('i=4202,s=1,v=1,a=t,t=d,f=24', 'AAAA') +
            esc('a=f,r=1,i=4202,x=0,y=0,s=1,v=1,f=24', 'AAAA') +
            esc('a=f,r=1,i=4202,x=0,y=0,s=1,v=1,f=24,q=2', 'AAAA')
    File.write(File.join(root, 'probe.sh'),
               "stty raw -echo; : > #{replies}; sleep 0.5; printf '%b' '#{probe}'; exec cat > #{replies}\n")
    code, = request('POST', '/v1/terminal-sessions',
                    { command: '/bin/sh', arguments: [File.join(root, 'probe.sh')],
                      surface: { width: 40, height: 4 }, display_id: display })
    check(code == 201, "probe session -> #{code}")
    sleep 2
    got = File.exist?(replies) ? File.binread(replies) : ''
    check(got == "\e_Gi=4202;OK\e\\" * 2,
          "the probe was answered #{got.inspect}; two i=4202;OK and nothing for q=2 turn edits on")
    puts 'KITTY_FRAME_EDIT_PASS replace=pixels+placement+screen edit=chunked+zlib blend=alpha overwrite=X1 ' \
         "refused=bounds+id+frame image_intact=yes damage=#{damage[0]}x#{damage[1]}_cells " \
         "gpu_partial=#{gpu_composing? ? 'yes' : 'cpu_compositor'} hibr_probe=OK,OK,silent"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
