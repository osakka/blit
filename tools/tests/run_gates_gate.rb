#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0216. The runner is measured, bounded and parallel; this proves it on a
# fake suite (BLIT_GATE_FAKE_SUITE), not on the real gates, so it takes seconds
# and fails for the runner's reasons only:
#
#   - two workers with different BLIT_GATE_PORT_OFFSET values do not collide on
#     a fixed gate port, and the same offset still does (Ruby and sh helpers);
#   - the runner hands each worker its own offset, and the timing lane the base;
#   - a hung gate is reported TIMEOUT, is a failure, keeps its output, and
#     leaves no process behind -- not even a blit-server it had started;
#   - BLIT_GATE_JOBS=1 and 4 report the same gates, in the same order, with the
#     same results and the same RUN_GATES line, and 4 really runs gates at once;
#   - the timing lane runs alone: no parallel gate overlaps a serial one, and
#     serial gates do not overlap each other.

require 'fileutils'
require 'open3'
require 'socket'
require 'tmpdir'

ROOT = File.expand_path('../..', __dir__)
RUNNER = File.join(ROOT, 'tools/run-gates.sh')
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))

def check(condition, message)
  raise message unless condition
end

def serving?(port)
  TCPSocket.new('127.0.0.1', port, connect_timeout: 1).close
  true
rescue StandardError
  false
end

# A port in the gap above the gates' own range, with its +1000 neighbour free.
def free_pair
  (19_901..19_999).each do |port|
    return port unless serving?(port) || serving?(port + 1000)
  end
  raise 'no free port pair for the offset test'
end

def run_ruby(code, env)
  out, status = Open3.capture2e(env, 'ruby', '-e', code)
  [out, status]
end

dir = Dir.mktmpdir('blit-run-gates-gate')
at_exit { FileUtils.rm_rf(dir) }

# --- ports ---------------------------------------------------------------

port = free_pair
holder = TCPServer.new('127.0.0.1', port)
ruby_probe = "require '#{ROOT}/tools/tests/gate_port.rb'; puts gate_port(#{port}, 'probe')"
out, = run_ruby(ruby_probe, 'BLIT_GATE_PORT_OFFSET' => '1000', 'BLIT_GATE_PORT_TIMEOUT' => '1')
check(out.strip == (port + 1000).to_s,
      "an offset of 1000 must move #{port} to #{port + 1000} without waiting: #{out}")
out, status = run_ruby(ruby_probe, 'BLIT_GATE_PORT_OFFSET' => '0', 'BLIT_GATE_PORT_TIMEOUT' => '1')
check(!status.success? && out.include?('still serving'),
      "the same offset must still collide and say so: #{out}")
sh_probe = ". #{ROOT}/tools/tests/gate_port.sh; gate_port #{port} probe"
out, status = Open3.capture2e({ 'BLIT_GATE_PORT_OFFSET' => '1000', 'BLIT_GATE_PORT_TIMEOUT' => '1' },
                              'sh', '-c', sh_probe)
check(status.success? && out.strip == (port + 1000).to_s, "sh gate_port ignored the offset: #{out}")
out, status = Open3.capture2e({ 'BLIT_GATE_PORT_OFFSET' => '0', 'BLIT_GATE_PORT_TIMEOUT' => '1' },
                              'sh', '-c', sh_probe)
check(!status.success? && out.include?('still serving'), "sh gate_port must collide at offset 0: #{out}")
holder.close

# --- the fake suite --------------------------------------------------------

stamp = File.join(dir, 'stamp.sh')
File.write(stamp, <<~'SH')
  #!/bin/sh
  # stamp.sh NAME SECONDS: record start and end, sleep in between.
  now() { date +%s%N; }
  printf 'start %s %s\n' "$1" "$(now)" >>"$RUNS/stamps"
  printf '%s offset=%s\n' "$1" "${BLIT_GATE_PORT_OFFSET:-unset}" >>"$RUNS/offsets"
  sleep "$2"
  printf 'end %s %s\n' "$1" "$(now)" >>"$RUNS/stamps"
  echo "$1 done"
SH
File.chmod(0o755, stamp)

server_port = free_pair
unique = "4#{Process.pid}" # an unlikely sleep duration, in seconds
suite = File.join(dir, 'suite.sh')
def pgate(n)
  "    run_gate p#{n} \"$STAMP\" p#{n} 0.8\n"
end
body = +"suite_fake() {\n"
(1..4).each { |n| body << pgate(n) }
body << "    run_serial_gate s1 \"$STAMP\" s1 1\n"
body << "    run_gate fails sh -c 'echo \"went wrong\"; exit 3'\n"
body << "    skip_gate skipped \"stated reason\"\n"
body << "    run_gate na sh -c 'echo \"not here\"; exit 77'\n"
(5..8).each { |n| body << pgate(n) }
body << "    run_serial_gate s2 \"$STAMP\" s2 1\n"
body << pgate(9)
body << "}\n"
File.write(suite, body)
hang_suite = File.join(dir, 'hang_suite.sh')
body = +"suite_fake() {\n"
# The server is started the way every gate starts one (tools/tests/gate_env.sh);
# its empty input directory goes under this gate's own, which is removed.
body << "    run_gate hangs sh -c '. #{ROOT}/tools/tests/gate_env.sh; TMPDIR=#{dir}; gate_env_setup; " \
        "env $GATE_SERVER_ENV #{BINARY} #{server_port} >/dev/null 2>&1 & sleep 0.7; kill -0 $! && echo server-alive; sleep #{unique} & echo started; wait'\n"
body << "    run_gate fine sh -c 'echo fine'\n"
body << "}\n"
File.write(hang_suite, body)

def runner(dir, suite, jobs, suites, extra = {})
  env = { 'BLIT_GATE_FAKE_SUITE' => suite, 'BLIT_GATE_JOBS' => jobs.to_s,
          'BLIT_GATE_PORT_OFFSET' => '7000', 'BLIT_GATE_FAILURE_DIR' => dir,
          'RUNS' => File.join(dir, "runs-#{jobs}"), 'STAMP' => File.join(dir, 'stamp.sh') }.merge(extra)
  FileUtils.mkdir_p(env['RUNS'])
  out, status = Open3.capture2e(env, 'sh', RUNNER, *suites)
  [out, status, env['RUNS']]
end

def lines_of(out)
  out.lines.grep(/\A\S+\s+(PASS|FAIL|SKIP)\s/).map do |line|
    name, verdict = line.split
    [name, verdict]
  end
end

def verdict_line(out)
  out.lines.grep(/\ARUN_GATES_/).last.to_s.strip
end

# --- JOBS=1 and 4 agree ------------------------------------------------------

serial_out, serial_status, serial_runs = runner(dir, suite, 1, %w[fake])
four_out, four_status, four_runs = runner(dir, suite, 4, %w[fake])
check(!serial_status.success? && !four_status.success?, 'the fake suite has a failing gate: both must exit 1')
serial_lines = lines_of(serial_out)
four_lines = lines_of(four_out)
expected = %w[p1 p2 p3 p4 s1 fails skipped na p5 p6 p7 p8 s2 p9]
check(serial_lines.map(&:first) == expected, "JOBS=1 order: #{serial_lines.map(&:first)}")
check(four_lines == serial_lines, "JOBS=4 differs from JOBS=1:\n#{four_lines}\n#{serial_lines}")
check(verdict_line(serial_out) == verdict_line(four_out),
      "verdict lines differ: #{verdict_line(serial_out)} / #{verdict_line(four_out)}")
check(verdict_line(four_out) == 'RUN_GATES_FAIL pass=11 skip=2 fail=1 failed: fails',
      "unexpected verdict line: #{verdict_line(four_out)}")
check(four_out.include?('run-gates: slowest gates') && four_out.include?('time per suite'),
      'the timing table is missing')
check(four_out =~ /^p1\s+PASS\s+p1 done \(\d+\.\ds\)$/, 'a report line must end with its duration')

# --- the runner hands out offsets -------------------------------------------

offsets = File.readlines(File.join(four_runs, 'offsets')).to_h { |l| l.split.then { |n, o| [n, o] } }
parallel_offsets = %w[p1 p2 p3 p4 p5 p6 p7 p8 p9].map { |n| offsets.fetch(n) }.uniq
check((parallel_offsets - %w[offset=7000 offset=8000 offset=9000 offset=10000]).empty?,
      "parallel offsets outside base + k*1000: #{parallel_offsets}")
check(parallel_offsets.length > 1, 'JOBS=4 should have used more than one worker')
check(offsets['s1'] == 'offset=7000' && offsets['s2'] == 'offset=7000',
      "the timing lane uses the base offset: #{offsets}")
serial_offsets = File.readlines(File.join(serial_runs, 'offsets')).map { |l| l.split.last }.uniq
check(serial_offsets == ['offset=7000'], "JOBS=1 must use the base offset only: #{serial_offsets}")

# --- parallel is parallel, the timing lane is alone --------------------------

def intervals(runs)
  starts = {}
  spans = {}
  File.readlines(File.join(runs, 'stamps')).each do |line|
    kind, name, ns = line.split
    (kind == 'start' ? starts : spans)[name] = kind == 'start' ? ns.to_i : (starts.fetch(name)..ns.to_i)
  end
  spans
end

def overlap?(a, b)
  a.begin < b.end && b.begin < a.end
end

spans = intervals(four_runs)
parallel_names = %w[p1 p2 p3 p4 p5 p6 p7 p8 p9]
check(parallel_names.combination(2).any? { |a, b| overlap?(spans[a], spans[b]) },
      'JOBS=4 never ran two gates at once')
%w[s1 s2].each do |serial|
  hit = parallel_names.find { |name| overlap?(spans[serial], spans[name]) }
  check(hit.nil?, "timing-lane gate #{serial} overlapped parallel gate #{hit}")
end
check(!overlap?(spans['s1'], spans['s2']), 'timing-lane gates must not overlap each other')
last_parallel = parallel_names.map { |n| spans[n].end }.max
check(%w[s1 s2].all? { |s| spans[s].begin >= last_parallel }, 'the timing lane starts after the parallel lane ends')

serial_spans = intervals(serial_runs)
check(serial_spans.values.combination(2).none? { |a, b| overlap?(a, b) }, 'JOBS=1 must run one gate at a time')

# --- a hung gate -------------------------------------------------------------

started = Time.now
out, status, = runner(dir, hang_suite, 2, %w[fake], 'BLIT_GATE_TIMEOUT' => '4', 'BLIT_GATE_PORT_OFFSET' => '0')
elapsed = Time.now - started
check(!status.success?, 'a timed-out gate must fail the run')
check(out =~ /^hangs\s+FAIL\s+rc=124 TIMEOUT after 4s: started/, "hung gate not reported as TIMEOUT:\n#{out}")
check(out =~ /^fine\s+PASS\s+fine /, "the gate after a timeout must still run:\n#{out}")
check(elapsed < 30, "the timeout did not bound the run: #{elapsed.round}s")
log = Dir.glob(File.join(dir, 'blit-gate-hangs.*.out')).first
check(log && File.read(log).include?('server-alive'), 'a failure keeps its full output, and the server had started')
sleep 0.5
leftover, = Open3.capture2e('pgrep', '-af', "sleep #{unique}")
check(!leftover.include?("sleep #{unique}"), "the hung gate left a process behind: #{leftover}")
server_left, = Open3.capture2e('pgrep', '-af', "#{File.basename(BINARY)} #{server_port}")
check(!server_left.include?("#{File.basename(BINARY)} #{server_port}"),
      "the hung gate left a blit-server behind: #{server_left}")
check(!serving?(server_port), "port #{server_port} is still served after the timeout")

puts 'RUN_GATES_GATE_PASS offsets=distinct timeout=reported_no_orphan jobs1=jobs4 timing_lane=alone'
