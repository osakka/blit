#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0243 step 2: Blit answers the Glyph Protocol's support check `s` and
# coverage query `q` exactly as its specification (v1.18, docs/terminal.md)
# defines them, so programs written for Rio, Ghostty and kitty work with
# Blit unchanged. A program in a terminal session (glyph_protocol_probe.rb)
# sends each request followed by DA1 and records what came back before DA1's
# reply -- the specification's own detection rule (§3.4). Every case expects
# a reply only this protocol produces, so every case fails on a server
# without it; a request that must go unanswered is followed by `s`, whose
# reply must then be the only one.
#
#   - `s`: `fmt=` empty (Blit accepts no registrations), no `cap`; with the
#     7-bit, raw 8-bit and UTF-8-encoded C1 framings.
#   - `q ; cp=`: a text letter, a Blit icon, a Nerd glyph, an emoji, an Arabic
#     letter, a codepoint nothing draws, the registration block, and Blit's
#     old icon range (in and above the block), on a pixel font (Terminus)
#     and an outline font (Hack); one codepoint whose answer differs between
#     them.
#   - errors (§5.4), `grapheme` unsupported, unknown keys ignored.
#   - `r` rejected, `c` of an empty glossary (§6.2, §7.2), `reply=`.
#   - dropped with no reply and no output: an unknown verb, a message over
#     the bound, and SR-4's class (CR, LF, NUL, BEL inside the message).
#   - order: replies in request order, all before DA1's; a Kitty graphics
#     APC is still Kitty's, answered in order with a Glyph Protocol request.
#   - bounded work (SR-16): a flood of lookups is answered, the part past
#     the per-second budget `reason=unknown`, and DA1 promptly.
#   - nothing reaches the screen.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19431'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
PROBE = File.join(ROOT, 'tools/tests/glyph_protocol_probe.rb')
abort("glyph protocol gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |http| http.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

$failures = 0
def check(condition, message)
  if condition
    puts "PASS #{message}"
  else
    $failures += 1
    warn "FAIL #{message}"
  end
end

$server = nil
def start(env)
  $server = gate_spawn({ **gate_display_env, 'BLIT_BIND_ADDRESS' => '127.0.0.1',
                         'BLIT_AUTH_TOKEN' => '', 'BLIT_LOGIN_AUTOSTART' => '0',
                         'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_FONT_SCALE' => '1',
                         'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'),
                         'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf') }.merge(env),
                       BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').first == 200
      rescue StandardError
        nil
      end
      sleep 0.1
    end
  end
end

def stop
  return if $server.nil?

  Process.kill('TERM', $server) rescue nil
  Process.wait($server) rescue nil
  $server = nil
end
at_exit { stop }

def apc(body) = "\e_#{body}\e\\"
def reply(body) = apc("25a1;#{body}")
def hex(bytes) = bytes.b.unpack1('H*')
S = apc('25a1;s')
S_REPLY = reply('s;fmt=')

# Runs `cases` ([name, request, expected bytes before DA1's reply, options])
# in one terminal session and checks each; returns the screen's text.
def run_cases(label, cases)
  Dir.mktmpdir('blit-glyph-protocol') do |work|
    input = File.join(work, 'cases.json')
    output = File.join(work, 'results.json')
    File.write(input, JSON.generate(cases.map do |_, bytes, _, options|
      { 'hex' => hex(bytes), 'timeout' => (options || {}).fetch(:timeout, 5) }
    end))
    code, created = request('POST', '/v1/terminal-sessions', {
      'command' => '/bin/sh',
      'arguments' => ['-c', "ruby #{PROBE} #{input} #{output}; sleep 0.1"],
      'environment' => {}, 'surface' => { 'width' => 80, 'height' => 24 }
    })
    raise "#{label}: terminal session -> #{code} #{created.inspect[0, 200]}" unless code.between?(200, 299)

    id = created.fetch('id')
    Timeout.timeout(120) do
      sleep 0.05 while %w[starting running].include?(request('GET', "/v1/terminal-sessions/#{id}").last.fetch('state'))
    end
    _, cells = request('GET', "/v1/surfaces/surface-#{id}/cells")
    request('DELETE', "/v1/terminal-sessions/#{id}")
    raise "#{label}: the probe wrote no results" unless File.exist?(output)

    results = JSON.parse(File.read(output))
    cases.zip(results).each do |(name, _, expected, options), result|
      options ||= {}
      if options[:check]
        check(options[:check].call(result), "#{label}: #{name} (#{result['ms']} ms)")
      else
        got = result['before'] && [result['before']].pack('H*')
        check(got == expected && result['after'] == '',
              "#{label}: #{name}" + (got == expected && result['after'] == '' ? '' :
                " (expected #{expected.inspect}, got #{got.inspect}, after DA1 #{result['after'].inspect})"))
      end
    end
    cells.fetch('cells').map { |cell| cell.fetch('text') }.join.strip
  end
end

def q(cp) = apc("25a1;q;cp=#{cp}")
def status(cp, value) = reply("q;cp=#{cp};status=#{value}")

# What each font's cells draw, by codepoint: [Terminus (pixel), Hack (outline)].
COVERAGE = {
  '41' => ['system', 'system'],        # A, the text font
  '101000' => ['system', 'system'],    # Blit's first icon
  'e0a0' => ['system', 'system'],      # Nerd: Powerline's branch, the spec's own example
  'f07b' => ['system', 'system'],      # Nerd: Font Awesome's folder
  '1f600' => ['system', 'system'],     # emoji, colour
  '628' => ['system', 'system'],       # Arabic beh, the word face
  '64b' => ['system', 'system'],       # Arabic fathatan alone: a mark, zero width, drawn over its cell
  'f0000' => ['', ''],                 # PUA-A's first: nothing in the stack (Nerd starts at F0001)
  '20000' => ['', 'system'],           # CJK Ext. B, two cells: Unifont draws it under an outline font;
                                       # a pixel font keeps its wide cells pixels and has no plane 2
  '100000' => ['', ''],                # the registration block's first
  '1003ff' => ['', ''],                # and last
  '100006' => ['', ''],                # an old icon, in the block (it still draws)
  '100400' => ['', ''],                # an old icon above the block (it still draws)
  'fe0f' => ['', '']                   # a selector: no glyph of its own (§5.3)
}.freeze

def coverage_cases(font)
  index = font == :pixel ? 0 : 1
  COVERAGE.map do |cp, answers|
    ["q cp=#{cp}", q(cp), status(cp, answers[index])]
  end
end

PROTOCOL_CASES = [
  ['s', S, S_REPLY],
  ['s ignores parameters', apc('25a1;s;x=1;fmt=vg'), S_REPLY],
  ['s in 8-bit C1 (APC 9F, ST 9C)', "\x9f25a1;s\x9c".b, S_REPLY],
  ['s in UTF-8-encoded C1', "\u009f25a1;s\u009c", S_REPLY],
  ['q cp, uppercase hex and leading zeros', q('0000E0A0'), status('e0a0', 'system')],
  ['q unknown keys ignored, nothing echoed', apc('25a1;q;cp=41;evil=rm -rf;id'), status('41', 'system')],
  ['q in request order, before DA1', q('41') + q('100000') + S, status('41', 'system') + status('100000', '') + S_REPLY],
  ['q neither cp nor grapheme', apc('25a1;q'), reply('q;reason=malformed_query')],
  ['q both cp and grapheme', apc('25a1;q;cp=41;grapheme=41'), reply('q;reason=malformed_query')],
  ['q two cp', apc('25a1;q;cp=41;cp=42'), reply('q;reason=malformed_query')],
  ['q cp not hex', q('zz'), reply('q;reason=malformed_query')],
  ['q cp empty', q(''), reply('q;reason=malformed_query')],
  ['q cp a surrogate', q('d800'), reply('q;cp=d800;reason=malformed_query')],
  ['q cp past U+10FFFF', q('110000'), reply('q;reason=malformed_query')],
  ['q grapheme unsupported', apc('25a1;q;grapheme=1F468,200d,1f469'),
   reply('q;grapheme=1f468,200d,1f469;reason=unsupported')],
  ['q grapheme unsupported, unparsed not echoed', apc('25a1;q;grapheme=1f468,x'), reply('q;reason=unsupported')],
  ['r in the block: no format accepted', apc('25a1;r;cp=100000;fmt=vg;M0 0L10 10Z'),
   reply('r;cp=100000;status=1;reason=unsupported_format')],
  ['r outside the block', apc('25a1;r;cp=e0a0;fmt=glyf;AAAA'), reply('r;cp=e0a0;status=1;reason=out_of_namespace')],
  ['r over the bound, payload cut: still rejected', apc("25a1;r;cp=100001;#{'A' * 20_000}"),
   reply('r;cp=100001;status=1;reason=unsupported_format')],
  ['r reply=2 still fails aloud', apc('25a1;r;cp=100002;reply=2;AAAA'),
   reply('r;cp=100002;status=1;reason=unsupported_format')],
  ['r reply=0 is silent', apc('25a1;r;cp=100002;reply=0;AAAA') + S, S_REPLY],
  ['c every slot', apc('25a1;c'), reply('c;status=0')],
  ['c an empty slot', apc('25a1;c;cp=100005'), reply('c;cp=100005;status=0')],
  ['c outside the block', apc('25a1;c;cp=41'), reply('c;cp=41;status=1;reason=out_of_namespace')],
  ['c reply=2 success is silent', apc('25a1;c;cp=100005;reply=2') + S, S_REPLY],
  ['c reply=0 is silent', apc('25a1;c;cp=41;reply=0') + S, S_REPLY],
  ['unknown verb dropped', apc('25a1;x') + S, S_REPLY],
  ['empty verb dropped', apc('25a1;') + S, S_REPLY],
  ['q over 1024 bytes dropped', apc("25a1;q;cp=41;x=#{'a' * 1100}") + S, S_REPLY],
  ['s over 16 KiB dropped', apc("25a1;s;x=#{'a' * 17_000}") + S, S_REPLY],
  ['SR-4: LF and a command inside q', apc("25a1;q;cp=41\nid\n") + S, S_REPLY],
  ['SR-4: CR inside q', apc("25a1;q;cp=41\r") + S, S_REPLY],
  ['SR-4: NUL inside q', apc("25a1;q\0;cp=41") + S, S_REPLY],
  ['SR-4: BEL inside s (BEL does not end an APC)', apc("25a1;s\a") + S, S_REPLY],
  ['SR-4: control inside r', apc("25a1;r;cp=100000;\nid\n") + S, S_REPLY],
  ['Kitty graphics APC still Kitty\'s, in order', apc('Ga=T,f=24,s=1,v=1,i=7;AAAA') + S,
   apc('Gi=7;OK') + S_REPLY]
].freeze

BUDGET = 256
def flood_case
  cps = (0x1f300...(0x1f300 + BUDGET + 64)).map { |cp| format('%x', cp) }
  check = lambda do |result|
    return false unless result['before'] && result['after'] == ''

    replies = [result['before']].pack('H*').scan(/\e_25a1;([^\e]*)\e\\/).flatten
    # The first BUDGET are looked up; past them, `unknown` until the second
    # is over (on a loaded host the second can end inside the burst, so a
    # later one may be looked up again). Every reply in request order.
    rest = replies.drop(BUDGET)
    replies.length == cps.length &&
      replies.first(BUDGET).each_with_index.all? { |text, i| text.match?(/\Aq;cp=#{cps[i]};status=(system)?\z/) } &&
      rest.each_with_index.all? do |text, i|
        text.match?(/\Aq;cp=#{cps[BUDGET + i]};(status=(system)?|reason=unknown)\z/)
      end &&
      rest.first == "q;cp=#{cps[BUDGET]};reason=unknown" &&
      result['ms'] < 2000
  end
  ["#{cps.length} uncached lookups at once: #{BUDGET} answered, then unknown, in order, DA1 within 2 s",
   cps.map { |cp| q(cp) }.join, nil, { check: check, timeout: 10 }]
end

Dir.mktmpdir('blit-glyph-protocol-settings') do |settings|
  start('BLIT_SETTINGS_PATH' => File.join(settings, 'settings.json'))
  screen = run_cases('pixel', PROTOCOL_CASES + coverage_cases(:pixel))
  check(screen.empty?, "pixel: nothing reached the screen (#{screen[0, 80].inspect})")
  code, = request('PUT', '/v1/fonts', { 'font_id' => 'hack' })
  check(code == 200, "Hack selected (#{code})")
  sleep 1.1
  screen = run_cases('outline', coverage_cases(:outline))
  check(screen.empty?, "outline: nothing reached the screen (#{screen[0, 80].inspect})")
  # A second apart from the lookups before it, so the flood starts with the
  # whole budget.
  sleep 1.1
  run_cases('flood', [flood_case])
end

if $failures.positive?
  warn "glyph protocol gate: #{$failures} failure(s)"
  exit 1
end
puts 'GLYPH_PROTOCOL_PASS'
