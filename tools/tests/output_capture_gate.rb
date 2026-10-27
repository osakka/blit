#!/usr/bin/env ruby
# frozen_string_literal: true

# A session's raw PTY output, captured on request: the bytes exactly as they
# arrived, before the parser, with each read's offset and time. Bounded by
# seconds and max_bytes, explicit, and freed on DELETE. Built to see which
# escape sequences a program sent -- e.g. a clear arriving while the pen held
# a colour -- which the cell snapshot cannot show.

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19539'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def check(ok, why)
  raise "OUTPUT_CAPTURE_FAIL #{why}" unless ok
end

Dir.mktmpdir('blit-output-capture') do |root|
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    trigger = File.join(root, 'go')
    script = "while [ ! -e #{trigger} ]; do sleep 0.05; done; " \
             "printf '\\033[48;2;30;30;33mFACE\\033[2J'; sleep 0.3; printf '\\033[0mPLAIN'; sleep 600"
    code, session = request('POST', '/v1/terminal-sessions',
                            { command: '/bin/sh', arguments: ['-c', script], display_id: 'display-1' })
    check(code == 201, "session -> #{code}")
    id = session['id']
    path = "/v1/terminal-sessions/#{id}/output-capture"

    code, = request('GET', path)
    check(code == 200, "GET before arming -> #{code}")
    check(request('GET', path)[1]['state'] == 'none', 'a capture exists before arming')
    check(request('POST', path, { seconds: 0 })[0] == 400, 'seconds 0 accepted')
    check(request('POST', path, { max_bytes: 3_000_000 })[0] == 400, 'max_bytes over 2 MiB accepted')
    check(request('POST', '/v1/terminal-sessions/999/output-capture', {})[0] == 404, 'unknown session accepted')

    code, armed = request('POST', path, { seconds: 5 })
    check(code == 202 && armed['state'] == 'armed', "arming -> #{code} #{armed}")
    File.write(trigger, '')
    sleep 1.0
    code, captured = request('GET', path)
    check(code == 200 && captured['state'] == 'armed', "reading while armed -> #{code} #{captured['state']}")
    bytes = Base64.decode64(captured['data_base64'])
    check(bytes.include?("\e[48;2;30;30;33mFACE\e[2J"), "the exact bytes are missing: #{bytes.inspect[0, 120]}")
    check(bytes.include?("\e[0mPLAIN"), "the second write is missing: #{bytes.inspect}")
    check(bytes.index("\e[2J") < bytes.index("\e[0m"), 'the order of writes was not kept')
    reads = captured['reads']
    check(reads.size >= 2 && reads.each_cons(2).all? { |a, b| b[0] > a[0] && b[1] >= a[1] },
          "reads are not ordered offsets and times: #{reads}")
    check(captured['bytes'] == bytes.bytesize && captured['truncated'] == false, 'byte count or truncation wrong')

    # Bounded: a small capture keeps its first bytes and says it truncated.
    code, = request('POST', path, { seconds: 5, max_bytes: 4 })
    check(code == 202, 're-arming')
    request('POST', "/v1/terminal-sessions/#{id}/input", { data: "x" })
    run2 = request('POST', '/v1/terminal-sessions', { command: '/bin/sh', arguments: ['-c', 'sleep 0.5; printf ABCDEFGHIJ; sleep 600'], display_id: 'display-1' })[1]
    path2 = "/v1/terminal-sessions/#{run2['id']}/output-capture"
    request('POST', path2, { seconds: 5, max_bytes: 4 })
    sleep 1.2
    small = request('GET', path2)[1]
    check(small['bytes'] <= 4 && small['truncated'] == true, "bounded capture: #{small}")

    code, = request('DELETE', path)
    check(code == 204 && request('GET', path)[1]['state'] == 'none', 'DELETE did not free the capture')
    puts "OUTPUT_CAPTURE_PASS bytes=exact order=kept reads=timed bounded=truncates delete=frees errors=400_404"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
