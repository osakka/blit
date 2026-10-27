#!/usr/bin/env ruby
# frozen_string_literal: true

# A font change must not shake a running Hibr desktop (blit#136).
#
# Every layout resize -- a font change reflowing the pane, a new grid --
# used to start the attach repair loop again: up to 32 passes, 250 ms
# apart, each pushing the terminal one row shorter and back. A desktop that
# was already running and drawing took every one as a resize, cleared and
# repainted, for eight seconds after each font change, and a click landing
# in one was lost. A resize that changed the geometry has already signalled
# the program through TIOCSWINSZ, so nothing more is owed.
#
# This runs a Hibr session that counts the SIGWINCH it receives, makes two
# font changes, and counts what arrives in the three seconds after. The
# real resizes are a handful; the old loop delivered dozens.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19257'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
HIBR = ENV.fetch('BLIT_GATE_HIBR', '/usr/bin/hibr')
BASE = URI("http://127.0.0.1:#{PORT}")
LIMIT = 4

abort("layout reassert gate: missing #{BINARY}") unless File.executable?(BINARY)
abort("layout reassert gate: missing #{HIBR}") unless File.executable?(HIBR)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) { |h| h.request(req) }
  [response.code.to_i, (JSON.parse(response.body) rescue nil)]
end

def count(file)
  File.exist?(file) ? File.read(file).to_i : 0
end

failures = []
result = nil
Dir.mktmpdir('blit-reassert') do |root|
  counter = File.join(root, 'winch')
  script = File.join(root, 'count.hibr')
  File.mkfifo(File.join(root, 'idle'))
  File.write(script, <<~HIBR)
    n=0
    trap 'n=$((n + 1)); printf "%s\\n" "$n" > #{counter}' WINCH
    exec 3<> #{File.join(root, 'idle')}
    while :; do read -t 0.05 -u 3 x; done
  HIBR
  server = spawn({ **gate_display_env,
                   'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0',
                   'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'),
                   'BLIT_INPUT_ROOT' => File.join(root, 'input'),
                   },
                 BINARY, PORT.to_s, out: File.join(root, 'server.log'), err: %i[child out])
  Dir.mkdir(File.join(root, 'input'))
  begin
    Timeout.timeout(15) { sleep 0.1 until (request('GET', '/v1/version').first == 200 rescue false) }
    code, session = request('POST', '/v1/terminal-sessions',
                            { 'command' => HIBR, 'arguments' => [script],
                              'pane_id' => 'pane-display-1-1',
                              'environment' => { 'HOME' => root } })
    raise "session -> #{code}" unless code.between?(200, 299)

    # Past the startup repair, whatever it does for this session.
    Timeout.timeout(30) do
      loop do
        mine = request('GET', '/v1/terminal-sessions')[1]['sessions'].find { |s| s['id'] == session['id'] }
        break if mine && mine.dig('hibr_attachment', 'settled') != false

        sleep 0.2
      end
    end
    sleep 1
    before = count(counter)
    [19, 20].each do |px|
      code, = request('PUT', '/v1/fonts', { 'size_px' => px })
      failures << "PUT /v1/fonts #{px} -> #{code}" unless code == 200
      sleep 0.3
    end
    sleep 3
    got = count(counter) - before
    failures << "#{got} SIGWINCH in the 3.6 s after two font changes, wanted at most #{LIMIT}" if got > LIMIT
    result = got
    request('DELETE', "/v1/terminal-sessions/#{session['id']}")
  ensure
    Process.kill('TERM', server) rescue nil
    Timeout.timeout(10) { Process.wait(server) } rescue nil
  end
end

if failures.empty?
  puts "LAYOUT_REASSERT_PASS winch_after_two_font_changes=#{result} limit=#{LIMIT}"
else
  failures.each { |f| warn "LAYOUT_REASSERT_FAIL #{f}" }
  exit 1
end
