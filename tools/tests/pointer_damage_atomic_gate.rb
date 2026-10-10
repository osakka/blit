#!/usr/bin/env ruby
# frozen_string_literal: true

# Regression gate for the SDL path where pointer state and terminal damage
# arrive in one frame. The renderer updates the cached scene damage and
# composes the pointer after copying that complete scene; it must preserve
# untouched pixels atomically.

require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require_relative 'gate_port'

port = gate_port(Integer(ENV.fetch('BLIT_POINTER_DAMAGE_PORT', '19211')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-pointer-damage-#{Process.pid}.log"
pid = Process.spawn({'SDL_VIDEODRIVER' => 'dummy',
                     'BLIT_RENDERER' => 'software',
                     'LD_LIBRARY_PATH' => 'build/sdl3-kms'},
                    'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

def request(base, method, path, body = nil)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 5) do |http|
    req = method.new(path)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    response = http.request(req)
    raise "HTTP #{response.code} for #{path}: #{response.body}" unless
      response.is_a?(Net::HTTPSuccess)
    response.body.to_s.empty? ? nil : JSON.parse(response.body)
  end
end

def screenshot(base)
  Net::HTTP.start(base.host, base.port, open_timeout: 2, read_timeout: 8) do |http|
    response = http.request(Net::HTTP::Get.new('/v1/diagnostics/screenshots/display-1'))
    raise "screenshot HTTP #{response.code}" unless response.is_a?(Net::HTTPSuccess)
    response.body
  end
end

def pixel(bmp, x, y)
  raise 'not a BMP' unless bmp.byteslice(0, 2) == 'BM'
  offset = bmp.byteslice(10, 4).unpack1('V')
  width = bmp.byteslice(18, 4).unpack1('l<')
  height = bmp.byteslice(22, 4).unpack1('l<')
  bits = bmp.byteslice(28, 2).unpack1('v')
  raise "unsupported BMP depth #{bits}" unless [24, 32].include?(bits)
  bytes = bits / 8
  stride = ((width * bytes + 3) / 4) * 4
  row = height - y - 1
  bmp.byteslice(offset + row * stride + x * bytes, bytes).bytes
end

begin
  at_exit do
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
  Timeout.timeout(8) do
    loop do
      begin
        request(base, Net::HTTP::Get, '/v1/version')
        break
      rescue StandardError
        sleep 0.05
      end
    end
  end

  session = request(base, Net::HTTP::Post, '/v1/terminal-sessions',
                    'command' => '/bin/sh',
                    'arguments' => ['-c', 'sleep 3'],
                    'surface_mode' => 'external',
                    'surface' => {'width' => 8, 'height' => 4})
  id = session.fetch('id')
  path = "/v1/surfaces/surface-#{id}/cells"
  cells = (0...4).flat_map do |y|
    (0...8).map do |x|
      {'x' => x, 'y' => y, 'text' => ' ',
       'style' => {'foreground' => '#ffffff', 'background' => '#203040'}}
    end
  end
  request(base, Net::HTTP::Put, path,
          'base_revision' => 1, 'presentation' => 'defer', 'cells' => cells)
  request(base, Net::HTTP::Put, path,
          'base_revision' => 2, 'presentation' => 'commit',
          'cells' => [{'x' => 0, 'y' => 0, 'text' => ' ',
                       'style' => {'foreground' => '#ffffff',
                                   'background' => '#203040'}}])
  sleep 0.2
  # These requests deliberately remain adjacent. Depending on socket arrival
  # order, one attempt may be rendered in a separate loop turn; repeat the
  # same bounded experiment until the trace proves the intended overlap rather
  # than making scheduling luck part of the acceptance result.
  race = false
  revision = 3
  8.times do |attempt|
    cell_x = attempt + 1
    cell = {'x' => cell_x, 'y' => 0, 'text' => 'X',
            'style' => {'foreground' => '#ffffff',
                        'background' => '#405060'}}
    deferred = request(base, Net::HTTP::Put, path,
                       'base_revision' => revision,
                       'presentation' => 'defer', 'cells' => [cell])
    commit = {'base_revision' => deferred.fetch('revision'),
              'presentation' => 'commit', 'cells' => [cell]}
    pointer_socket = TCPSocket.new('127.0.0.1', port)
    update_socket = TCPSocket.new('127.0.0.1', port)
    pointer_payload = JSON.generate('kind' => 'pointer_motion',
                                    'x' => 300 + attempt * 20,
                                    'y' => 300 + attempt * 20)
    update_payload = JSON.generate(commit)
    pointer_request = "POST /v1/input/inject HTTP/1.1\r\n" \
                      "Host: 127.0.0.1\r\nConnection: close\r\n" \
                      "Content-Type: application/json\r\n" \
                      "Content-Length: #{pointer_payload.bytesize}\r\n\r\n" \
                      "#{pointer_payload}"
    update_request = "PUT #{path} HTTP/1.1\r\n" \
                     "Host: 127.0.0.1\r\nConnection: close\r\n" \
                     "Content-Type: application/json\r\n" \
                     "Content-Length: #{update_payload.bytesize}\r\n\r\n" \
                     "#{update_payload}"
    pointer_thread = Thread.new { pointer_socket.write(pointer_request) }
    update_thread = Thread.new { update_socket.write(update_request) }
    [pointer_thread, update_thread].each(&:join)
    pointer_response = pointer_socket.read
    update_response = update_socket.read
    pointer_socket.close
    update_socket.close
    raise 'pointer request failed' unless pointer_response.start_with?('HTTP/1.1 2')
    raise 'commit update failed' unless update_response.start_with?('HTTP/1.1 2')
    revision = JSON.parse(update_response.split("\r\n\r\n", 2).last).fetch('revision')
    trace = request(base, Net::HTTP::Get,
                    '/v1/diagnostics/trace?kind=render_path&limit=128')
    race = trace.fetch('events').any? do |event|
      detail = event.fetch('detail')
      detail.include?('path=incremental-damage') &&
        detail.include?('target_changed=1') &&
        detail.include?('pointer_dirty=1')
    end
    break if race
  end
  sleep 0.2
  bmp = screenshot(base)
  # The scene begins at (4,4), with 8x16 cells. Both samples are outside the
  # changed cell and must retain the committed background.
  first = pixel(bmp, 10, 10)
  second = pixel(bmp, 50, 50)
  raise 'pointer-plus-damage incremental boundary was not observed' unless race
  raise "scene was partially cleared: #{first.inspect} #{second.inspect}" if
    first[0, 3] == [0, 0, 0] || second[0, 3] == [0, 0, 0]
  puts "POINTER_DAMAGE_ATOMIC_PASS pixels=#{first[0, 3].join(',')},#{second[0, 3].join(',')}"
ensure
  begin
    request(base, Net::HTTP::Delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
