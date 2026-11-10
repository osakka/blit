#!/usr/bin/env ruby
# frozen_string_literal: true

# Gate servers are sealed from the host they run on (tools/tests/gate_env.rb,
# gate_env.py, gate_env.sh). Run from inside a Blit session, gates used to hand
# their servers the session's BLIT_AUTH_TOKEN, BLIT_DISPLAY_BACKEND, ... -- a
# gate's unauthenticated calls then failed -- and a server started without
# BLIT_INPUT_ROOT opened the owner's real keyboard and mouse.
#
# This runs with a polluted environment, as a Blit session would leave it,
# whose BLIT_INPUT_ROOT names a directory of fabricated devices: a server that
# saw it would list them. It proves the rule of each language's helper on the
# environment it produces, and then end to end on a real server started by
# Ruby (gate_spawn) and by sh (gate_with_server.sh): no inherited variable
# reached it, its API answers without a token, and it has no input device.

require 'json'
require 'net/http'
require 'open3'
require 'shellwords'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT', File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
FOREIGN = /\A(?:BLIT_|HIBR_|LISTEN_)|\A(?:WAYLAND_DISPLAY|WAYLAND_SOCKET|NOTIFY_SOCKET)\z/

def check(ok, why)
  return if ok

  warn "GATE_ENV_FAIL #{why}"
  exit 1
end

abort("gate env gate: missing #{BINARY}") unless File.executable?(BINARY)

fake = Dir.mktmpdir('blit-gate-env-fake')
at_exit { FileUtils.rm_rf(fake) }
%w[event0 event1].each { |node| File.write(File.join(fake, node), '') }
JUNK = {
  'BLIT_AUTH_TOKEN' => 'junk', 'BLIT_API_TOKEN' => 'junk', 'BLIT_EXPECT_DESKTOP' => '1',
  'BLIT_BIND_ADDRESS' => '0.0.0.0', 'BLIT_DISPLAY_BACKEND' => 'junk', 'BLIT_KMS_COMPOSITOR' => 'junk',
  'BLIT_SESSION_ID' => 'junk', 'BLIT_SERVER_PORT' => '1', 'BLIT_INPUT_ROOT' => fake,
  'WAYLAND_DISPLAY' => 'junk', 'HIBR_HOLD' => '/nonexistent', 'LISTEN_FDS' => '3',
  'NOTIFY_SOCKET' => '/nonexistent'
}.freeze
JUNK.each { |name, value| ENV[name] = value }
ENV['BLIT_GATE_SERVER_ENV'] = 'BLIT_GATE_ENV_OPERATOR=added BLIT_GATE_ENV_BOTH=operator'

# The rule, on an environment a helper produced. `gate` is what the gate set.
def sealed(env, gate, name, input_root: true)
  stray = env.keys.grep(FOREIGN) - gate.keys - %w[BLIT_INPUT_ROOT BLIT_GATE_ENV_OPERATOR BLIT_GATE_ENV_BOTH]
  check(stray.empty?, "#{name} let inherited #{stray.sort.join(',')} through")
  check(env['PATH'] == ENV['PATH'] && env['HOME'] == ENV['HOME'], "#{name} dropped PATH or HOME")
  gate.each { |k, v| check(env[k] == v, "#{name}: the gate's #{k}=#{v} became #{env[k].inspect}") }
  return unless input_root

  root = env['BLIT_INPUT_ROOT'].to_s
  check(root != fake_root && File.directory?(root) && Dir.empty?(root),
        "#{name}: BLIT_INPUT_ROOT #{root.inspect} is not an empty directory of its own")
  check(env['BLIT_GATE_ENV_OPERATOR'] == 'added', "#{name} dropped BLIT_GATE_SERVER_ENV's words")
end

def fake_root = JUNK['BLIT_INPUT_ROOT']

gate = { 'BLIT_GATE_ENV_BOTH' => 'gate', 'BLIT_PSF_FONT' => 'font' }

# Ruby: what gate_spawn hands the child, read back from the child itself.
out, status = Open3.capture2(gate_server_env(gate), '/usr/bin/env', '-0', unsetenv_others: true)
check(status.success?, 'env under gate_server_env failed')
sealed(out.split("\0").to_h { |pair| pair.split('=', 2) }, gate, 'gate_env.rb')
reader, writer = IO.pipe
pid = gate_spawn(gate, '/usr/bin/env', '-0', out: writer)
writer.close
spawned = reader.read.split("\0").to_h { |pair| pair.split('=', 2) }
Process.wait(pid)
sealed(spawned, gate, 'gate_spawn')
argv = gate_sudo_argv(gate, 'blit-server', '1')
JUNK.each_key { |name| check(argv.each_cons(2).include?(['-u', name]), "gate_sudo_argv does not unset #{name}") }
check(argv.include?('BLIT_GATE_ENV_BOTH=gate') && argv.last(2) == %w[blit-server 1],
      "gate_sudo_argv: #{argv.inspect}")

# Python: server_env and process_env.
python = <<~PY
  import json, sys
  sys.dont_write_bytecode = True
  sys.path.insert(0, sys.argv[1])
  import gate_env
  print(json.dumps([gate_env.server_env(dict(BLIT_GATE_ENV_BOTH='gate'), BLIT_PSF_FONT='font'),
                    gate_env.process_env(BLIT_FRAME_TOKEN='t')]))
PY
out, status = Open3.capture2('python3', '-c', python, File.join(ROOT, 'tools/tests'))
check(status.success?, "the Python helper failed: #{out}")
server, process = JSON.parse(out)
# A Python helper's directory is gone once its process is: judge the rest.
check(server['BLIT_INPUT_ROOT'].to_s.include?('blit-gate-no-input'), "gate_env.py input root #{server['BLIT_INPUT_ROOT']}")
sealed(server, gate, 'gate_env.py server_env', input_root: false)
check(server['BLIT_GATE_ENV_OPERATOR'] == 'added', 'gate_env.py dropped BLIT_GATE_SERVER_ENV')
sealed(process, { 'BLIT_FRAME_TOKEN' => 't' }, 'gate_env.py process_env', input_root: false)
check(!process.key?('BLIT_GATE_ENV_OPERATOR'), 'gate_env.py process_env took the server-only words')

# sh: env $GATE_SERVER_ENV, read back from the child, checked while it exists.
sh = <<~'SH'
  . "$1/tools/tests/gate_env.sh"
  gate_env_setup
  env $GATE_SERVER_ENV BLIT_GATE_ENV_BOTH=gate BLIT_PSF_FONT=font /usr/bin/env -0 >"$2"
  ls -A "$GATE_ENV_INPUT" >"$2.ls"
  input=$GATE_ENV_INPUT
  gate_env_cleanup
  [ ! -e "$input" ]
SH
Dir.mktmpdir('blit-gate-env-sh') do |work|
  file = File.join(work, 'env')
  _, status = Open3.capture2('sh', '-c', sh, 'sh', ROOT, file)
  check(status.success?, 'gate_env.sh failed or left its input directory behind')
  child = File.read(file).split("\0").to_h { |pair| pair.split('=', 2) }
  check(File.read("#{file}.ls").empty?, "gate_env.sh's input directory was not empty")
  check(child['BLIT_INPUT_ROOT'].to_s.include?('blit-gate-no-input'), "gate_env.sh input root #{child['BLIT_INPUT_ROOT']}")
  sealed(child, gate, 'gate_env.sh', input_root: false)
  check(child['BLIT_GATE_ENV_OPERATOR'] == 'added', 'gate_env.sh dropped BLIT_GATE_SERVER_ENV')
end

# End to end: a real server, as the gate would see it from the inside. Run by
# both starters below; reads the server's own environment and devices.
PROBE = <<~'RB'
  require 'json'
  require 'net/http'
  base, pid, fake = ARGV
  environ = File.binread("/proc/#{pid}/environ").split("\0").to_h { |pair| pair.split('=', 2) }
  leaked = %w[BLIT_AUTH_TOKEN BLIT_API_TOKEN BLIT_EXPECT_DESKTOP BLIT_BIND_ADDRESS BLIT_SESSION_ID
              BLIT_SERVER_PORT WAYLAND_DISPLAY HIBR_HOLD LISTEN_FDS NOTIFY_SOCKET].select { |k| environ.key?(k) }
  abort "leaked into the server: #{leaked.join(',')}" unless leaked.empty?
  abort "server BLIT_INPUT_ROOT #{environ['BLIT_INPUT_ROOT']}" if environ['BLIT_INPUT_ROOT'].to_s.empty? ||
                                                                 environ['BLIT_INPUT_ROOT'] == fake
  abort "server display backend #{environ['BLIT_DISPLAY_BACKEND']}" unless environ['BLIT_DISPLAY_BACKEND'] == 'kms'
  response = Net::HTTP.get_response(URI("#{base}/v1/input/devices"))
  abort "unauthenticated /v1/input/devices -> #{response.code}" unless response.code == '200'
  count = JSON.parse(response.body).fetch('devices').length
  abort "the server has #{count} input devices" unless count.zero?
  opened = Dir.glob("/proc/#{pid}/fd/*").map { |fd| File.readlink(fd) rescue '' }
  abort "the server holds #{opened.grep(%r{/dev/input/}).join(',')}" unless opened.grep(%r{/dev/input/}).empty?
  puts 'sealed'
RB

port = gate_port(19840)
base = "http://127.0.0.1:#{port}"
log = File.join(Dir.tmpdir, "blit-gate-env-#{Process.pid}.log")
server = gate_spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0',
                      'BLIT_SETTINGS' => 'ignore' }, BINARY, port.to_s, out: log, err: %i[child out])
begin
  Timeout.timeout(15) do
    loop do
      break if (Net::HTTP.get_response(URI("#{base}/v1/version")).code == '200' rescue false)
      check(!Process.wait(server, Process::WNOHANG), "the gate_spawn server exited: #{File.read(log) rescue ''}")
      sleep 0.1
    end
  end
  out, status = Open3.capture2e('ruby', '-e', PROBE, base, server.to_s, fake)
  check(status.success? && out.strip == 'sealed', "gate_spawn server: #{out.strip}")
ensure
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
  FileUtils.rm_f(log)
end

# gate_with_server.sh takes the port before the offset, like every gate port.
out, status = Open3.capture2e('sh', File.join(ROOT, 'tools/tests/gate_with_server.sh'), '19841',
                              'sh', '-c', 'exec ruby -e "$1" "$BLIT_TEST_SERVER" "$BLIT_SOAK_PID" "$2"',
                              'sh', PROBE, fake)
check(status.success? && out.strip.end_with?('sealed'), "gate_with_server.sh server: #{out.strip}")

puts 'GATE_ENV_PASS ruby=gate_spawn,gate_sudo_argv python=server_env,process_env sh=GATE_SERVER_ENV ' \
     'servers=gate_spawn,gate_with_server.sh inherited=none input_devices=0'
