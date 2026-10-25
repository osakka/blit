#!/usr/bin/env ruby
# frozen_string_literal: true

# Black-box gate for the API-owned binary pixel-frame path. The server is
# started here so the gate is deterministic and does not depend on a shell
# wrapper or a second renderer.

require 'base64'
require 'json'
require 'net/http'
require 'securerandom'
require 'socket'
require 'tmpdir'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'test_profile'

external_base = ENV['BLIT_TEST_SERVER']
port = Integer(ENV.fetch('BLIT_GATE_PORT', '19208'))
# Only this gate's own server binds the port; against an external server
# the port is expected to be serving already.
port = gate_port(port) unless external_base
base = external_base ? URI(external_base) : URI("http://127.0.0.1:#{port}")
log = File.join(Dir.tmpdir, "blit-pixel-frame-#{Process.pid}.log")
pid = nil
unless external_base
  pid = Process.spawn(TestProfile.environment,
                      ENV.fetch('BLIT_GATE_BINARY','build/blit/blit-server'), port.to_s,
                      out: log, err: [:child, :out])
end

at_exit do
  if pid
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

def http_json(base, method, path, body = nil)
  klass = {get: Net::HTTP::Get, post: Net::HTTP::Post,
           delete: Net::HTTP::Delete}.fetch(method)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 3) do |http|
    request = klass.new(path)
    if body
      request['Content-Type'] = 'application/json'
      request.body = JSON.generate(body)
    end
    response = http.request(request)
    raise "HTTP #{response.code}: #{response.body}" unless response.code.start_with?('2')
    body = response.body.to_s
    body.empty? ? nil : JSON.parse(body)
  end
end

Timeout.timeout(8) do
  loop do
    begin
      http_json(base, :get, '/v1/version')
      break
    rescue StandardError
      sleep 0.1
    end
  end
end

TestProfile.verify!(http_json(base,:get,'/v1/diagnostics')) unless external_base
session = http_json(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh',
                    'surface_mode' => 'external',
                    'surface' => {'width' => 80, 'height' => 24})
display_id = http_json(base,:get,'/v1/displays').fetch('displays').first.fetch('id')
session_id = session.fetch('id')
socket = nil
at_exit do
  socket.close if socket && !socket.closed?
  begin
    http_json(base, :delete, "/v1/terminal-sessions/#{session_id}")
  rescue StandardError
    # The server may already have been stopped; the test result is reported
    # by the original failure and cleanup must remain best-effort.
  end
end
socket = TCPSocket.new(base.host, base.port)
key = Base64.strict_encode64(SecureRandom.random_bytes(16))
socket.write("GET /v1/stream HTTP/1.1\r\nHost: #{base.host}:#{base.port}\r\n" \
             "Upgrade: websocket\r\nConnection: Upgrade\r\n" \
             "Sec-WebSocket-Key: #{key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
headers = +' '
headers.clear
headers << socket.read(1) while !headers.end_with?("\r\n\r\n")
raise 'websocket upgrade failed' unless headers.start_with?('HTTP/1.1 101')

def websocket_send(socket, opcode, payload, final: true)
  first = (final ? 0x80 : 0) | opcode
  length = payload.bytesize
  header = [first]
  if length < 126
    header << (0x80 | length)
  elsif length <= 65_535
    header << (0x80 | 126)
  else
    raise 'test payload too large'
  end
  mask = SecureRandom.random_bytes(4)
  masked = payload.bytes.each_with_index.map { |byte, index| byte ^ mask.getbyte(index % 4) }.pack('C*')
  header_bytes = header.pack('C*')
  header_bytes += [length].pack('n') if length >= 126
  socket.write(header_bytes + mask + masked)
end

def websocket_read_text(socket)
  first = socket.read(2).bytes
  length = first[1] & 0x7f
  length = socket.read(2).unpack1('n') if length == 126
  raise 'unexpected long websocket response' if length == 127
  payload = socket.read(length)
  raise 'expected text websocket response' unless (first[0] & 0x0f) == 1
  payload
end

raise 'stream did not connect' unless websocket_read_text(socket).include?('stream_connected')
websocket_send(socket, 1, JSON.generate(
  'type' => 'subscribe', 'subscriptions' => ['frames']))
subscription_reply = nil
Timeout.timeout(3) do
  loop do
    subscription_reply = websocket_read_text(socket)
    break if subscription_reply.include?('subscription_received')
  end
end
websocket_send(socket, 1, JSON.generate(
  'type' => 'pixel_frame_begin', 'session_id' => session_id,
  'image_id' => 77, 'width' => 8, 'height' => 4, 'format' => 32,
  'x' => 3, 'y' => 5, 'z' => 4,
  'ack' => ENV['BLIT_PIXEL_ACK'] == '1'))
ready_reply = websocket_read_text(socket)
raise "pixel frame was not accepted: #{ready_reply.inspect}" unless ready_reply.include?('pixel_frame_ready')

pixels = 32.times.map do |index|
  [(index * 7) & 255, (index * 11) & 255, (index * 13) & 255, 255]
end.flatten.pack('C*')
half = pixels.bytesize / 2
acknowledge = ENV['BLIT_PIXEL_ACK'] == '1'
websocket_send(socket, 2, pixels.byteslice(0, half), final: false)
websocket_send(socket, 0, pixels.byteslice(half, pixels.bytesize - half), final: true)
websocket_read_text(socket) if acknowledge

target_frame_count = Integer(ENV.fetch('BLIT_PIXEL_FRAME_COUNT', '30'))
raise 'pixel frame count must be at least 2' if target_frame_count < 2
frame_count = 1
runtime_before = http_json(base, :get, '/v1/diagnostics/runtime').fetch('performance')
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
pace_hz = ENV['BLIT_PIXEL_PACE_HZ'] && Float(ENV['BLIT_PIXEL_PACE_HZ'])
pace_interval = pace_hz && pace_hz > 0 ? 1.0 / pace_hz : nil
(target_frame_count - 1).times do
  # The begin record negotiates the frame format once. Every final binary
  # continuation below is an independent complete frame.
  websocket_send(socket, 2, pixels, final: true)
  frame_count += 1
  websocket_read_text(socket) if acknowledge
  sleep pace_interval if pace_interval
end
elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 3
snapshot = nil
image = nil
begin
  snapshot = http_json(base, :get, "/v1/terminal-sessions/#{session_id}")
  image = snapshot.fetch('graphics').find { |candidate| candidate.fetch('id') == 77 }
  break if image && image.fetch('revision') >= frame_count + 1
  sleep 0.05
end while Process.clock_gettime(Process::CLOCK_MONOTONIC) < deadline
sleep 0.7
runtime = http_json(base, :get, '/v1/diagnostics/runtime').fetch('performance')
drain_deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 3
while runtime.fetch('pixel_frames_pending') > 0 &&
      Process.clock_gettime(Process::CLOCK_MONOTONIC) < drain_deadline
  sleep 0.05
  runtime = http_json(base, :get, '/v1/diagnostics/runtime').fetch('performance')
end
if runtime.fetch('pixel_frames_accepted') < frame_count
  raise "pixel frame acceptance counter is short: #{runtime.inspect} image=#{image.inspect}"
end
raise "pixel frame presentation backlog was not drained: #{runtime.inspect}" unless
  runtime.fetch('pixel_frames_pending').zero?

minimum_rendered = Integer(ENV.fetch('BLIT_PIXEL_MIN_RENDERED', '0'))
rendered = runtime.fetch('frames') - runtime_before.fetch('frames')
if minimum_rendered > 0 && rendered < minimum_rendered
  raise "paced pixel presentation was short: rendered=#{rendered} " \
        "minimum=#{minimum_rendered} runtime=#{runtime.inspect}"
end

raise 'pixel image missing from session snapshot' unless image
raise "pixel frame backlog was not drained: #{image.inspect}" unless image.fetch('revision') >= frame_count + 1
raise 'pixel image is incomplete' unless image.fetch('complete') && image.fetch('visible')
raise 'pixel image dimensions are wrong' unless image.values_at('width', 'height', 'format') == [8, 4, 32]
raise 'legacy placement is not source-sized' unless image.values_at('placement_width','placement_height') == [8,4]

# The same source buffer must fit the requested destination, including odd
# cell heights and same-source reuse. Source bytes must never be rescaled by
# changing the metadata that controls payload length.
colours=[[255,0,0],[0,255,0],[0,0,255],[255,255,0]]
checker=(0...4).flat_map{|y|(0...8).flat_map{|x| colours[(y/2)*2+x/4]+[255]}}.pack('C*')
[[8,16],[9,17],[12,23],[16,32],[8,16]].each do |cw,ch|
  websocket_send(socket, 1, JSON.generate(
    type:'pixel_frame_begin',session_id:session_id,image_id:77,
    width:8,height:4,format:32,x:3,y:5,
    placement_width:4*cw,placement_height:3*ch))
  Timeout.timeout(3) { loop { break if websocket_read_text(socket).include?('pixel_frame_ready') } }
  websocket_send(socket,2,checker)
  Timeout.timeout(3) do
    loop do
      placed=http_json(base,:get,"/v1/terminal-sessions/#{session_id}").fetch('graphics').find{|g|g['id']==77}
      break if placed && placed['complete'] && placed.values_at('placement_width','placement_height')==[4*cw,3*ch]
      sleep 0.02
    end
  end
  # Independently inspect presented quadrant colours, not just API metadata.
  Timeout.timeout(3) do
    loop do
      capture=Net::HTTP.get_response(base+"/v1/diagnostics/screenshots/#{display_id}")
      if capture.code=='409'
        sleep 0.02
        next
      end
      bmp=capture.body
      raise "not a BMP capture: #{capture.code}: #{bmp[0,400]}" unless bmp.start_with?('BM')
      offset=bmp.byteslice(10,4).unpack1('V')
      width=bmp.byteslice(18,4).unpack1('l<')
      height=bmp.byteslice(22,4).unpack1('l<')
      bytes=bmp.byteslice(28,2).unpack1('v')/8
      stride=((width*bytes+3)/4)*4
      samples=colours.each_index.map do |i|
        x=3+cw+(i%2)*2*cw; y=5+(3*ch)/4+(i/2)*(3*ch)/2
        row=height>0 ? height-1-y : y
        bmp.byteslice(offset+row*stride+x*bytes,3).bytes.reverse
      end
      outside=[[3+4*cw+1,5+ch],[3+cw,5+3*ch+1]].map do |x,y|
        row=height>0 ? height-1-y : y
        bmp.byteslice(offset+row*stride+x*bytes,3).bytes.reverse
      end
      break if samples==colours && outside==[[0,0,0],[0,0,0]]
      sleep 0.02
    end
  end
end

# Reject malformed/new partial geometry before disturbing the visible image.
[{placement_width:32}, {placement_width:0,placement_height:48},
 {placement_width:32.5,placement_height:48},
 {placement_width:65536,placement_height:48},
 {placement_width:'32',placement_height:48}].each do |invalid|
  websocket_send(socket,1,JSON.generate({type:'pixel_frame_begin',session_id:session_id,
    image_id:77,width:8,height:4,format:32}.merge(invalid)))
  reply=Timeout.timeout(3){loop{v=websocket_read_text(socket);break v if v.include?('pixel_frame_error')}}
  raise "invalid geometry accepted: #{reply}" unless reply.include?('invalid_begin')
end

unchanged=http_json(base,:get,"/v1/terminal-sessions/#{session_id}").fetch('graphics').find{|g|g['id']==77}
raise 'invalid geometry changed the retained placement' unless unchanged && unchanged['visible'] &&
  unchanged.values_at('x','y','placement_width','placement_height')==[3,5,32,48]
socket.close
socket = nil
puts format('PIXEL_FRAME_PASS frames=%d ingest_hz=%.3f accepted=%d rendered=%d render_ms=%.3f fragmented_rgba=32bytes reuse=true placement=source_independent_odd_even_pixels shrink=erased invalid=rejected',
            frame_count, frame_count / elapsed, runtime.fetch('pixel_frames_accepted'),
            rendered,
            runtime.fetch('last_render_ms'))
