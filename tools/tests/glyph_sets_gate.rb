#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0243 step 3, the set level: Blit answers for every glyph set its font
# registry declares (assets/fonts/fonts.source.json, `sets`), by set and
# name, over HTTP (docs/protocols/glyph-sets.md, docs/api.md "Glyph sets").
# This gate reads the names sources itself -- the icon catalogue, the Nerd
# names file and the registry's retired names -- and holds the server to
# them:
#
#   - the registry's sets equal the API's: GET /v1/fonts `sets` and GET
#     /v1/fonts/sets, every set with its owner, scope, version (the
#     catalogue's SHA-256, which is GET /v1/fonts/icons' revision, or the
#     Nerd release) and count; paged through, every listed name with its
#     codepoint, alias and tile, and the set's codepoint ranges; looked up,
#     every name, deprecated and retired ones included, with its alias,
#     deprecation, replacements and retirement;
#   - the registry's retired names are exactly the codepoints the
#     catalogue's retired list keeps, and (where the history is here) every
#     id the catalogue ever published is live or retired;
#   - the 11 names Blit's set shares with Nerd sets resolve per set, with no
#     folding of `-` and `_`;
#   - the grammar: uppercase, too long, empty, a bad hyphen, framing and
#     separator characters, encoded and raw control and non-ASCII bytes, and
#     a bad query are all 400 `malformed_query` with one fixed body, so
#     nothing of the request is echoed;
#   - paging: at most 128 a page, the default 128, `after` resuming after
#     any name, `more`;
#   - an unknown set (emoji included: it is not served yet) is 404
#     `unknown_set`, an unknown name 404 `unknown_name`, a retired one 410
#     `retired`; the routes are authenticated like the rest and read-only.
#
# It fails on a server without the set level (no `sets`, no
# /v1/fonts/sets).

require 'digest'
require 'json'
require 'net/http'
require 'open3'
require 'socket'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
FONTS = File.join(ROOT, 'assets/fonts')
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19558'), 10))
TOKEN = 'glyph-sets-gate'
NAME = /\A[a-z0-9][a-z0-9_-]{0,95}\z/
abort("glyph sets gate: missing #{BINARY} (tools/build-tcc.sh)") unless File.executable?(BINARY)

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

# ---- What the sources say, read here, not from the generator or header. ----

registry = JSON.parse(File.read(File.join(FONTS, 'fonts.source.json')))
level = registry.fetch('sets')
sources = level.select { |_, v| v.is_a?(Hash) && v.key?('names') }
check(sources.keys.sort == %w[blit nerd], "the registry declares the blit and nerd names sources (#{sources.keys})")

# set id -> {owner:, scope:, version:, faces:, names: {name => {cp:, deprecated:, by:, tile:, retired:}}}
expected = {}
blit = sources.fetch('blit')
catalogue_path = File.join(FONTS, blit.dig('names', 'file'))
catalogue = JSON.parse(File.read(catalogue_path))
blit_names = {}
blit.dig('names', 'lists').each do |list|
  catalogue.fetch(list).each do |entry|
    tile = nil
    if entry['cells'] && entry['cells']['span'] > 1
      tile = { 'column' => entry['cells']['position'], 'row' => 0, 'columns' => entry['cells']['span'], 'rows' => 1 }
    elsif (entry['columns'] || 1) > 1 || (entry['rows'] || 1) > 1
      tile = { 'column' => entry['column'] || 0, 'row' => entry['row'] || 0,
               'columns' => entry['columns'] || 1, 'rows' => entry['rows'] || 1 }
    end
    check(!blit_names.key?(entry['id']), "blit/#{entry['id']} is one entry") if blit_names.key?(entry['id'])
    blit_names[entry['id']] = { cp: entry['codepoint'].to_i(16), deprecated: entry['deprecated'] == true,
                                by: entry['replaced_by'], tile: tile }
  end
end
retired_ranges = catalogue.fetch('retired').map { |r| (r['from'].to_i(16)..r['to'].to_i(16)) }
retired = blit.fetch('retired_names')
retired_points = retired.values.map { |r| r['codepoint'].to_i(16) }
# Retired names start with the name lookup (0.3.134): a name the lookup
# published and the catalogue later drops is carried here, at a codepoint
# the catalogue's retired list keeps. Ids removed before the lookup existed
# were never addressable by name and are not carried (the owner,
# 2026-11-22).
check(retired_points.uniq.size == retired_points.size &&
      retired_points.all? { |p| retired_ranges.any? { |r| r.cover?(p) } },
      "the registry's #{retired.size} retired names each hold a codepoint the catalogue's retired list keeps")
check((retired.keys & blit_names.keys).empty?, 'no retired name is live')
check(retired.all? { |_, r| (r['replaced_by'] || []).all? { |b| blit_names.key?(b) } },
      'every retired name is replaced only by live names')
retired.each { |name, r| blit_names[name] = { retired: true, by: r['replaced_by'] } }
def git_lines(*args)
  out, status = Open3.capture2('git', '-C', ROOT, *args, err: File::NULL)
  status.success? ? out.split : []
rescue SystemCallError
  []
end
# The catalogue's history, where this tree has it: a full clone with more
# than one revision of the file. An export, a shallow clone or no git at all
# is a NOTE, not a failure.
# Only the revisions from the name lookup on (89abd838, 0.3.134): what it
# published must stay live or be retired.
SETS_SINCE = '89abd838'
revisions = git_lines('rev-parse', '--is-shallow-repository') == ['false'] &&
            git_lines('cat-file', '-t', SETS_SINCE) == ['commit'] ?
  [SETS_SINCE] + git_lines('log', '--format=%H', "#{SETS_SINCE}..HEAD", '--', 'assets/fonts/icons/catalogue.json') : []
if revisions.size >= 1
  ever = revisions.flat_map do |rev|
    text, = Open3.capture2('git', '-C', ROOT, 'show', "#{rev}:assets/fonts/icons/catalogue.json")
    JSON.parse(text).values.grep(Array).flatten.grep(Hash).filter_map { |e| e['id'] }
  end.uniq
  check((ever - blit_names.keys).empty?,
        "every id the catalogue published since the name lookup (#{revisions.size} revisions) is live or retired " \
        "(#{(ever - blit_names.keys).first(5)})")
else
  puts 'NOTE the catalogue history is not here (an export, a shallow clone or no git): ' \
       'the retired names are held to its retired list only'
end
expected['blit'] = { 'owner' => blit['owner'], 'scope' => blit['scope'], 'faces' => blit['faces'],
                     'version' => Digest::SHA256.file(catalogue_path).hexdigest, names: blit_names }

nerd = sources.fetch('nerd')
nerd_file = JSON.parse(File.read(File.join(FONTS, nerd.dig('names', 'file'))))
check(nerd_file.delete('METADATA')&.fetch('version') == nerd['version'], "the Nerd names file is release #{nerd['version']}")
nerd_file.each do |key, glyph|
  prefix, rest = key.split('-', 2)
  set = (expected["nerd-#{prefix}"] ||= { 'owner' => 'nerd', 'scope' => nerd['scope'], 'faces' => nerd['faces'],
                                          'version' => nerd['version'], names: {} })
  set[:names][rest] = { cp: glyph.fetch('code').to_i(16) }
end
check(expected.keys.grep(/\Anerd-/).size == 17, "the Nerd file makes 17 sets (#{expected.keys.grep(/\Anerd-/).size})")

# Section 4.2: the canonical name of a glyph with several names in one set
# is the bytewise smallest; the rest are aliases.
expected.each_value do |set|
  set[:names].reject { |_, n| n[:retired] }.group_by { |_, n| n[:cp] }.each_value do |group|
    canonical = group.map(&:first).min
    group.each { |name, n| n[:alias_of] = canonical unless name == canonical }
  end
  set[:listed] = set[:names].reject { |_, n| n[:retired] || n[:deprecated] }.keys.sort
  points = set[:names].reject { |_, n| n[:retired] }.map { |_, n| n[:cp] }.uniq.sort
  set[:ranges] = points.slice_when { |a, b| b != a + 1 }.map { |run| { 'first' => run.first.to_s(16), 'last' => run.last.to_s(16) } }
  set['count'] = set[:listed].size
end
check(expected.values.sum { |s| s[:names].count { |_, n| n[:alias_of] } } == 366, 'the Nerd sets have 366 aliases')
check(expected['blit']['count'] == 808 && expected['blit'][:names].size == 830 + retired.size,
      "blit: 830 ids, 808 listed (#{expected['blit']['count']}), #{retired.size} retired")
check(expected.values.flat_map { |s| s[:names].values }.none? { |n| n[:cp] && n[:cp].between?(0x100000, 0x100495) },
      'no name is in the registration block or the old icon range')
expected_sets = expected.keys.sort.map do |id|
  { 'id' => id, 'owner' => expected[id]['owner'], 'scope' => expected[id]['scope'],
    'version' => expected[id]['version'], 'count' => expected[id]['count'], 'faces' => expected[id]['faces'] }
end

# The clashes (section 1): a Blit id that is also a Nerd name, as spelled or
# with Nerd's _ for Blit's -.
clashes = []
expected['blit'][:names].each do |id, n|
  next if n[:retired]

  expected.each do |set_id, set|
    next if set_id == 'blit'

    [id, id.tr('-', '_')].uniq.each { |spelt| clashes << [id, set_id, spelt] if set[:names].key?(spelt) }
  end
end
check(clashes.map(&:first).uniq.sort ==
      %w[battery-0 battery-1 battery-2 battery-3 battery-4 bluetooth phone-classic-off phone-off phone-ring power wifi-off],
      "the 11 names Blit shares with Nerd sets (#{clashes.map(&:first).uniq.size})")

# ---- The server. ----

def raw(path, method: 'GET', token: TOKEN)
  Socket.tcp('127.0.0.1', PORT, connect_timeout: 5) do |socket|
    socket.write("#{method} ".b + path.b + " HTTP/1.1\r\nHost: 127.0.0.1\r\n".b +
                 "#{token ? "Authorization: Bearer #{token}\r\n" : ''}Connection: close\r\n\r\n".b)
    response = socket.read.to_s
    head, body = response.split("\r\n\r\n", 2)
    [head.to_s[/\AHTTP\/1\.1 (\d+)/, 1].to_i, body.to_s]
  end
end

def get(path)
  code, body = raw(path)
  [code, (JSON.parse(body) rescue nil)]
end

def page_all(id, limit: nil)
  names = []
  pages = 0
  after = nil
  loop do
    query = [after && "after=#{after}", limit && "limit=#{limit}"].compact.join('&')
    code, page = get("/v1/fonts/sets/#{id}#{query.empty? ? '' : "?#{query}"}")
    return [nil, pages, nil] unless code == 200 && page.is_a?(Hash) && page['names'].is_a?(Array)

    pages += 1
    return [nil, pages, page] if page['names'].size > (limit || 128) || (page['more'] && page['names'].empty?)

    names.concat(page['names'])
    break unless page['more']

    after = page['names'].last['name']
  end
  [names, pages, nil]
end

Dir.mktmpdir('blit-glyph-sets') do |root|
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_BIND_ADDRESS' => '127.0.0.1', 'BLIT_AUTH_TOKEN' => TOKEN, 'BLIT_LOGIN_AUTOSTART' => '0',
    'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_FONT_ROOT' => FONTS, 'BLIT_FONT_LOCAL_ROOT' => File.join(root, 'local'),
    'BLIT_PSF_FONT' => File.join(FONTS, 'Blit-Terminus16.psf'), 'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(20) { loop { break if (raw('/v1/version').first rescue nil) == 200; sleep 0.1 } }

    _, fonts = get('/v1/fonts')
    check(fonts.is_a?(Hash) && fonts['sets'] == expected_sets,
          "GET /v1/fonts sets are the registry's #{expected_sets.size} (#{fonts.is_a?(Hash) ? fonts['sets'].inspect[0, 160] : fonts})")
    code, listing = get('/v1/fonts/sets')
    check(code == 200 && listing.is_a?(Hash) && listing['sets'] == expected_sets, "GET /v1/fonts/sets -> #{code}, the same sets")
    _, icons = get('/v1/fonts/icons')
    check(icons.is_a?(Hash) && icons['revision'] == expected['blit']['version'],
          "blit's version is the catalogue's revision (#{expected['blit']['version'][0, 12]})")

    # Every set, paged through: every listed name, its codepoint, alias, tile.
    pages_total = 0
    expected.each do |id, set|
      names, pages, bad = page_all(id)
      pages_total += pages
      want = set[:listed].map do |name|
        n = set[:names][name]
        { 'name' => name, 'codepoints' => [n[:cp].to_s(16)] }.tap do |e|
          e['alias_of'] = n[:alias_of] if n[:alias_of]
          e['tile'] = n[:tile] if n[:tile]
        end
      end
      check(names == want, "#{id}: #{set['count']} names over #{pages} pages, each with its codepoint, alias and tile" \
                           "#{bad ? " (bad page #{bad.inspect[0, 200]})" : ''}#{names && names != want ? " (first difference #{(names.zip(want).find { |a, b| a != b } || [names.size, want.size]).inspect[0, 200]})" : ''}")
    end
    _, first = get('/v1/fonts/sets/blit')
    check(first.is_a?(Hash) && first['set'] == expected_sets.first && first['ranges'] == expected['blit'][:ranges],
          "the listing carries the set and its #{expected['blit'][:ranges].size} codepoint ranges, derived")
    check(expected.all? { |id, set| get("/v1/fonts/sets/#{id}?limit=1")[1]&.fetch('ranges', nil) == set[:ranges] },
          'every set\'s ranges are its names\' runs')

    # Every name looked up, deprecated and retired ones included.
    lookups = expected.flat_map { |id, set| set[:names].map { |name, n| [id, name, n] } }
    started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    wrong = Queue.new
    work = Queue.new
    lookups.each { |l| work << l }
    Array.new(4) do
      Thread.new do
        while (item = (work.pop(true) rescue nil))
          id, name, n = item
          code, record = get("/v1/fonts/sets/#{id}/#{name}")
          want = if n[:retired]
                   [410, { 'type' => 'about:blank', 'title' => 'Gone', 'status' => 410, 'code' => 'retired',
                           'detail' => "#{id}/#{name} was retired: it no longer resolves and is never reused.",
                           'set' => id, 'version' => expected[id]['version'], 'name' => name, 'retired' => true }
                           .tap { |r| r['replaced_by'] = n[:by] if n[:by] }]
                 else
                   [200, { 'set' => id, 'scope' => expected[id]['scope'], 'version' => expected[id]['version'],
                           'name' => name, 'codepoints' => [n[:cp].to_s(16)] }.tap do |r|
                           r['alias_of'] = n[:alias_of] if n[:alias_of]
                           r['deprecated'] = true if n[:deprecated]
                           r['replaced_by'] = n[:by] if n[:by]
                           r['tile'] = n[:tile] if n[:tile]
                         end]
                 end
          wrong << [id, name, code, record] unless [code, record] == want
        end
      end
    end.each(&:join)
    elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
    bad = Array.new(wrong.size) { wrong.pop }
    check(bad.empty?, "all #{lookups.size} names looked up (#{lookups.count { |l| l[2][:alias_of] }} aliases, " \
                      "#{lookups.count { |l| l[2][:deprecated] }} deprecated, #{lookups.count { |l| l[2][:retired] }} retired) " \
                      "answer their records in #{elapsed.round(1)} s#{bad.empty? ? '' : ": #{bad.first(3).inspect[0, 300]}"}")

    # The clashes resolve per set; no folding of - and _.
    clashes.each do |id, set_id, spelt|
      _, mine = get("/v1/fonts/sets/blit/#{id}")
      _, theirs = get("/v1/fonts/sets/#{set_id}/#{spelt}")
      check(mine&.fetch('codepoints', nil) == [expected['blit'][:names][id][:cp].to_s(16)] &&
            theirs&.fetch('codepoints', nil) == [expected[set_id][:names][spelt][:cp].to_s(16)] &&
            mine['codepoints'] != theirs['codepoints'],
            "blit/#{id} is #{mine&.fetch('codepoints', nil)&.first}, #{set_id}/#{spelt} is #{theirs&.fetch('codepoints', nil)&.first}")
      next if spelt == id

      code, problem = get("/v1/fonts/sets/#{set_id}/#{id}")
      check(code == 404 && problem&.fetch('code', nil) == 'unknown_name', "#{set_id}/#{id} (Blit's spelling) -> #{code} unknown_name")
      code, problem = get("/v1/fonts/sets/blit/#{spelt}")
      check(code == 404 && problem&.fetch('code', nil) == 'unknown_name', "blit/#{spelt} (Nerd's spelling) -> #{code} unknown_name")
    end

    # The grammar: one fixed refusal, nothing echoed.
    malformed = ['/v1/fonts/sets/Blit', '/v1/fonts/sets/blit/BlueTooth', "/v1/fonts/sets/#{'a' * 33}",
                 "/v1/fonts/sets/blit/#{'a' * 97}", '/v1/fonts/sets/nerd-', '/v1/fonts/sets/nerd--fa',
                 '/v1/fonts/sets/1blit', '/v1/fonts/sets/-blit', '/v1/fonts/sets/', '/v1/fonts/sets/blit/',
                 '/v1/fonts/sets/blit/-power', '/v1/fonts/sets/blit/_power', '/v1/fonts/sets/blit/a/b',
                 '/v1/fonts/sets/blit/blue;tooth', '/v1/fonts/sets/blit/blue,tooth', '/v1/fonts/sets/blit/blue=tooth',
                 '/v1/fonts/sets/blit/blue:tooth', '/v1/fonts/sets/blit/blue.tooth', '/v1/fonts/sets/blit/blue@tooth',
                 '/v1/fonts/sets/nerd_fa/bluetooth', '/v1/fonts/sets/blit/x%0d%0aecho%20pwned',
                 '/v1/fonts/sets/blit/%3Bx', "/v1/fonts/sets/blit/caf\xC3\xA9".b, "/v1/fonts/sets/blit/a\x1b[31m".b,
                 '/v1/fonts/sets/blit?limit=0', '/v1/fonts/sets/blit?limit=129', '/v1/fonts/sets/blit?limit=abc',
                 '/v1/fonts/sets/blit?limit=01', '/v1/fonts/sets/blit?limit=', '/v1/fonts/sets/blit?after=Upper',
                 '/v1/fonts/sets/blit?after=', '/v1/fonts/sets/blit?foo=1', '/v1/fonts/sets/blit?limit=5&limit=6',
                 '/v1/fonts/sets/blit?after=a&after=b', '/v1/fonts/sets/blit?after=a&', '/v1/fonts/sets/blit/bluetooth?x=1',
                 '/v1/fonts/sets?after=a']
    bodies = malformed.map do |path|
      code, body = raw(path)
      check(code == 400 && (JSON.parse(body)['code'] rescue nil) == 'malformed_query',
            "#{path.inspect[0, 60]} -> #{code} malformed_query")
      body
    end
    check(bodies.uniq.size == 1 && !bodies.first.match?(/BlueTooth|Blit|tooth|pwned|aaaa|nerd|Upper|foo|caf|\e/),
          'every malformed request gets the one fixed body: nothing of it is echoed')

    # Paging bounds.
    _, page = get('/v1/fonts/sets/nerd-md')
    check(page && page['names'].size == 128 && page['more'] == true, 'a page is 128 names by default, more: true')
    _, page = get('/v1/fonts/sets/nerd-md?limit=128')
    check(page && page['names'].size == 128, 'limit=128 is the bound and is accepted')
    _, page = get('/v1/fonts/sets/nerd-pl?limit=1')
    check(page && page['names'].size == 1 && page['more'] == true, 'limit=1 is one name')
    names, pages, = page_all('nerd-fa', limit: 7)
    check(names&.map { |n| n['name'] } == expected['nerd-fa'][:listed] && pages == (expected['nerd-fa']['count'] + 6) / 7,
          "nerd-fa in pages of 7: #{pages} pages, every name once")
    _, page = get('/v1/fonts/sets/blit?after=zzzz')
    check(page && page['names'] == [] && page['more'] == false, 'after the last name: no names, more: false')
    _, page = get('/v1/fonts/sets/blit?after=wifi-1&limit=1')
    want = expected['blit'][:listed].find { |n| n > 'wifi-1' }
    check(page && page['names'].map { |n| n['name'] } == [want], "after a deprecated name (wifi-1): #{want}")
    _, page = get('/v1/fonts/sets/nerd-dev?after=amazonwebservicer&limit=2')
    check(page && page['names'].map { |n| n['name'] } == expected['nerd-dev'][:listed].select { |n| n > 'amazonwebservicer' }.first(2),
          'after a name the set never had: the names after it')
    check(expected['blit'][:names].select { |_, n| n[:deprecated] }.keys.none? { |n| expected['blit'][:listed].include?(n) },
          'deprecated and retired names are not listed')

    # Unknown, retired, auth, methods.
    [['/v1/fonts/sets/nerd-xx', 'unknown_set'], ['/v1/fonts/sets/emoji', 'unknown_set'],
     ['/v1/fonts/sets/emoji/pinata', 'unknown_set'], ['/v1/fonts/sets/nerd/bluetooth', 'unknown_set'],
     ['/v1/fonts/sets/blit/no-such-icon', 'unknown_name'], ['/v1/fonts/sets/nerd-md/phone-off', 'unknown_name']].each do |path, why|
      code, problem = get(path)
      check(code == 404 && problem&.fetch('code', nil) == why, "#{path} -> #{code} #{why}")
    end
    # Ids removed before the name lookup existed are unknown, not retired.
    %w[phone-off-large-left file-asm-amber-left slate-dark-close].each do |old|
      code, problem = get("/v1/fonts/sets/blit/#{old}")
      check(code == 404 && problem&.fetch('code', nil) == 'unknown_name', "blit/#{old} (removed before 0.3.134) -> 404 unknown_name")
    end
    # A retired name answers 410 with its replacement, when the registry has one.
    if (name, r = retired.first)
      code, problem = get("/v1/fonts/sets/blit/#{name}")
      check(code == 410 && problem&.fetch('code', nil) == 'retired' && !problem.key?('codepoints') &&
            problem['replaced_by'] == r['replaced_by'], "blit/#{name} -> 410 retired")
    else
      puts 'NOTE no name is retired yet: the 410 path waits for the first name the lookup publishes and later drops'
    end
    _, record = get('/v1/fonts/sets/blit/wifi-1')
    check(record&.fetch('deprecated', nil) == true && record['replaced_by'] == ['cellular-1'] && record['codepoints'] == ['101002'],
          'blit/wifi-1 resolves, deprecated, replaced_by cellular-1')
    _, record = get('/v1/fonts/sets/nerd-dev/aws')
    check(record&.fetch('alias_of', nil) == 'amazonwebservices' && record['codepoints'] == ['e7ad'], 'nerd-dev/aws is an alias of amazonwebservices')
    _, record = get('/v1/fonts/sets/blit/hui-right')
    check(record&.fetch('tile', nil) == { 'column' => 1, 'row' => 0, 'columns' => 2, 'rows' => 1 }, 'blit/hui-right is tile 1,0 of 2x1')
    check(raw('/v1/fonts/sets/blit', token: nil).first == 401 && raw('/v1/fonts/sets/blit/bluetooth', token: 'wrong').first == 401,
          'the routes need the bearer token')
    check(raw('/v1/fonts/sets/blit', method: 'POST').first == 405 && raw('/v1/fonts/sets/blit/bluetooth', method: 'DELETE').first == 405,
          'the routes are read-only (405)')
    $summary = "sets=#{expected.size} names=#{lookups.size} listed=#{expected.values.sum { |s| s['count'] }} " \
               "pages=#{pages_total} clashes=#{clashes.map(&:first).uniq.size} lookups_s=#{elapsed.round(1)}"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

if $failures.zero?
  puts "GLYPH_SETS_PASS #{$summary}"
else
  warn "GLYPH_SETS_FAIL #{$failures} checks failed"
  exit 1
end
