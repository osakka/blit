#!/usr/bin/env ruby
# frozen_string_literal: true

# Gitea #216, ADR 0245 phase 2a: a glyph-set import never holds the
# server's loop. The expensive part -- reading the staged parts back, the
# transport and manifest hashes, the signature, the names parse, FreeType's
# checks, the ledger's allocation, writing and syncing the set's files, the
# set table and the remap -- runs on a worker thread; the loop keeps only
# the final swap and the answer.
#
# A signed set whose font is just under the 16 MiB limit (the repository's
# Nerd symbols font with a padding table: FreeType never reads it, every
# hash and write does) is imported while a pane draws all the time:
#
#   - from the moment its body is sent until the PUT answers, GET
#     /v1/version answers within BOUND_MS every time it is asked, the pane
#     keeps being presented, and the server's own max_loop_gap_ms stays
#     under BOUND_MS;
#   - meanwhile a second import is refused 409 glyph_import_active, and a
#     DELETE of the set being imported 409 glyph_import_active too;
#   - the PUT answers 201 as before, its loop_us (the part on the loop) is
#     small, the set is listed loaded and its glyphs drawn (GET /v1/fonts/
#     sets answers its names), and the ledger is the one 2a writes.
#
# Timing: run it alone (run_serial_gate). On 2ceca022, where the import ran
# on the loop, the GETs asked during it waited for the whole import.

require 'json'
require 'socket'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'glyph_store_fixture'

F = GlyphStoreFixture
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19577'), 10))
TOKEN = 'glyph-import-live-gate'
BOUND_MS = Float(ENV.fetch('GLYPH_IMPORT_LIVE_BOUND_MS', '250'))
LOOP_US_BOUND = Integer(ENV.fetch('GLYPH_IMPORT_LIVE_LOOP_US', '50000'), 10)
FONT_LIMIT = 16 << 20

$failures = 0
def check(condition, message)
  if condition
    puts "PASS #{message}"
  else
    $failures += 1
    warn "FAIL #{message}"
  end
  condition
end

def clock
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

def api(method, path)
  F.http(PORT, TOKEN, method, path)
end

def performance
  api('GET', '/v1/diagnostics/runtime')[1].fetch('performance')
end

# FONT with one table more, 'zpad', of zero bytes, so the file is SIZE
# bytes: every record moves 16 bytes on, the padding goes last.
def padded_font(font, size)
  count = font.byteslice(4, 2).unpack1('n')
  records = (0...count).map { |t| font.byteslice(12 + 16 * t, 16).unpack('a4NNN') }
  data = font.byteslice(12 + 16 * count..)
  body_at = 12 + 16 * (count + 1)
  head = font.byteslice(0, 4) + [count + 1].pack('n') + font.byteslice(6, 6)
  records = records.map { |tag, sum, offset, length| [tag, sum, offset + 16, length] }
  pad_at = body_at + data.bytesize
  pad_at += (4 - pad_at % 4) % 4
  pad_length = size - pad_at
  records << ['zpad', 0, pad_at, pad_length]
  out = head + records.sort_by(&:first).map { |r| r.pack('a4NNN') }.join + data
  out += "\0" * (pad_at - out.bytesize)
  out + "\0" * pad_length
end

summary = {}
Dir.mktmpdir('blit-glyph-import-live') do |work|
  keys = F.keys(work)
  store = F.store(work)
  big_font = File.join(work, 'big.ttf')
  File.binwrite(big_font, padded_font(File.binread(F::NERD_FONT), FONT_LIMIT - 4096))
  glyphs = F.nerd_codes('fa', 3)
  big = F.make_set(File.join(work, 'big'), name: 'x-big', release: '1.0', glyphs: glyphs, font: big_font,
                                           secret: keys[:secret], sign: keys[:sign])
  small = F.make_set(File.join(work, 'small'), name: 'x-small', release: '1.0', glyphs: F.nerd_codes('dev', 2),
                                               secret: keys[:secret], sign: keys[:sign])
  check(big['font'].bytesize > FONT_LIMIT - 8192 && big['font'].bytesize <= FONT_LIMIT,
        "the test font is #{big['font'].bytesize} bytes, just under the 16 MiB limit")
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    # A pane that draws all the time, so presents can be counted.
    code, = F.http(PORT, TOKEN, 'POST', '/v1/terminal-sessions',
                   body: JSON.generate({ command: '/bin/sh', display_id: 'display-1',
                                         arguments: ['-c', 'i=0; while :; do i=$((i + 1)); printf "\\r%d " $i; sleep 0.02; done'] }),
                   headers: { 'Content-Type' => 'application/json' })
    check(code == 201, "a drawing session (#{code})")
    sleep 1.5
    before = performance
    check(before['max_loop_gap_ms'] < BOUND_MS,
          "before the import the loop's worst gap is #{before['max_loop_gap_ms'].round(1)} ms, under the bound")

    sent_at = nil
    result = nil
    put = Thread.new do
      started = clock
      code, body = F.import(PORT, TOKEN, 'x-big', big, sent: -> { sent_at = clock })
      result = [code, body, clock - started]
    end
    sleep 0.005 until sent_at || !put.alive?
    # From the moment the body is sent until the PUT answers: how long the
    # runtime diagnostics take to answer, and the pane's presents in them.
    samples = []
    sampler = Thread.new do
      while put.alive?
        started = clock
        code, document = api('GET', '/v1/diagnostics/runtime')
        samples << [clock, (clock - started) * 1000.0, code,
                    (document.dig('performance', 'present_calls') rescue nil)]
        sleep 0.02
      end
    end
    # The import is under way (its body is in, or nearly): a second one and
    # a removal of the same set are refused, not queued behind it.
    second, second_body = F.import(PORT, TOKEN, 'x-small', small)
    removal, removal_body = api('DELETE', '/v1/fonts/glyphs/system/x-big')
    put.join
    sampler.join
    answered = clock
    after = performance
    code, body, took = result
    window_ms = sent_at ? (answered - sent_at) * 1000.0 : 0.0
    worst = samples.map { |s| s[1] }.max || 0.0
    presents = after['present_calls'] - before['present_calls']
    # The longest stretch the samples saw without a new present.
    still = 0.0
    since = sent_at
    last = nil
    samples.each do |at, _, _, count|
      next unless count

      if last.nil? || count > last
        still = [still, (at - since) * 1000.0].max if since
        since = at
        last = count
      end
    end
    still = [still, (answered - since) * 1000.0].max if since
    # The server's own worst loop gap: a new worst during the window is
    # the import's (the pane alone never comes near the bound).
    gap = after['max_loop_gap_ms'] > before['max_loop_gap_ms'] ? after['max_loop_gap_ms'] : 0.0

    check(code == 201 && body.is_a?(Hash) && body['verified'] == true && body['set'] == 'x-big',
          "the big set imported: #{code} #{body.inspect[0, 200]}")
    check(second == 409 && second_body.is_a?(Hash) && second_body['code'] == 'glyph_import_active',
          "a second import meanwhile: #{second} #{second_body.is_a?(Hash) ? second_body['code'] : second_body.to_s[0, 80]}")
    check(removal == 409 && removal_body.is_a?(Hash) && removal_body['code'] == 'glyph_import_active',
          "a DELETE of the set meanwhile: #{removal} #{removal_body.is_a?(Hash) ? removal_body['code'] : removal_body.to_s[0, 80]}")
    check(samples.size >= 3 && samples.all? { |s| s[2] == 200 },
          "GET /v1/diagnostics/runtime asked #{samples.size} times over the #{window_ms.round} ms the import was in flight")
    check(worst < BOUND_MS, "and it answered within #{BOUND_MS.round} ms every time (worst #{worst.round(1)} ms)")
    check(gap < BOUND_MS, "the loop never paused #{BOUND_MS.round} ms (new max_loop_gap_ms #{gap.round(1)})")
    check(presents >= 3 && still < BOUND_MS + 100,
          "the drawing pane kept being presented: #{presents} presents, never #{(BOUND_MS + 100).round} ms " \
          "without one (longest #{still.round} ms)")
    loop_us = body.is_a?(Hash) ? body['loop_us'] : nil
    check(loop_us.is_a?(Integer) && loop_us < LOOP_US_BOUND && body['import_us'].is_a?(Integer) && body['import_us'] > loop_us,
          "the answer says what the loop held: loop_us #{loop_us.inspect} of import_us #{body['import_us'] rescue nil}")

    _, listing = api('GET', '/v1/fonts/glyphs')
    entry = listing['sets'].find { |s| s['id'] == 'x-big' }
    check(entry && entry.dig('install', 'state') == 'loaded' && entry.dig('install', 'verified') == true &&
          listing.dig('counters', 'imports') == 1 && listing.dig('ledger', 'system', 'allocated') == 3,
          "listed loaded and verified, 3 codepoints allocated (#{entry.inspect[0, 160]})")
    name = glyphs.map(&:first).min
    code, answer = api('GET', "/v1/fonts/sets/x-big/#{name}")
    check(code == 200 && answer.is_a?(Hash) && answer['codepoints'] == ['102000'],
          "its first name answers U+102000 (#{code} #{answer.inspect[0, 160]})")
    ledger = File.read(File.join(store, 'ledger'))
    check(ledger.lines.first == "blit-glyph-ledger 1\n" && ledger.lines.size == 4 &&
          ledger.lines[1].start_with?('a 102000 system x-big '),
          "the ledger holds its three allocations (#{ledger.lines.size - 1} lines)")
    check(Dir.children(File.join(store, 'staging')).empty?, 'staging is empty afterwards')

    # What the loop holds grows with names and retirements, not bytes: a
    # set of thousands of names, then an update that drops half of them
    # (each retired in the ledger), each with loop_us under its bound.
    many = F.nerd_codes('md').first(6000)
    names_set = F.make_set(File.join(work, 'many'), name: 'x-many', release: '1.0', glyphs: many,
                                                    secret: keys[:secret], sign: keys[:sign])
    code, body = F.import(PORT, TOKEN, 'x-many', names_set)
    many_loop = body.is_a?(Hash) ? body['loop_us'] : nil
    check(code == 201 && body['allocated'] == many.size && many_loop.is_a?(Integer) && many_loop < LOOP_US_BOUND,
          "#{many.size} names imported: #{code}, loop_us #{many_loop.inspect}")
    kept = many.each_slice(2).map(&:first)
    names_set = F.make_set(File.join(work, 'many'), name: 'x-many', release: '1.1', glyphs: kept,
                                                    secret: keys[:secret], sign: keys[:sign])
    code, body = F.import(PORT, TOKEN, 'x-many', names_set)
    drop_loop = body.is_a?(Hash) ? body['loop_us'] : nil
    dropped = many.size - kept.size
    retired_lines = File.read(File.join(store, 'ledger')).lines.count { |l| l.start_with?('r system x-many ') }
    check(code == 201 && drop_loop.is_a?(Integer) && drop_loop < LOOP_US_BOUND && retired_lines == dropped,
          "an update dropping #{dropped} of them: #{code}, loop_us #{drop_loop.inspect}, #{retired_lines} retired")
    code, gone = api('GET', "/v1/fonts/sets/x-many/#{(many - kept).first.first}")
    check(code == 410, "a dropped name answers 410 (#{code} #{gone.is_a?(Hash) ? gone['code'] : ''})")

    # The store takes the next change once the import has answered.
    code, body = F.import(PORT, TOKEN, 'x-small', small)
    check(code == 201, "an import after it is accepted (#{code})")
    summary = { window_ms: window_ms.round, worst_ms: worst.round(1), gap_ms: gap.round(1), presents: presents, still_ms: still.round,
                import_us: (result[1]['import_us'] rescue nil), loop_us: loop_us, many_loop_us: many_loop, drop_loop_us: drop_loop, took_ms: (took * 1000).round }
  ensure
    F.stop(pid)
  end
end

if $failures.zero?
  puts "GLYPH_IMPORT_LIVE_GATE_PASS backend=#{GATE_BACKEND} font_bytes=#{FONT_LIMIT - 4096} " \
       "window_ms=#{summary[:window_ms]} get_worst_ms=#{summary[:worst_ms]} max_loop_gap_ms=#{summary[:gap_ms]} " \
       "presents=#{summary[:presents]} longest_without_present_ms=#{summary[:still_ms]} import_us=#{summary[:import_us]} loop_us=#{summary[:loop_us]} " \
       "names_#{6000}_loop_us=#{summary[:many_loop_us]} drop_3000_loop_us=#{summary[:drop_loop_us]} " \
       'second_import=409 delete=409'
else
  warn "GLYPH_IMPORT_LIVE_GATE_FAIL #{$failures} checks failed #{summary.inspect}"
  exit 1
end
