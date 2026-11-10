#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0198, revised: a desktop's views are one program. The owner's mini PC
# had Boxer's lock held by the HDMI view while the desktop's Release Pointer
# gave `?7701l` back on another view; the lock stayed on with nothing able to
# end it (the pointer is still while locked, so focus could not move), and
# the raw motion reports reached a window as typing. Here a primary and a
# --join of one fake desktop: the join takes the lock, the primary gives it
# back, and the lock must end, be logged once with its reason, and stop the
# raw reports. A session outside the desktop giving the lock back must not.
require 'json'
require 'net/http'
require 'fileutils'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19554'), 10))
ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
ON = "\e[?7701;1$y"
OFF = "\e[?7701;2$y"

def request(method, path, body = nil)
  uri = URI("http://127.0.0.1:#{PORT}#{path}")
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch, 'PUT' => Net::HTTP::Put,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2, read_timeout: 4) { |h| h.request(req) }
  [response.code.to_i, response.body.to_s.empty? ? {} : JSON.parse(response.body)]
end

def check(ok, why)
  raise "POINTER_LOCK_DESKTOP_FAIL #{why}" unless ok
end

def wait_until(what, seconds = 5)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  raise "POINTER_LOCK_DESKTOP_FAIL timed out waiting for #{what}"
end

def sent(file)
  File.exist?(file) ? File.binread(file) : ''
end

def locked?(id)
  request('GET', "/v1/terminal-sessions/#{id}")[1].dig('mouse_reporting', 'pointer_lock') == true
end

def performance
  request('GET', '/v1/diagnostics/runtime')[1].fetch('performance')
end

Dir.mktmpdir('blit-pointer-lock-desktop') do |root|
  desktop = File.join(root, 'desktop')
  File.write(desktop, "#!/bin/sh\nexec /bin/sh \"$@\"\n")
  FileUtils.chmod(0o755, desktop)
  log = File.join(root, 'server.log')
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = gate_spawn(env, BINARY, PORT.to_s, out: log, err: [:child, :out])
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    topology = request('GET', '/v1/topology')[1]
    panes = request('PUT', '/v1/topology', { revision: topology.fetch('revision'),
                                              layout: { columns: 1, rows: 2 } })[1]
            .fetch('panes').sort_by { |p| p.dig('pixel_region', 'y') }

    # A view of the desktop that copies its input to `file` after `script`.
    view = lambda do |file, script, pane, join|
      args = ['-c', "stty raw -echo; : > #{file}; sleep 1; printf '#{script}'; exec cat > #{file}",
              '--session', 'gate']
      args << '--join' if join
      code, body = request('POST', '/v1/terminal-sessions',
                           { command: desktop, arguments: args, pane_id: pane.fetch('id'),
                             surface: { width: 40, height: 10 } })
      check(code == 201, "view -> #{code} #{body}")
      body
    end
    primary_file = File.join(root, 'primary')
    join_file = File.join(root, 'join')
    primary = view.call(primary_file, '', panes[1], false)
    join = view.call(join_file, '\033[?7701h', panes[0], true)
    code, = request('PATCH', "/v1/terminal-sessions/#{join['id']}", { focused: true })
    check(code.between?(200, 299), "focus the join -> #{code}")
    wait_until('the join to hold the lock') { sent(join_file).include?(ON) || locked?(join['id']) }
    check(locked?(join['id']), 'the join did not take the lock')
    engages = performance['pointer_lock_engages']

    # Raw motion goes to the join while it holds the lock.
    5.times { request('POST', '/v1/input/inject', { kind: 'pointer_relative', dx: 4, dy: -2 }) }
    wait_until('raw motion to the join') { sent(join_file).match?(/\e\[<35;-?\d+;-?\d+M/) }

    # A session that is no view of the desktop gives the lock back: nothing.
    stranger_file = File.join(root, 'stranger')
    stranger = request('POST', '/v1/terminal-sessions',
                       { command: '/bin/sh', arguments: ['-c', "printf '\\033[?7701l'; exec cat > #{stranger_file}"],
                         surface: { width: 20, height: 4 } })[1]
    sleep 1.0
    check(locked?(join['id']), 'a session outside the desktop released its lock')

    # The primary gives it back: the lock the join holds ends.
    File.write(File.join(root, 'noop'), '')
    request('POST', "/v1/terminal-sessions/#{primary['id']}/input", { text: '' })
    code, = request('POST', '/v1/terminal-sessions',
                    { command: desktop, arguments: ['-c', "printf '\\033[?7701l'; sleep 2", '--session', 'gate', '--join'],
                      pane_id: panes[1].fetch('id'), surface: { width: 20, height: 4 } })
    check(code == 201, "a give-back view -> #{code}")
    wait_until('the desktop-view release') { !locked?(join['id']) }
    wait_until('the unasked notice to the holder') { sent(join_file).include?(OFF) }
    perf = performance
    check(perf['pointer_lock_engages'] == engages, "engages changed: #{perf['pointer_lock_engages']} vs #{engages}")
    check(perf.dig('pointer_lock_released', 'desktop_view') == 1,
          "not counted as desktop_view: #{perf['pointer_lock_released']}")
    wait_until('the journal lines') do
      text = File.read(log)
      text.include?('pointer lock engaged') && text.include?('reason=desktop-view')
    end

    # After the release, motion is no longer raw: nothing signed reaches the join.
    before = sent(join_file).length
    5.times { request('POST', '/v1/input/inject', { kind: 'pointer_relative', dx: -9, dy: -9 }) }
    sleep 0.5
    check(sent(join_file)[before..].to_s !~ /\e\[<\d+;-\d+/, 'signed motion after the release')
    request('DELETE', "/v1/terminal-sessions/#{stranger['id']}") if stranger
    puts 'POINTER_LOCK_DESKTOP_PASS view_release=desktop_view stranger=ignored notice=sent ' \
         "logged=engaged+released raw_after_release=none engages=#{perf['pointer_lock_engages']}"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
