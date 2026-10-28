#!/usr/bin/env ruby
# frozen_string_literal: true

# Pointer lock, mode ?7701 (ADR 0198, #139), as the program on the PTY sees
# it. Each session puts its terminal in raw mode and copies every byte it is
# sent to a file, so the gate reads exactly what Blit wrote.
#
#   * DECRQM answers 2 (settable, off) before and 1 after CSI ? 7701 h
#   * a session without focus is refused, with ; 2 $ y
#   * while locked: the pointer is hidden and does not move; raw motion
#     arrives as CSI < 35 ; dx ; dy M (32 + 3, no button), summed exactly; a button arrives as
#     CSI < 0 ; 0 ; 0 M; absolute motion is ignored
#   * the program gives it back with CSI ? 7701 l: no notice, pointer shown
#   * Blit gives it back, with an unsolicited CSI ? 7701 ; 2 $ y, when focus
#     moves to another session, and when the program stops reading
#   * the pointer reappears where it was locked

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19553'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
QUERY = "\e[?7701$p"
OFF = "\e[?7701;2$y"
ON = "\e[?7701;1$y"

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PATCH' => Net::HTTP::Patch, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['Idempotency-Key'] = "lock-#{rand(1 << 30)}" if method == 'POST'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : JSON.parse(text)]
end

def check(ok, why)
  raise "POINTER_LOCK_FAIL #{why}" unless ok
end

def inject(event)
  code, = request('POST', '/v1/input/inject', event)
  check(code.between?(200, 299), "inject #{event} -> #{code}")
end

def pointer
  p = request('GET', '/v1/diagnostics')[1].fetch('render').fetch('pointer')
  [p['x'], p['y']]
end

def hidden?
  request('GET', '/v1/settings')[1].dig('effective', 'pointer_shown', 'hidden')
end

def wait_until(what, seconds = 5)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  raise "POINTER_LOCK_FAIL timed out waiting for #{what}"
end

# A raw-mode session that copies its input to `file`. `script` runs first,
# after `delay` seconds, so the gate can give the session focus beforehand.
def session(file, script, row, delay: 1.0, tail: "exec cat > #{file}")
  code, body = request('POST', '/v1/terminal-sessions',
                       { command: '/bin/sh', display_id: 'display-1',
                         arguments: ['-c', "stty raw -echo; : > #{file}; sleep #{delay}; " \
                                           "printf '#{script}'; #{tail}"],
                         surface: { width: 40, height: 4 } })
  check(code == 201, "session -> #{code} #{body}")
  body
end

def focus(id)
  code, = request('PATCH', "/v1/terminal-sessions/#{id}", { focused: true })
  check(code.between?(200, 299), "focus #{id} -> #{code}")
end

# What a session has been sent so far; nothing before it has started.
def sent(file)
  File.exist?(file) ? File.binread(file) : ''
end

def deltas(text)
  text.scan(/\e\[<(\d+);(-?\d+);(-?\d+)([Mm])/).map { |b, x, y, f| [b.to_i, x.to_i, y.to_i, f] }
end

Dir.mktmpdir('blit-pointer-lock') do |root|
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    inject({ kind: 'pointer_motion', x: 100, y: 100 })

    # Refused without focus: the program is told at once.
    cold = File.join(root, 'cold')
    unfocused = session(cold, '\033[?7701h', 0, delay: 0.2)
    wait_until('the refusal') { sent(cold).include?(OFF) }
    check(!hidden?, 'a refused lock hid the pointer')
    _, refused = request('GET', "/v1/terminal-sessions/#{unfocused['id']}")
    check(refused.dig('mouse_reporting', 'pointer_lock_refusals') == 1,
          "the refusal was not counted: #{refused['mouse_reporting']}")
    request('DELETE', "/v1/terminal-sessions/#{unfocused['id']}")

    # Engaged with focus.
    held = File.join(root, 'held')
    game = session(held, '\033[?7701$p\033[?7701h\033[?7701$p', 0)
    focus(game['id'])
    wait_until('the lock') { sent(held).include?(ON) }
    before = sent(held)
    check(before.index(OFF) < before.index(ON), "DECRQM before engaging was not 2: #{before.inspect}")
    _, state = request('GET', "/v1/terminal-sessions/#{game['id']}")
    check(state.dig('mouse_reporting', 'pointer_lock') == true &&
          state.dig('mouse_reporting', 'pointer_lock_refusals').zero?, "session JSON: #{state['mouse_reporting']}")
    wait_until('the pointer to hide') { hidden? }
    at = pointer

    # Raw motion, summed exactly; the pointer stays put; absolute motion is
    # ignored.
    20.times { inject({ kind: 'pointer_relative', dx: 7, dy: -3 }) }
    inject({ kind: 'pointer_motion', x: 300, y: 300 })
    wait_until('all the motion') { deltas(sent(held)).sum { |d| d[1] } == 140 }
    moves = deltas(sent(held))
    # 32 + 3: motion with no button held, as in any SGR hover report.
    check(moves.all? { |b, *| b == 35 }, "motion codes #{moves.map(&:first).uniq}")
    check(moves.sum { |d| d[2] } == -60, "dy summed to #{moves.sum { |d| d[2] }}, expected -60")
    check(moves.size < 20, "#{moves.size} reports for 20 samples: not coalesced")
    check(pointer == at, "the locked pointer moved: #{at} -> #{pointer}")

    inject({ kind: 'pointer_button', button: 0, pressed: true })
    inject({ kind: 'pointer_button', button: 0, pressed: false })
    wait_until('the click') { sent(held).include?("\e[<0;0;0m") }
    check(sent(held).include?("\e[<0;0;0M"), 'no press in the lock form')

    # Focus moving to another session takes the lock back, with the notice.
    other = File.join(root, 'other')
    bystander = session(other, '', 1, delay: 0)
    notices = sent(held).scan(OFF).size
    focus(bystander['id'])
    wait_until('the notice on focus loss') { sent(held).scan(OFF).size == notices + 1 }
    wait_until('the pointer to show') { !hidden? }
    check(pointer == at, "the pointer came back at #{pointer}, locked at #{at}")
    check(!sent(other).include?("\e[<"), 'the bystander was sent lock reports')

    # The program gives it back itself: no notice. The same program engages
    # again once it has focus, then releases.
    again = File.join(root, 'again')
    polite = session(again, '\033[?7701h', 2, delay: 1.0,
                     tail: "sleep 1; printf '\\033[?7701l'; exec cat > #{again}.after")
    focus(polite['id'])
    wait_until('the second lock') { hidden? }
    wait_until('the program to release') { !hidden? }
    sleep 0.3
    check(!sent("#{again}.after").include?(OFF), 'a program release was answered with a notice')
    check(!sent(again).include?(OFF), 'the second lock was refused')

    # A program that stops reading is not allowed to keep the pointer: its
    # input fills, and a second later the pointer is back.
    stuck = File.join(root, 'stuck')
    frozen = session(stuck, '\033[?7701h', 3, delay: 1.0, tail: 'sleep 120')
    focus(frozen['id'])
    wait_until('the frozen program to lock') { hidden? }
    Timeout.timeout(30) do
      until !hidden?
        inject({ kind: 'pointer_relative', dx: 1000, dy: 1000 })
        sleep 0.02
      end
    end
    _, state = request('GET', "/v1/terminal-sessions/#{frozen['id']}")
    check(state.dig('mouse_reporting', 'pointer_lock') == false, 'a stuck program kept the lock')
    puts 'POINTER_LOCK_PASS query=2_then_1 refused_unfocused=told+counted hidden=yes still=yes ' \
         "raw_sum=exact coalesced=#{moves.size}/20 buttons=lock_form focus_loss=released+told " \
         'program_release=silent stuck_program=released'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
