#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #32 / ADR 0063. An explicitly external Hibr surface's cells belong to
# its adapter owner. Native Hibr terminal desktops are covered by the join
# viewport gate and deliberately parse their PTY output.
#
# A desktop reattached with --resume came back blank and reported itself
# healthy: `hold attach` replays the held session's terminal stream, Blit's
# engine rendered that stream into a surface that already had a registered
# owner, and the result was 32,400 blank cells that every status check read as
# a full desktop. Two things are asserted here:
#
#   - a desktop's PTY output does not author its cells, so an unpainted
#     surface stays visibly unpainted instead of being filled with whatever
#     the stream contained; and
#   - an owner that registers and never paints is reported, so "running" stops
#     meaning "showing something".
#
# ADR 0057 declined a stub owner registration for its own evidence, because
# what mattered there was the absence of echo on a surface an owner is
# actively drawing. Here a stub owner is exactly the subject: the question is
# what Blit does when an owner registers and then draws nothing.

require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19266'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
# Must match BLIT_HIBR_UNPAINTED_US in the server.
UNPAINTED_SECONDS = 10

abort("desktop-repaint gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "desk-#{rand(1 << 30)}" if %w[POST PUT].include?(method)
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(method, path, body = nil)
  response = request(method, path, body)
  unless response.code.start_with?('2')
    abort("desktop-repaint gate: #{method} #{path} -> #{response.code} #{response.body}")
  end
  JSON.parse(response.body)
end

work = Dir.mktmpdir('blit-desktop-gate')
at_exit { FileUtils.rm_rf(work) rescue nil }
require 'fileutils'

# Blit decides a session is a desktop from the command's basename, so the
# fixture has to be called this. It records the credentials Blit puts in the
# child's environment -- the gate cannot read the child's environment, and
# stdout is precisely the channel under test -- then produces exactly the kind
# of terminal output a reattachment replays.
launcher = File.join(work, 'blit-session.hibr')
File.write(launcher, <<~'SCRIPT')
  #!/bin/sh
  printf '%s\n%s\n' "$BLIT_SESSION_TOKEN" "$BLIT_SURFACE_ID" > "$1"
  # What `hold attach` replays: a clear, a home, and a screenful of blanks.
  printf '\033[2J\033[H'
  i=0
  while [ $i -lt 40 ]; do printf 'STREAM REPLAY LINE %s\r\n' "$i"; i=$((i + 1)); done
  # exec without the session token, so Blit's owner rediscovery stops finding
  # a match. It then retries on its short interval and retires its bounded
  # window in about eight seconds -- before the unpainted threshold. That is
  # the live host's timing. A fixture that keeps matching holds the window
  # open for a minute instead, which is how a check coupled to that window
  # passed this gate and reported nothing on the host.
  exec env -u BLIT_SESSION_TOKEN sleep 120
SCRIPT
FileUtils.chmod(0o755, launcher)

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                 },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

failures = []

def desktop(launcher, work, name, extra = [])
  record = File.join(work, "#{name}.env")
  created = json('POST', '/v1/terminal-sessions',
                 'command' => launcher,
                 # The record path goes first so the fixture sees it as $1;
                 # Blit's desktop-role parsing scans every argument, so the
                 # order of the rest does not matter to it.
                 'arguments' => [record, '--session', name,
                                 '--name', 'display-1'] + extra,
                 'display_id' => 'display-1',
                 # Use explicit external mode because this gate tests the
                 # adapter-owner/unpainted diagnostic, not native PTY parsing.
                 'surface_mode' => 'external',
                 'surface' => { 'width' => 60, 'height' => 12 })
  token = nil
  Timeout.timeout(10) do
    loop do
      if File.file?(record) && File.read(record).lines.length >= 2
        token = File.read(record).lines.first.strip
        break
      end
      sleep 0.1
    end
  end
  [created, token]
end

def register_owner(created, token)
  request('POST', "/v1/terminal-sessions/#{created.fetch('id')}/hibr-owner",
          'pid' => created.fetch('process_id'), 'token' => token)
end

def attachment(id)
  json('GET', "/v1/terminal-sessions/#{id}").fetch('hibr_attachment')
end

# A desktop that registers an owner and draws nothing, which is the state a
# --resume reattachment lands in.
created, token = desktop(launcher, work, 'unpainted', ['--resume'])
abort('desktop-repaint gate: the fixture never reported its token') if token.nil?
registered = register_owner(created, token)
unless registered.code.start_with?('2')
  abort("desktop-repaint gate: owner registration #{registered.code} #{registered.body}")
end
sleep 2.5

surface = created.fetch('surface_id')
summary = json('GET', "/v1/surfaces/#{surface}/summary")
state = attachment(created.fetch('id'))

# The PTY produced a clear and forty lines. None of it may be in the surface.
if summary.fetch('non_empty_cells') != 0
  failures << "the desktop's PTY output authored #{summary.fetch('non_empty_cells')} " \
              'cells of an owned surface; its owner is the only writer'
end
unless state['resume_requested']
  failures << '--resume was not recorded for a blit-session.hibr launch, so a ' \
              'resumed desktop cannot say it is one'
end
failures << 'the owner did not register' unless state['owner_registered']
if state.key?('owner_painted')
  if state['owner_painted']
    failures << "owner_painted is true with #{state['owner_writes'].inspect} writes"
  end
else
  failures << 'the attachment does not report owner_painted, so a desktop ' \
              'that registered an owner and drew nothing is indistinguishable ' \
              'from one that drew a screenful'
end

# It must say so, rather than reporting a healthy blank desktop.
begin
  Timeout.timeout(UNPAINTED_SECONDS + 15) do
    loop do
      break if (json('GET', '/v1/diagnostics')['errors'] || [])
               .include?('hibr_desktop_unpainted')

      sleep 0.5
    end
  end
rescue Timeout::Error
  failures << 'a desktop whose owner registered and never painted was never ' \
              'reported: diagnostics errors stayed ' \
              "#{(json('GET', '/v1/diagnostics')['errors'] || []).inspect}"
end
logged = (json('GET', '/v1/diagnostics/runtime')['logs'] || [])
         .any? { |l| l['code'] == 'hibr_desktop_unpainted' }
failures << 'the unpainted desktop was not written to the runtime log' unless logged
# The report has to survive the owner-rediscovery window closing, which is
# the thing that silently broke this on the live host.
retired = attachment(created.fetch('id'))
if retired['reassert_attempts'].to_i < 32
  warn "desktop-repaint gate: note: the reassert window was still open " \
       "(#{retired['reassert_attempts']} attempts) when the report arrived, " \
       'so this run did not exercise the retired-window case'
end

# And an owner that does paint is counted, so the signal is not just "always on".
revision = json('GET', "/v1/surfaces/#{surface}/summary").fetch('revision')
painted = request('PUT', "/v1/surfaces/#{surface}/cells",
                  'base_revision' => revision,
                  'cells' => [{ 'x' => 0, 'y' => 0, 'text' => 'D',
                                'style' => { 'foreground' => '#ffffff',
                                             'background' => '#204060' } }])
unless %w[200 204].include?(painted.code)
  failures << "the owner's cell write was refused: #{painted.code} #{painted.body}"
end
sleep 0.5
after = attachment(created.fetch('id'))
unless after['owner_writes'].to_i >= 1 && after['owner_painted']
  failures << "an accepted owner write did not count: #{after.inspect}"
end

request('DELETE', "/v1/terminal-sessions/#{created.fetch('id')}")

unless failures.empty?
  failures.each { |f| warn "desktop-repaint gate: #{f}" }
  exit 1
end

puts 'DESKTOP_REPAINT_PASS pty_authored_cells=0 resume_recorded=yes ' \
     'unpainted_reported=yes owner_write_counted=yes'
