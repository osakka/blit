#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0245 section 3: the codepoint ledger, over HTTP imports into a scratch
# store (system scope; user scope is phase 2b), signed with a throwaway key:
#
#   - a first import allocates U+102000 on, one contiguous run, in bytewise
#     name order;
#   - an update keeps every name's codepoint, redrawn (another `code`) or
#     not; a new name takes the next codepoint; a dropped name is retired
#     (410 retired) and an update bringing it back is refused (409
#     glyph_retired_name) with nothing changed;
#   - uninstalled, the set and its names answer 410 uninstalled; reinstalled,
#     every name has its old codepoint back and nothing new is allocated;
#   - another set's codepoints follow; the ledger survives a restart;
#   - hundreds of random imports, updates and removals: no codepoint is ever
#     given to two addresses, no address ever changes codepoint (checked
#     after every step against the API and the ledger file);
#   - a crash between the ledger's append and the rename: the appended
#     codepoints are the set's when it is imported again; a torn last line is
#     cut off at start; a crash after the rename, before the retirements
#     are written (an installed set lacking a name the ledger allocated it):
#     the next start retires the name, and the set still loads;
#   - the system quota (24,574) answers 507 glyph_quota_exhausted and a full
#     ledger 507 glyph_ledger_full, each with nothing changed; a set that
#     fits exactly is taken.

require 'json'
require_relative 'gate_port'
require_relative 'glyph_store_fixture'

F = GlyphStoreFixture
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19575'), 10))
TOKEN = 'glyph-ledger-gate'
FIRST = 0x102000
LAST = 0x10fffd
STEPS = Integer(ENV.fetch('GLYPH_LEDGER_STEPS', '200'), 10)

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

def lookup(set, name)
  code, body = F.http(PORT, TOKEN, 'GET', "/v1/fonts/sets/#{set}/#{name}")
  [code, body.is_a?(Hash) ? body : {}]
end

def cp_of(set, name)
  code, body = lookup(set, name)
  code == 200 ? body['codepoints'].first.to_i(16) : nil
end

# The ledger file: allocations as {[scope, set, name] => cp}, retirements.
def ledger(store)
  path = File.join(store, 'ledger')
  return [{}, [], []] unless File.exist?(path)

  lines = File.read(path).lines
  allocations = {}
  repeats = []
  retired = []
  lines.drop(1).each do |line|
    f = line.split
    if f[0] == 'a'
      key = f[2, 3]
      repeats << key if allocations.key?(key)
      allocations[key] = f[1].to_i(16)
    elsif f[0] == 'r'
      retired << f[1, 3]
    end
  end
  [allocations, retired, repeats]
end

summary = {}
Dir.mktmpdir('blit-glyph-ledger') do |work|
  keys = F.keys(work)
  pool = F.nerd_codes('fa', 60)
  store = F.store(work)
  make = lambda do |set, release, glyphs|
    F.make_set(File.join(work, "set-#{set}"), name: set, release: release, glyphs: glyphs,
                                               secret: keys[:secret], sign: keys[:sign])
  end
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    # A first install: name order, one run.
    first = pool.first(10).shuffle(random: Random.new(1)).to_h
    code, body = F.import(PORT, TOKEN, 'x-ledger', make.call('x-ledger', '1.0', first))
    check(code == 201 && body['allocated'] == 10, "first import: #{code} allocated #{body['allocated'] rescue body}")
    want = first.keys.sort.each_with_index.to_h { |name, i| [name, FIRST + i] }
    got = want.keys.to_h { |name| [name, cp_of('x-ledger', name)] }
    check(got == want, "allocated in bytewise name order from U+102000 (#{got.values.map { |c| c&.to_s(16) }.join(' ')})")
    version1 = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-ledger?limit=1')[1]['set']['version']

    # An update: two names more, one redrawn (another code).
    second = first.merge(pool[10][0] => pool[10][1], pool[11][0] => pool[11][1])
    redrawn = first.keys.first
    second[redrawn] = pool[12][1]
    code, body = F.import(PORT, TOKEN, 'x-ledger', make.call('x-ledger', '1.1', second))
    check(code == 201 && body['allocated'] == 2, "an update with two names more: #{code} allocated #{body['allocated'] rescue body}")
    news = [pool[10][0], pool[11][0]].sort
    want.merge!(news.each_with_index.to_h { |name, i| [name, FIRST + 10 + i] })
    got = want.keys.to_h { |name| [name, cp_of('x-ledger', name)] }
    check(got == want, "every name keeps its codepoint (#{redrawn} redrawn too); the new ones follow in name order")
    version2 = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-ledger?limit=1')[1]['set']['version']
    check(version1 != version2 && version2.match?(/\A[0-9a-f]{64}\z/), 'the set\'s version changes with the update')

    # A name dropped is retired; bringing it back is refused.
    dropped = want.keys.sort[3]
    third = second.reject { |name, _| name == dropped }
    code, = F.import(PORT, TOKEN, 'x-ledger', make.call('x-ledger', '1.2', third))
    code_r, record = lookup('x-ledger', dropped)
    check(code == 201 && code_r == 410 && record['code'] == 'retired', "#{dropped} dropped by an update: 410 retired")
    check(ledger(store)[1].include?(['system', 'x-ledger', dropped]), 'the retirement is in the ledger')
    before = F.digest(store)
    code, body = F.import(PORT, TOKEN, 'x-ledger', make.call('x-ledger', '1.3', second))
    check(code == 409 && body['code'] == 'glyph_retired_name' && F.digest(store) == before,
          "an update bringing #{dropped} back: #{code} #{body['code'] rescue body}, nothing changed")
    want.delete(dropped)

    # Uninstall and reinstall.
    code, = F.http(PORT, TOKEN, 'DELETE', '/v1/fonts/glyphs/system/x-ledger')
    c1, b1 = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-ledger')
    c2, b2 = lookup('x-ledger', want.keys.first)
    check(code == 200 && c1 == 410 && b1['code'] == 'uninstalled' && c2 == 410 && b2['code'] == 'uninstalled',
          "uninstalled: the set and its names answer 410 uninstalled (#{code} #{c1} #{c2})")
    code, body = F.import(PORT, TOKEN, 'x-ledger', make.call('x-ledger', '1.2', third))
    got = want.keys.to_h { |name| [name, cp_of('x-ledger', name)] }
    check(code == 201 && body['allocated'].zero? && got == want,
          'reinstalled: every name has its old codepoint back, nothing new allocated')

    # Another set follows.
    code, body = F.import(PORT, TOKEN, 'x-other', make.call('x-other', '1.0', pool.first(5).to_h))
    others = pool.first(5).map(&:first).sort.each_with_index.to_h { |name, i| [name, FIRST + 12 + i] }
    check(code == 201 && others.all? { |name, cp| cp_of('x-other', name) == cp },
          'another set\'s codepoints follow; the same names in another set are other codepoints')
  ensure
    F.stop(pid)
  end

  # A restart keeps everything.
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    got = want.keys.to_h { |name| [name, cp_of('x-ledger', name)] }
    check(got == want, 'after a restart every codepoint is the same')

    # Random imports, updates and removals: each import keeps most of the
    # set's last names, drops one now and then (retired for good) and adds a
    # few new ones; a redraw points a kept name at another code.
    rng = Random.new(Integer(ENV.fetch('GLYPH_LEDGER_SEED', '245'), 10))
    big = F.nerd_codes('md', 3000)
    sets = %w[x-r1 x-r2 x-r3]
    retired = Hash.new { |h, k| h[k] = [] }
    last = Hash.new { |h, k| h[k] = {} }
    seen = {}
    installed = {}
    fresh = big.dup.shuffle(random: rng)
    imports = removals = 0
    STEPS.times do |step|
      set = sets.sample(random: rng)
      if installed[set] && rng.rand < 0.15
        code, = F.http(PORT, TOKEN, 'DELETE', "/v1/fonts/glyphs/system/#{set}")
        check(false, "step #{step}: DELETE #{set} -> #{code}") unless code == 200
        installed.delete(set)
        removals += 1
        next
      end
      glyphs = last[set].dup
      if glyphs.size > 2 && rng.rand < 0.25
        gone = glyphs.keys.sample(random: rng)
        glyphs.delete(gone)
      end
      (rng.rand(4) + (glyphs.empty? ? 1 : 0)).times do
        name, code = fresh.pop
        glyphs[name] = code
      end
      glyphs[glyphs.keys.sample(random: rng)] = big.sample(random: rng)[1] if rng.rand < 0.3 # a redraw
      code, body = F.import(PORT, TOKEN, set, make.call(set, "1.#{step}", glyphs))
      unless code == 201
        check(false, "step #{step}: #{set} import -> #{code} #{body.inspect[0, 200]}")
        next
      end
      imports += 1
      # Every name the set ever had and this import lacks is retired.
      (seen.keys.select { |s, _| s == set }.map(&:last) - glyphs.keys).each do |name|
        retired[set] << name unless retired[set].include?(name)
      end
      last[set] = glyphs
      installed[set] = true
      glyphs.each_key do |name|
        cp = cp_of(set, name)
        if seen.key?([set, name])
          check(false, "step #{step}: #{set}/#{name} moved from #{seen[[set, name]].to_s(16)} to #{cp&.to_s(16)}") unless seen[[set, name]] == cp
        else
          seen[[set, name]] = cp
        end
      end
      retired[set].each do |name|
        code, = lookup(set, name)
        check(false, "step #{step}: retired #{set}/#{name} answers #{code}") unless code == 410
      end
    end
    allocations, ledger_retired, repeats = ledger(store)
    check(repeats.empty?, "no address is allocated twice in the ledger (#{repeats.first(3)})")
    check(allocations.values.uniq.size == allocations.size, "no codepoint is given to two addresses (#{allocations.size} allocations)")
    check(allocations.values.each_cons(2).all? { |a, b| b > a }, 'allocations only grow')
    check(seen.all? { |(set, name), cp| allocations[['system', set, name]] == cp },
          "every codepoint the API answered over #{STEPS} steps is the ledger's for that address")
    check(ledger_retired.all? { |_, set, name| lookup(set, name).first != 200 }, 'no retired name resolves')
    summary[:random] = "steps=#{STEPS} imports=#{imports} removals=#{removals} allocations=#{allocations.size} " \
                       "retired=#{ledger_retired.size}"
  ensure
    F.stop(pid)
  end

  # A crash between the ledger's append and the rename; a torn last line.
  allocations, = ledger(store)
  next_cp = allocations.values.max + 1
  crash = pool.first(3).to_h
  File.open(File.join(store, 'ledger'), 'a') do |f|
    crash.keys.sort.each_with_index { |name, i| f.write(format("a %06x system x-crash %s\n", next_cp + i, name)) }
    f.write('a 10fff0 system x-torn hal')
  end
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    check(!File.read(File.join(store, 'ledger')).include?('x-torn'), 'a torn last line is cut off at start')
    code, body = F.import(PORT, TOKEN, 'x-crash', make.call('x-crash', '1.0', crash))
    check(code == 201 && body['allocated'].zero? &&
          crash.keys.sort.each_with_index.all? { |name, i| cp_of('x-crash', name) == next_cp + i },
          'after a crash between append and rename, the appended codepoints are the set\'s')
  ensure
    F.stop(pid)
  end
  # After the rename, before the retirements: x-crash's ledger holds a name
  # the installed set lacks, with no retirement.
  allocations, = ledger(store)
  lost = 'lost-name'
  File.open(File.join(store, 'ledger'), 'a') { |f| f.write(format("a %06x system x-crash %s\n", allocations.values.max + 1, lost)) }
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    _, listing = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs')
    entry = listing['sets'].find { |s| s['id'] == 'x-crash' }
    code_r, record = lookup('x-crash', lost)
    check(entry && entry['install']['state'] == 'loaded' && code_r == 410 && record['code'] == 'retired' &&
          ledger(store)[1].include?(['system', 'x-crash', lost]),
          "after a crash before the retirements, the next start retires #{lost} and the set loads " \
          "(#{entry && entry['install']['state']}, #{code_r})")
  ensure
    F.stop(pid)
  end

  # The quota and a full ledger, on ledgers made for them.
  fill = lambda do |path, scope, count, from = FIRST|
    File.open(path, 'w', 0o600) do |f|
      f.write("blit-glyph-ledger 1\n")
      count.times { |i| f.write(format("a %06x %s x-filler n%06d\n", from + i, scope, i)) }
    end
    File.chmod(0o600, path)
  end
  quota_store = F.store(work, 'quota')
  fill.call(File.join(quota_store, 'ledger'), 'system', 24_570)
  pid = F.start(PORT, store: quota_store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    before = F.digest(quota_store)
    code, body = F.import(PORT, TOKEN, 'x-quota', make.call('x-quota', '1.0', pool.first(10).to_h))
    check(code == 507 && body['code'] == 'glyph_quota_exhausted' && F.digest(quota_store) == before,
          "the system quota spent: #{code} #{body['code'] rescue body}, nothing changed")
    code, = F.import(PORT, TOKEN, 'x-quota', make.call('x-quota', '1.0', pool.first(4).to_h))
    check(code == 201, "a set that fits the quota exactly is taken (#{code})")
  ensure
    F.stop(pid)
  end
  full_store = F.store(work, 'full')
  fill.call(File.join(full_store, 'ledger'), 'user-1000', (LAST - FIRST + 1) - 5)
  pid = F.start(PORT, store: full_store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    before = F.digest(full_store)
    code, body = F.import(PORT, TOKEN, 'x-full', make.call('x-full', '1.0', pool.first(10).to_h))
    check(code == 507 && body['code'] == 'glyph_ledger_full' && F.digest(full_store) == before,
          "a full ledger: #{code} #{body['code'] rescue body}, nothing changed")
    code, = F.import(PORT, TOKEN, 'x-full', make.call('x-full', '1.0', pool.first(5).to_h))
    check(code == 201 && cp_of('x-full', pool.first(5).map(&:first).max) == LAST,
          "the last five codepoints are taken, up to U+10FFFD (#{code})")
    code, body = F.import(PORT, TOKEN, 'x-full2', make.call('x-full2', '1.0', pool.first(1).to_h))
    check(code == 507 && body['code'] == 'glyph_ledger_full', 'then the ledger is full')
    _, listing = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs')
    check(listing['ledger']['free'].zero? && listing['ledger']['allocated'] == LAST - FIRST + 1,
          'GET /v1/fonts/glyphs reports the ledger used up')
  ensure
    F.stop(pid)
  end
end

if $failures.zero?
  puts "GLYPH_LEDGER_GATE_PASS #{summary[:random]} quota=507 full=507"
else
  warn "GLYPH_LEDGER_GATE_FAIL #{$failures} checks failed"
  exit 1
end
