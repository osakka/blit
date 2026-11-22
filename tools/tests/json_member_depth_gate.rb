#!/usr/bin/env ruby
# frozen_string_literal: true

# Security review SR-20 (Gitea #202): a request's fields are its own
# members, never a member of an object nested in it. The key scanner used to
# match a quoted name followed by `:` at any depth, so:
#
#   - `user` inside `environment` set the run user (as root the child ran as
#     that account; otherwise the request was refused 400);
#   - `command` inside `environment` stood in for a missing command
#     (the server before this change created the session: 201);
#   - an environment variable named `width` was read as the surface's width
#     (a string, so the request was refused 400);
#   - a cell whose text is "cursor" was taken for the cursor object, and the
#     next cell's x and y for its position (the cursor went to 0,1, not to
#     the update's own 3,2);
#   - on the stream, an input message whose data said "subscribe" was taken
#     for a subscription, and its data never reached the session.
#
# Each of these is now read from the object it belongs to, and the variables
# still reach the child as variables.

require 'json'
require 'base64'
require 'etc'
require 'securerandom'
require 'socket'
require 'rbconfig'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19552'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'PUT' => Net::HTTP::Put,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, (JSON.parse(text) rescue text)]
end

def check(ok, why)
  raise "JSON_MEMBER_DEPTH_FAIL #{why}" unless ok
end

def websocket_send_text(socket, payload)
  mask = SecureRandom.random_bytes(4)
  masked = payload.bytes.each_with_index.map { |byte, index| byte ^ mask.getbyte(index % 4) }.pack('C*')
  raise 'test payload too large' if payload.bytesize >= 126

  socket.write([0x81, 0x80 | payload.bytesize].pack('C*') + mask + masked)
end

def websocket_read_text(socket)
  first = socket.read(2).bytes
  length = first[1] & 0x7f
  length = socket.read(2).unpack1('n') if length == 126
  raise 'unexpected long websocket message' if length == 127

  socket.read(length)
end

Dir.mktmpdir('blit-json-member-depth') do |root|
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    # An account that is not the server's, so a child run as it would show.
    other = Process.uid.zero? ? 'nobody' : 'root'
    Etc.getpwnam(other)

    # 1. `user` only inside `environment`: the child runs as the server's
    #    user, with `user` a variable of its environment.
    out = File.join(root, 'user.json')
    code, body = request('POST', '/v1/terminal-sessions',
                         { command: RbConfig.ruby, surface: { width: 20, height: 4 },
                           environment: { 'user' => other },
                           arguments: ['-rjson', '-e',
                                       "File.write(#{out.inspect}, JSON.generate('uid' => Process.uid, " \
                                       "'user' => ENV['user'])); sleep 30"] })
    check(code == 201, "user inside environment -> #{code} #{body.inspect[0, 160]}")
    Timeout.timeout(10) { sleep 0.05 until File.size?(out) }
    seen = JSON.parse(File.read(out))
    check(seen['uid'] == Process.uid && seen['user'] == other,
          "user inside environment ran as uid #{seen['uid']} (server #{Process.uid}), user=#{seen['user'].inspect}")
    request('DELETE', "/v1/terminal-sessions/#{body.fetch('id')}")

    # 2. `command` only inside `environment` is no command.
    code, body = request('POST', '/v1/terminal-sessions',
                         { surface: { width: 20, height: 4 },
                           environment: { 'command' => '/bin/true' } })
    check(code == 400, "command inside environment -> #{code} #{body.inspect[0, 160]}")

    # 3. The geometry is the surface object's, whatever the environment says.
    code, body = request('POST', '/v1/terminal-sessions',
                         { command: '/bin/sleep', arguments: ['30'],
                           environment: { 'width' => '7', 'height' => '3' },
                           surface: { width: 20, height: 4 } })
    check(code == 201, "width inside environment -> #{code} #{body.inspect[0, 160]}")
    surface_path = "/v1/surfaces/#{body.fetch('surface_id')}/cells"
    session_id = body.fetch('id')
    code, cells = request('GET', surface_path)
    check(code == 200 && cells['width'] == 20 && cells['height'] == 4,
          "surface #{cells['width']}x#{cells['height']}, wanted 20x4")

    # 4. A cell whose text is "cursor" is a cell, and the update's cursor is
    #    the update's own member.
    code, body = request('PUT', surface_path,
                         { base_revision: cells['revision'],
                           cells: [{ x: 0, y: 0, text: 'cursor' }, { x: 0, y: 1, text: 'x' }],
                           cursor: { x: 3, y: 2, visible: true } })
    check(code == 200, "cell text \"cursor\" -> #{code} #{body.inspect[0, 160]}")
    code, cells = request('GET', surface_path)
    row = cells.fetch('cells').select { |c| c['y'] == 0 }.sort_by { |c| c['x'] }.map { |c| c['text'] }.join
    check(code == 200 && row.start_with?('cursor') && cells.dig('cursor', 'x') == 3 &&
          cells.dig('cursor', 'y') == 2, "after the update: row 0 #{row.inspect}, cursor #{cells['cursor'].inspect}")
    request('DELETE', "/v1/terminal-sessions/#{session_id}")

    # 5. A stream message is what its own `type` says: input whose data
    #    says "subscribe" is input.
    out = File.join(root, 'line')
    code, body = request('POST', '/v1/terminal-sessions',
                         { command: '/bin/sh', surface: { width: 20, height: 4 },
                           arguments: ['-c', "stty -echo; read line; printf %s \"$line\" > #{out}; sleep 30"] })
    check(code == 201, "stream session -> #{code}")
    session_id = body.fetch('id')
    sleep 0.3
    stream = TCPSocket.new(BASE.host, BASE.port)
    key = Base64.strict_encode64(SecureRandom.random_bytes(16))
    stream.write("GET /v1/stream HTTP/1.1\r\nHost: #{BASE.host}:#{BASE.port}\r\n" \
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n" \
                 "Sec-WebSocket-Key: #{key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
    headers = +''
    headers << stream.read(1) until headers.end_with?("\r\n\r\n")
    check(headers.start_with?('HTTP/1.1 101'), "stream upgrade #{headers.lines.first.inspect}")
    Timeout.timeout(5) { websocket_read_text(stream) } # stream_connected
    websocket_send_text(stream, JSON.generate('type' => 'input', 'session_id' => session_id.to_s,
                                              'data' => "subscribe\n"))
    reply = Timeout.timeout(5) { websocket_read_text(stream) }
    Timeout.timeout(5) { sleep 0.05 until File.size?(out) } rescue nil
    line = File.exist?(out) ? File.read(out) : nil
    stream.close
    check(reply.include?('input_forwarded') && line == 'subscribe',
          "stream input \"subscribe\" -> #{reply.inspect[0, 120]}, the session read #{line.inspect}")
    request('DELETE', "/v1/terminal-sessions/#{session_id}")

    puts 'JSON_MEMBER_DEPTH_PASS user_in_environment=ignored command_in_environment=400 ' \
         'width_in_environment=ignored cell_text_cursor=cell stream_input_subscribe=input'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
