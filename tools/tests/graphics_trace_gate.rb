#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #19. The live Hibr desktop presents no bitmap at all, and from outside
# Blit the two explanations are the same observation: a producer that sends no
# image and an image Blit decodes and throws away both leave a session with
# zero images, no errors and a healthy document. This gate asserts the
# evidence that tells them apart -- one trace entry per graphics payload that
# reaches the terminal parser, written after the decode so it records the
# outcome too, plus counters that survive the trace ring's eviction.
#
# It also asserts the negative, which is the part that makes the evidence
# usable: a device control string that is not a graphics payload must not
# appear, or "nothing arrived" could never be read as nothing arrived.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19265'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("graphics-trace gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "graphics-trace-#{rand(1 << 30)}" if method == 'POST'
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |h| h.request(req) }
end

def json(path)
  JSON.parse(request('GET', path).body)
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "graphics-trace gate: FAIL #{message}"
end

server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'SDL_RENDERER' => 'software',
                 'BLIT_PSF_FONT' => FONT,
                 'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end
Timeout.timeout(20) do
  loop do
    begin
      break if request('GET', '/v1/version').code == '200'
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

# A payload reaches the parser from the session's child process, so what is
# measured is Blit's parser and not an API write.
def emit(chunks)
  parameters = Array(chunks)
  script = "printf '%b' \"#{(1..parameters.length).map { |i| "$#{i}" }.join}\"; " \
           'sleep 0.3'
  created = JSON.parse(request('POST', '/v1/terminal-sessions',
    'command' => '/bin/sh',
    'arguments' => ['-c', script, 'sh', *parameters],
    'surface' => { 'width' => 40, 'height' => 8 }).body)
  abort("graphics-trace gate: create failed #{created}") unless created['id']
  session = created.fetch('id')
  Timeout.timeout(15) do
    loop do
      state = json("/v1/terminal-sessions/#{session}")
      break unless %w[starting running].include?(state.fetch('state'))

      sleep 0.05
    end
  end
  state = json("/v1/terminal-sessions/#{session}")
  request('DELETE', "/v1/terminal-sessions/#{session}")
  state
end

# A flood needs more payloads than `arguments` can carry (32), so the
# repetition happens in the shell.
def emit_many(chunk, times)
  script = "i=0; while [ $i -lt #{times} ]; do printf '%b' \"$1\"; " \
           'i=$((i+1)); done; sleep 0.3'
  created = JSON.parse(request('POST', '/v1/terminal-sessions',
    'command' => '/bin/sh',
    'arguments' => ['-c', script, 'sh', chunk],
    'surface' => { 'width' => 40, 'height' => 8 }).body)
  abort("graphics-trace gate: flood create failed #{created}") unless created['id']
  session = created.fetch('id')
  Timeout.timeout(60) do
    loop do
      state = json("/v1/terminal-sessions/#{session}")
      break unless %w[starting running].include?(state.fetch('state'))

      sleep 0.05
    end
  end
  request('DELETE', "/v1/terminal-sessions/#{session}")
end

def counters
  performance = json('/v1/diagnostics/runtime').fetch('performance')
  %w[graphics_payloads graphics_payloads_kitty graphics_payloads_sixel
     graphics_payloads_rejected].to_h do |field|
    [field, performance[field]]
  end
end

def trace(kind = 'graphics_payload')
  json("/v1/diagnostics/trace?kind=#{kind}&limit=128").fetch('events')
end

base = counters
# Nothing below this reads without the counters, so a missing one ends the
# gate here rather than cascading into NoMethodError noise.
abort("graphics-trace gate: FAIL the performance document does not publish " \
      "the graphics payload counters: #{base.inspect}") if base.value?(nil)
check(base.values.all?(&:zero?),
      "nothing has entered the parser yet, got #{base.inspect}")
check(trace.empty?, "no graphics trace entries yet, got #{trace.length}")

# --- a payload that decodes ---
state = emit(["\\033Pq#1;2;0;100;0~~~~~~~~\\033\\\\"])
check(state.fetch('graphics').length == 1, 'the sixel decodes')
after = counters
check(after['graphics_payloads'] == 1 && after['graphics_payloads_sixel'] == 1,
      "one sixel payload counted, got #{after.inspect}")
check(after['graphics_payloads_rejected'].zero?,
      "nothing rejected, got #{after.inspect}")
events = trace
check(events.length == 1, "one trace entry, got #{events.length}")
detail = events.last.to_h['detail'].to_s
puts "accepted: #{detail}"
check(detail.include?('protocol=sixel'), "the entry names the protocol: #{detail}")
check(detail.include?('images=0->1'),
      "the entry records the outcome: #{detail}")
check(detail.include?('rejected=0') && detail.include?('reason=-'),
      "the entry records no rejection: #{detail}")
check(detail =~ /bytes=\d+/ && !detail.include?('bytes=0'),
      "the entry records the payload size: #{detail}")
check(detail.include?('header=DCS0;0;0q'),
      "the entry records the introducer: #{detail}")

# --- a payload that arrives and is thrown away ---
#
# This is the case the ticket cannot currently distinguish: the session ends
# with zero images either way.
state = emit(["\\033Pq#999~~~~\\033\\\\"])
check(state.fetch('graphics').length.zero?, 'the bad register creates no image')
after = counters
check(after['graphics_payloads'] == 2,
      "the rejected payload is still counted as arriving, got #{after.inspect}")
check(after['graphics_payloads_rejected'] == 1,
      "the rejection is counted, got #{after.inspect}")
detail = trace.last.to_h['detail'].to_s
puts "rejected: #{detail}"
check(detail.include?('rejected=1') && detail.include?('reason=sixel-colour'),
      "the entry names the reason: #{detail}")
check(detail.include?('images=0->0'),
      "the entry records that no image resulted: #{detail}")

# --- the Kitty side of the same funnel ---
state = emit(['\\033_Ga=T,f=24,s=2,v=2,q=2;AAAAAAAAAAAAAAAA\\033\\\\'])
after = counters
check(after['graphics_payloads'] == 3 && after['graphics_payloads_kitty'] == 1,
      "the Kitty payload is counted separately, got #{after.inspect}")
check(state.fetch('graphics').length == 1,
      "the Kitty transmission decodes, got #{state['graphics_diagnostics'].inspect}")
detail = trace.last.to_h['detail'].to_s
puts "kitty: #{detail}"
check(detail.include?('protocol=kitty'), "the entry names Kitty: #{detail}")
check(detail.include?('images=0->1'),
      "the entry records the outcome: #{detail}")
check(detail.include?('header=Ga=T,f=24,s=2,v=2,q=2'),
      "the entry records the control keys: #{detail}")

# Hibr splits a Kitty upload at 4096 base64 bytes. The opening chunk carries
# i=<id>; every q=2 continuation deliberately omits i and means "continue the
# current transmission". This is the exact wire shape that used to leave the
# live wallpaper incomplete on the real host.
state = emit([
  '\\033_Ga=T,f=24,s=1,v=1,i=7,m=1;/w\\033\\\\',
  '\\033_Gq=2,m=0;AA\\033\\\\'
])
after = counters
check(after['graphics_payloads'] == 5 && after['graphics_payloads_kitty'] == 3,
      "the id-less Kitty continuation is counted, got #{after.inspect}")
check(state.fetch('graphics').any? { |image| image['id'] == 7 && image['complete'] },
      "the id-less Kitty continuation completes image 7, got #{state.inspect}")
detail = trace.last.to_h['detail'].to_s
puts "kitty continuation: #{detail}"
check(detail.include?('header=Gq=2,m=0') && detail.include?('rejected=0'),
      "the id-less continuation is accepted: #{detail}")

# A key the protocol defines and Blit refuses still arrives, which is the
# distinction the ticket is about.
state = emit(['\\033_Ga=T,f=24,s=2,v=2,t=f,q=2;AAAAAAAAAAAAAAAA\\033\\\\'])
after = counters
check(after['graphics_payloads'] == 6 &&
      after['graphics_payloads_rejected'] == 2 &&
      after['graphics_payloads_kitty'] == 4,
      "a refused transmission medium arrives and is counted, got #{after.inspect}")
detail = trace.last.to_h['detail'].to_s
puts "kitty refused: #{detail}"
check(detail.include?('reason=unsupported-key'),
      "the entry names the reason: #{detail}")

# --- the negative, which is what makes zero readable as zero ---
before = counters
emit(['\\033P$qm\\033\\\\', '\\033P+q544e\\033\\\\', '\\033]0;title\\007'])
after = counters
check(after == before,
      "DECRQSS, XTGETTCAP and OSC are not graphics payloads, got " \
      "#{before.inspect} then #{after.inspect}")
check(trace.length == 6,
      "no trace entry for a non-graphics string, got #{trace.length}")

# Graphics payloads have their own ring rather than sharing the critical one.
# They arrive at whatever rate the application sends them, and the critical
# ring exists so a busy producer cannot evict input and render evidence --
# sharing it would have traded one blind spot for another. An unfiltered read
# returns the surface ring, as it always has, so the kind has to be named.
check(trace.all? { |e| e['kind'] == 'graphics_payload' },
      'the graphics filter returns only graphics entries')
check(json('/v1/diagnostics/trace?limit=128').fetch('events')
        .none? { |e| e['kind'] == 'graphics_payload' },
      'graphics entries do not sit in the surface ring')
emit_many("\\033Pq#1;2;0;100;0~\\033\\\\", 160)
check(trace.length == 128,
      "the graphics ring holds its bound, got #{trace.length}")
check(json('/v1/diagnostics/trace?kind=keyboard_key&limit=128')
        .fetch('events').length.zero?,
      'a graphics flood did not push anything into the critical ring')

unless $failures.empty?
  warn "graphics-trace gate: #{$failures.length} failure(s)"
  exit 1
end
final = counters
puts "GRAPHICS_TRACE_PASS payloads=#{final['graphics_payloads']} " \
     "kitty=#{final['graphics_payloads_kitty']} " \
     "sixel=#{final['graphics_payloads_sixel']} " \
     "rejected=#{final['graphics_payloads_rejected']}"
