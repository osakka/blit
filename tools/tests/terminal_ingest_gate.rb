#!/usr/bin/env ruby
# frozen_string_literal: true

# A session's output is taken in full buffers, not one 4 KiB PTY read per
# pass of the loop. A PTY hands over at most one line-discipline buffer per
# read; reading once per pass made a 6.4 MB kitty video frame cost about
# 1,560 passes, and held Hibr's video to about 8 frames a second on the
# owner's machine (2026-10-26: 4,103 bytes a read, 9,500 passes a second).
#
#   * three 1771x902 RGB kitty frames, chunked as Hibr sends them, all arrive
#     complete
#   * the bytes per read during the burst average at least 16 KiB
#   * a small write is still taken at once (the drain stops when the queue is
#     empty, it does not wait for a full buffer)

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19547'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
FRAME_W = 1771
FRAME_H = 902
FRAMES = 3

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : JSON.parse(text)]
end

def check(ok, why)
  raise "TERMINAL_INGEST_FAIL #{why}" unless ok
end

def performance
  request('GET', '/v1/diagnostics/observation')[1].dig('runtime', 'performance')
end

def images
  request('GET', '/v1/terminal-sessions')[1]['sessions'].flat_map { |s| s['graphics'] || [] }
end

Dir.mktmpdir('blit-terminal-ingest') do |root|
  stream = File.join(root, 'frames.kitty')
  File.open(stream, 'wb') do |out|
    FRAMES.times do |n|
      pixels = (Array.new(256) { |i| (n * 7 + i) & 255 }.pack('C*') * (FRAME_W * FRAME_H * 3 / 256 + 1))[0, FRAME_W * FRAME_H * 3]
      chunks = Base64.strict_encode64(pixels).scan(/.{1,4096}/)
      chunks.each_with_index do |chunk, j|
        more = j < chunks.size - 1 ? 1 : 0
        head = j.zero? ? "a=T,f=24,s=#{FRAME_W},v=#{FRAME_H},i=#{n + 1},q=2,m=#{more}" : "m=#{more}"
        out.write("\e_G#{head};#{chunk}\e\\")
      end
    end
  end
  env = gate_display_env(width: 1920, height: 1080).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    go = File.join(root, 'go')
    done = File.join(root, 'done')
    code, = request('POST', '/v1/terminal-sessions',
                    { command: '/bin/sh', display_id: 'display-1',
                      arguments: ['-c', "while [ ! -e #{go} ]; do sleep 0.05; done; cat #{stream}; : > #{done}; sleep 600"] })
    check(code == 201, "session -> #{code}")
    sleep 1.0
    before = performance
    File.write(go, '')
    Timeout.timeout(120) { sleep 0.1 until File.exist?(done) }
    Timeout.timeout(30) { sleep 0.1 until images.count { |g| g['complete'] } >= FRAMES }
    after = performance
    bytes = after['terminal_bytes'] - before['terminal_bytes']
    reads = after['terminal_reads'] - before['terminal_reads']
    check(bytes >= File.size(stream), "read #{bytes} of #{File.size(stream)} bytes")
    per_read = bytes / [reads, 1].max
    check(per_read >= 16 * 1024, "#{per_read} bytes a read over #{reads} reads: the loop takes one PTY buffer a pass")
    got = images.select { |g| g['complete'] }.map { |g| [g['width'], g['height']] }.uniq
    check(got == [[FRAME_W, FRAME_H]], "frames arrived as #{got}")

    # A small write is taken as soon as it is queued.
    code, small = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', display_id: 'display-1',
                            arguments: ['-c', "printf 'ingest-ok'; sleep 600"] })
    check(code == 201, "small session -> #{code}")
    Timeout.timeout(5) do
      loop do
        cells = request('GET', "/v1/surfaces/#{small['surface_id']}/cells")[1]['cells']
        break if cells.select { |c| c['y'].zero? }.map { |c| c['text'] }.join.include?('ingest-ok')
        sleep 0.05
      end
    end
    puts "TERMINAL_INGEST_PASS bytes=#{bytes} reads=#{reads} bytes_per_read=#{per_read} frames=#{FRAMES} small_write=prompt"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
