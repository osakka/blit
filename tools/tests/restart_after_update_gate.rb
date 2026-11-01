#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0205: restart after an update, at a safe moment.
#
# The server runs from a mock install root (BLIT_INSTALL_ROOT, test mode):
# <root>/bin/blit-server, a copy of the build, and <root>/VERSION. The copy
# is started as the old release (BLIT_TEST_VERSION, read once and not passed
# on to a restart); an update is the binary replaced by rename, as dpkg
# does, and VERSION rewritten, so the new process is the build itself and
# reports its own version. The login screen is tools/tests/fixtures/
# login_identity.rb: `waiting` is the login screen with nobody signed in,
# `desktop` a verified sign-in. The restart is an exec, so the pid stays;
# what proves one is `performance.uptime_ms` falling.
#
#   signed in    pending, both versions, waiting_for `signed-in`, and no
#                restart in 15 s
#   manual       pending, the panel's `update` line says `restart to apply`,
#                and no restart in 15 s
#   at-login-screen, then: a user's session -> `user-session`; input ->
#                `input`; both gone and 10 s idle -> one restart, back as the
#                new version, nothing pending
#   a VERSION its binary does not carry -> one restart, then pending with
#                `restart-attempted`, and no second restart in 15 s

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(19493)
BASE = URI("http://127.0.0.1:#{PORT}")
TOKEN = 'update-restart-gate'
NEW = File.read(File.join(ROOT, 'packaging/VERSION')).strip
OLD = '0.0.1'
HOLD = 15

abort("restart after update gate: missing #{BINARY}") unless File.executable?(BINARY)
unless Process.uid.zero? || system('sudo', '-n', 'true', out: File::NULL, err: File::NULL)
  puts 'RESTART_AFTER_UPDATE_SKIP root_fixture=required'
  exit 77
end

def check(value, message) = (raise(message) unless value)

def request(method, path, body = nil, revision = nil)
  q = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'PUT' => Net::HTTP::Put,
        'DELETE' => Net::HTTP::Delete }.fetch(method).new(BASE + path)
  q['Authorization'] = "Bearer #{TOKEN}"
  q['Content-Type'] = 'application/json'
  q['If-Match'] = revision.to_s if revision
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(BASE.host, BASE.port, open_timeout: 2, read_timeout: 5) { |h| h.request(q) }
  [r.code.to_i, r.body.to_s.empty? ? nil : JSON.parse(r.body)]
end

def get(path)
  code, body = request('GET', path)
  raise "GET #{path}: #{code}" unless code == 200
  body
end

# nil while the server is between processes. One slow answer is asked
# again, so a loaded host is not taken for a restart.
def uptime_ms
  2.times do
    return get('/v1/diagnostics/runtime').dig('performance', 'uptime_ms')
  rescue StandardError
    sleep 0.2
  end
  nil
end

def update = get('/v1/diagnostics').fetch('update')

def wait_for(what, seconds = 12)
  last = nil
  Timeout.timeout(seconds) do
    loop do
      begin
        last = yield
        return last if last
      rescue StandardError => e
        last = e
      end
      sleep 0.2
    end
  end
rescue Timeout::Error
  raise "#{what} (last: #{last.inspect})"
end

def save(member, value)
  wait_for('settings stayed dirty') { get('/v1/settings').dig('storage', 'state') == 'clean' }
  settings = get('/v1/settings')
  document = settings['document']
  document['server'] = (document['server'] || {}).merge(value) if member == 'server'
  document[member] = value unless member == 'server'
  code, reply = request('PUT', '/v1/settings', { document: document }, settings['revision'])
  check(code == 200, "PUT #{member} #{value}: #{code} #{reply}")
end

# No restart for `seconds`: the uptime only grows.
def holds(seconds, what)
  first = uptime_ms
  check(first, "#{what}: the server is not answering")
  previous = first
  deadline = Time.now + seconds
  while Time.now < deadline
    sleep 0.5
    now = uptime_ms
    check(now && now >= previous, "#{what}: the server restarted (uptime #{previous} -> #{now.inspect})")
    previous = now
  end
  previous - first
end

# One restart: the uptime falls, and the server answers again.
def restarts(what, seconds = 30)
  before = uptime_ms
  wait_for("#{what}: no restart", seconds) do
    now = uptime_ms
    if now && before && now < before then now
    else
      before = now if now
      nil
    end
  end
end

Dir.mktmpdir('blit-restart-after-update') do |root|
  File.chmod(0o755, root)
  install = File.join(root, 'install')
  FileUtils.mkdir_p([File.join(install, 'bin'), File.join(root, 'input')])
  fixture = File.join(ROOT, 'tools/tests/fixtures/login_identity.rb')
  target = File.join(root, 'target.hibr')
  hook = File.join(root, 'blit-login-session.hibr')
  FileUtils.cp(File.join(ROOT, 'tools/tests/fixtures/login_identity_target.hibr'), target)
  FileUtils.cp(File.join(ROOT, 'hibr/blit-login-session.hibr'), hook)
  File.chmod(0o755, target)
  File.chmod(0o755, hook)
  display = gate_display_env
  pid = nil
  log = nil

  # As dpkg does: a new file renamed over the old, never written in place.
  write_version = lambda do |version|
    File.write("#{install}/VERSION.new", "#{version}\n")
    File.rename("#{install}/VERSION.new", "#{install}/VERSION")
  end
  replace_binary = lambda do
    FileUtils.cp(BINARY, "#{install}/bin/blit-server.new")
    File.chmod(0o755, "#{install}/bin/blit-server.new")
    File.rename("#{install}/bin/blit-server.new", "#{install}/bin/blit-server")
  end
  stop = lambda do
    next unless pid
    system('sudo', '-n', 'kill', '-TERM', pid.to_s, out: File::NULL, err: File::NULL)
    Timeout.timeout(10) { Process.wait(pid) } rescue nil
    pid = nil
  end
  at_exit { stop.call }
  start = lambda do |mode|
    replace_binary.call
    write_version.call(OLD)
    result = File.join(root, "#{mode}.result")
    File.write(result, '')
    File.chmod(0o666, result)
    settings = File.join(root, "#{mode}.settings.json")
    FileUtils.rm_f(settings)
    log = File.join(root, "#{mode}.log")
    env = { **display, 'BLIT_TEST_MODE' => '1', 'BLIT_INSTALL_ROOT' => install,
            'BLIT_TEST_VERSION' => OLD,
            'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
            'BLIT_INPUT_ROOT' => File.join(root, 'input'), 'BLIT_SETTINGS_PATH' => settings,
            'BLIT_LOGIN_AUTOSTART' => '1', 'BLIT_LOGIN_COMMAND' => fixture,
            'BLIT_LOGIN_SCRIPT' => '', 'BLIT_AUTH_TOKEN' => TOKEN,
            'BLIT_GATE_IDENTITY_MODE' => mode, 'BLIT_GATE_TARGET' => target,
            'BLIT_GATE_RESULT' => result, 'BLIT_GATE_HOOK' => hook }
    pid = spawn('sudo', '-n', 'env', *env.map { |k, v| "#{k}=#{v}" },
                "#{install}/bin/blit-server", PORT.to_s, out: log, err: log)
    wait_for('the server did not start', 15) { get('/v1/version') }
    check(update['running_version'] == OLD, "started as #{update['running_version']}, not #{OLD}")
    wait_for("the login screen (#{mode}) did not come up") do
      state = get('/v1/login').dig('identity', 'state')
      state == (mode == 'waiting' ? 'waiting' : 'verified')
    end
  end
  # The update: pending within one 5 s check, with both versions.
  updated = lambda do |version|
    replace_binary.call
    write_version.call(version)
    wait_for("no restart pending for #{version}") do
      u = update
      u['restart_pending'] && u['installed_version'] == version && u['executable_replaced'] && u
    end
  end
  results = []

  begin
    # Signed in: never.
    start.call('desktop')
    u = updated.call(NEW)
    check(u['running_version'] == OLD, "running_version #{u['running_version']}")
    wait_for('waiting_for does not say signed-in') { update['waiting_for'].include?('signed-in') }
    holds(HOLD, 'signed in')
    check(update['waiting_for'].include?('signed-in'), "signed in: waiting_for #{update['waiting_for']}")
    results << "signed_in=#{update['waiting_for'].join('+')}:held_#{HOLD}s"
    stop.call

    # Manual: never, and the panel says so.
    start.call('waiting')
    save('server', { 'restart_after_update' => 'manual' })
    check(get('/v1/settings').dig('effective', 'restart_after_update') == 'manual',
          'effective restart_after_update is not manual')
    save('hud', { 'enabled' => true, 'display_id' => 'all', 'sections' => %w[blit] })
    u = updated.call(NEW)
    check(u['restart_after_update'] == 'manual', "update.restart_after_update #{u['restart_after_update']}")
    line = "update  #{NEW} installed, restart to apply"
    wait_for("the panel has no '#{line}'") do
      get('/v1/diagnostics')['hud']['panels'].first['lines'].include?(line)
    end
    holds(HOLD, 'manual')
    results << "manual=held_#{HOLD}s panel='#{line}'"

    # At the login screen: what is missing, then the restart.
    save('server', { 'restart_after_update' => 'at-login-screen' })
    code, session = request('POST', '/v1/terminal-sessions', { command: '/bin/cat', surface: { width: 20, height: 4 } })
    check(code.between?(200, 299), "user session: #{code} #{session}")
    request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 10, y: 10 })
    input_at = Time.now
    u = wait_for('waiting_for does not say user-session and input') do
      w = update
      w['waiting_for'].include?('user-session') && w['waiting_for'].include?('input') && w
    end
    missing = u['waiting_for']
    line = "update  #{NEW} installed, at login screen"
    wait_for("the panel has no '#{line}'") do
      get('/v1/diagnostics')['hud']['panels'].first['lines'].include?(line)
    end
    request('DELETE', "/v1/terminal-sessions/#{session['id']}")
    restarts('idle at the login screen', 30)
    waited = (Time.now - input_at).round
    after = wait_for('the restarted server did not answer') { update }
    check(after['running_version'] == NEW && after['installed_version'] == NEW && !after['restart_pending'] &&
          after['waiting_for'].empty?, "after the restart: #{after}")
    check(waited >= 10, "restarted #{waited} s after the last input, before 10 s idle")
    results << "waiting_for=#{missing.join('+')} restarted_#{waited}s_after_input " \
               "back=#{after['running_version']}:pending=false"

    # A VERSION its binary does not carry: one attempt, then never again.
    stray = '9.9.99'
    replace_binary.call
    write_version.call(stray)
    restarts('the stray version', 30)
    again = wait_for('no restart-attempted after the attempt') do
      w = update
      w['restart_pending'] && w['waiting_for'].include?('restart-attempted') && w
    end
    check(again['running_version'] == NEW, "after the attempt: #{again}")
    holds(HOLD, 'after one attempt')
    results << "once_per_version=#{stray}:one_restart,held_#{HOLD}s"
    stop.call
  rescue StandardError => e
    warn File.read(log) if log && File.file?(log)
    raise e
  end
  puts "RESTART_AFTER_UPDATE_PASS #{results.join(' ')}"
end
