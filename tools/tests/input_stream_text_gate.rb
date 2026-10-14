#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0068. The server, not each stream consumer, owns printable-key
# resolution. Prove that a real input WebSocket frame carries per-device
# keymap, Shift and AltGr results, and that releases carry no printable text.

require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19283'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("input stream text gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = {'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
           'PATCH' => Net::HTTP::Patch}.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 5) { |http| http.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  raise "#{method} #{path} -> #{response.code} #{response.body}" unless
    response.code.start_with?('2')

  response.body.empty? ? nil : JSON.parse(response.body)
end

def read_exact(socket, length, timeout_seconds)
  data = +''
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout_seconds
  while data.bytesize < length
    remaining = deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    raise Timeout::Error if remaining <= 0
    raise Timeout::Error unless IO.select([socket], nil, nil, remaining)

    data << socket.readpartial(length - data.bytesize)
  end
  data
end

def websocket_text(socket, timeout_seconds = 3)
  header = read_exact(socket, 2, timeout_seconds).bytes
  length = header[1] & 0x7f
  length = read_exact(socket, 2, timeout_seconds).unpack1('n') if length == 126
  length = read_exact(socket, 8, timeout_seconds).unpack1('Q>') if length == 127
  read_exact(socket, length, timeout_seconds).force_encoding(Encoding::UTF_8)
end

def open_input_stream
  socket = TCPSocket.new('127.0.0.1', PORT)
  key = ['blit-input-text'].pack('m0')
  socket.write("GET /v1/stream?events=input HTTP/1.1\r\n" \
               "Host: 127.0.0.1:#{PORT}\r\nConnection: Upgrade\r\n" \
               "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n" \
               "Sec-WebSocket-Key: #{key}\r\n\r\n")
  response = +''
  response << read_exact(socket, 1, 3) until response.include?("\r\n\r\n")
  raise "stream handshake failed: #{response.lines.first}" unless
    response.start_with?('HTTP/1.1 101')

  socket
end

def inject(device, code, value)
  response = request('POST', '/v1/input/inject',
                     'kind' => 'key', 'code' => code, 'value' => value,
                     'device_id' => device)
  raise "inject #{code}/#{value} -> #{response.code} #{response.body}" unless
    response.code == '202'
end

def next_key(socket, code, pressed)
  Timeout.timeout(5) do
    loop do
      frame = JSON.parse(websocket_text(socket))
      event = frame['event'] || {}
      next unless frame['type'] == 'input' && event['kind'] == 'key'
      next unless event['code'] == code && event['pressed'] == pressed

      return event
    end
  end
end

keymap = {
  'base' => 'us',
  'keys' => {
    '21' => ['z', 'Z', '', ''],
    '16' => ['q', 'Q', '@', 'Ω']
  }
}

Dir.mktmpdir('blit-stream-text') do |input_root|
  File.write(File.join(input_root, 'event0'), '')
  pid = Process.spawn({'SDL_VIDEODRIVER' => 'dummy',
                       'BLIT_PSF_FONT' => FONT,
                       'BLIT_INPUT_ROOT' => input_root,
                       'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms')},
                      BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        begin
          break if request('GET', '/v1/version').code == '200'
        rescue StandardError
          nil
        end
        sleep 0.1
      end
    end
    device = 'input-node-event0'
    json('PATCH', "/v1/input/devices/#{device}",
         'configuration' => {'keymap' => keymap})
    stream = open_input_stream

    inject(device, 21, 1)
    plain = next_key(stream, 21, true)
    inject(device, 21, 0)
    release = next_key(stream, 21, false)

    inject(device, 42, 1) # left Shift
    next_key(stream, 42, true)
    inject(device, 21, 1)
    shifted = next_key(stream, 21, true)
    inject(device, 21, 0)
    next_key(stream, 21, false)
    inject(device, 42, 0)
    next_key(stream, 42, false)

    inject(device, 100, 1) # right Alt / AltGr
    next_key(stream, 100, true)
    inject(device, 42, 1)
    next_key(stream, 42, true)
    inject(device, 16, 1)
    unicode = next_key(stream, 16, true)
    inject(device, 16, 0)
    next_key(stream, 16, false)
    inject(device, 42, 0)
    next_key(stream, 42, false)
    inject(device, 100, 0)
    next_key(stream, 100, false)

    failures = []
    failures << "plain text=#{plain['text'].inspect}, wanted z" unless
      plain['text'] == 'z'
    failures << "shift text=#{shifted['text'].inspect}, wanted Z" unless
      shifted['text'] == 'Z'
    failures << "AltGr+Shift text=#{unicode['text'].inspect}, wanted Ω" unless
      unicode['text'] == 'Ω'
    failures << "release carried text=#{release['text'].inspect}" if
      release.key?('text')
    unless failures.empty?
      failures.each { |failure| warn "input stream text gate: #{failure}" }
      exit 1
    end
    puts 'INPUT_STREAM_TEXT_PASS plain=z shift=Z altgr_shift=Ω release_text=omitted'
  ensure
    stream&.close
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
