#!/usr/bin/env ruby
# frozen_string_literal: true

# Kitty deletes are observable. A session's wallpaper image once vanished
# from the store while its program believed it placed, and nothing could
# say what had removed it. One session places images 1, 2 and 3, then:
#
#   * a=d,d=i,i=1  lowercase: the placement goes, the data stays (freed 0)
#   * a=d,d=I,i=2  capital: the placement and the image (freed 1)
#   * a=d,d=A      every placement, and every image left unplaced: 1 and 3
#                  (freed 2)
#
# graphics_diagnostics -- present before anything happened, with zeros --
# then counts i, I and A once each and delete_freed 3, and the trace has
# one kitty_delete line per delete with its session, letter, id and count.
# Every image freed is traced as image_freed with its id and reason: the
# deletes' (delete-d=I, delete-d=A), then, after one incomplete upload more
# than the store holds, the eviction's.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19593'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
MAX_IMAGES = File.read(File.join(ROOT, 'src/blit_graphics.h'))[/#define BLIT_GRAPHICS_MAX_IMAGES (\d+)/, 1].to_i
KINDS = %w[a A i I z Z c C p P q Q x X y Y].freeze

abort("kitty delete gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "KITTY_DELETE_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 5)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def apc(body)
  "\\033_G#{body}\\033\\\\"
end

def trace(kind)
  get("/v1/diagnostics/trace?kind=#{kind}&limit=128").fetch('events').map { |e| e.fetch('detail') }
end

env = gate_display_env.merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_AUTH_TOKEN' => '')
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-kitty-delete') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  server = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  # Each input line is one step, so the counters can be read between them.
  # A session argument is at most 255 bytes, so each step's bytes are a
  # positional parameter of the script; the last is one incomplete upload's
  # format, sent once more than the store holds, ids 101 and up.
  steps = {
    'place' => (1..3).map { |i| apc("a=T,f=24,s=1,v=1,i=#{i},q=2;/wAA") }.join,
    'lower' => apc('a=d,d=i,i=1,q=2'),
    'capital' => apc('a=d,d=I,i=2,q=2'),
    'all' => apc('a=d,d=A,q=2'),
    'evict' => apc('a=t,f=24,s=1,v=1,i=%d,m=1,q=2;/w')
  }
  script = 'stty -echo; while read -r step; do case "$step" in ' \
           'place) printf "$1";; lower) printf "$2";; capital) printf "$3";; all) printf "$4";; ' \
           "evict) i=101; while [ $i -le #{101 + MAX_IMAGES} ]; do printf \"$5\" $i; i=$((i + 1)); done;; " \
           'esac; done'
  code, created = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', arguments: ['-c', script, 'kitty-delete', *steps.values],
                            display_id: 'display-1', surface: { width: 40, height: 10 } })
  check(code == 201, "session: #{code} #{created}")
  session = created.fetch('id')
  # The trace names a session as the rest of the trace does.
  traced = "session-#{session.delete_prefix('session-')}"
  state = -> { get("/v1/terminal-sessions/#{session}") }
  step = lambda do |name|
    request('POST', "/v1/terminal-sessions/#{session}/input", { data: "#{name}\n" })
    sleep 0.4
  end

  zeros = state.call['graphics_diagnostics']
  check(zeros == { 'rejected' => 0, 'evicted' => 0, 'last_error' => '',
                   'deletes' => KINDS.to_h { |k| [k, 0] }, 'delete_freed' => 0 },
        "graphics_diagnostics before anything happened: #{zeros.inspect}")

  step.call('place')
  wait_until('the three images were not placed') { state.call['graphics'].map { |g| g['id'] }.sort == [1, 2, 3] }
  step.call('lower')
  check(state.call['graphics'].map { |g| g['id'] }.sort == [1, 2, 3], 'd=i freed an image')
  step.call('capital')
  check(state.call['graphics'].map { |g| g['id'] }.sort == [1, 3], 'd=I,i=2 did not free image 2 alone')
  step.call('all')
  after = state.call
  check(after['graphics'].empty?, "d=A left #{after['graphics'].map { |g| g['id'] }}")
  deletes = after.dig('graphics_diagnostics', 'deletes')
  want = KINDS.to_h { |k| [k, %w[i I A].include?(k) ? 1 : 0] }
  check(deletes == want, "deletes counted #{deletes}, not #{want}")
  check(after.dig('graphics_diagnostics', 'delete_freed') == 3,
        "delete_freed #{after.dig('graphics_diagnostics', 'delete_freed')}, not 3")

  lines = []
  wait_until('the deletes were not traced') { (lines = trace('kitty_delete')).length >= 3 }
  wanted = ["session=#{traced} d=i i=1 freed=0", "session=#{traced} d=I i=2 freed=1",
            "session=#{traced} d=A i=0 freed=2"]
  check(lines == wanted, "kitty_delete trace #{lines}, not #{wanted}")

  step.call('evict')
  freed = []
  wanted = ["session=#{traced} i=2 protocol=kitty complete=1 reason=delete-d=I",
            "session=#{traced} i=1 protocol=kitty complete=1 reason=delete-d=A",
            "session=#{traced} i=3 protocol=kitty complete=1 reason=delete-d=A",
            "session=#{traced} i=101 protocol=kitty complete=0 reason=evicted-incomplete"]
  wait_until('the eviction was not traced') { (freed = trace('image_freed')).length >= 4 }
  check(freed == wanted, "image_freed trace #{freed}, not #{wanted}")
  check(state.call.dig('graphics_diagnostics', 'evicted') == 1, 'the eviction was not counted')

  puts "KITTY_DELETE_PASS diagnostics=always_present deletes=i:1,I:1,A:1 delete_freed=3 " \
       "trace=kitty_delete_x3_exact image_freed=delete-d=I+delete-d=Ax2+evicted-incomplete"
end
