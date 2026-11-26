#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0245 section 7: a glyph set reaches root's FreeType only when Blit's
# release keys signed its manifest and every part hashes to it. With a
# throwaway key in a scratch keys directory and a scratch store (never the
# release secret, the real keys or /var/lib/blit):
#
#   1. the unit (glyph_signature_gate.c, built with tcc): the manifest's
#      grammar, the label both ways, a flipped bit and another key, and a
#      staged font replaced after its hash (the hashed bytes are what is
#      stored and what FreeType opens);
#   2. over HTTP, each refused with its code, the store unchanged (every
#      file and the ledger) and nothing parsed (the store's names_parsed and
#      fonts_opened counters, GET /v1/fonts/glyphs, unmoved): a bad
#      signature, a valid signature by a key not trusted, the release
#      statement with its genuine release signature offered as a manifest,
#      a manifest naming another set, a manifest of another version than
#      the names file's release, a part whose hash differs, a part whose
#      bytes are not the transport header's, a missing part; the signed set
#      then installs and is reported verified;
#   3. revocation: with its key file removed, the stored set is not loaded
#      after a restart, stays in the store and is reported refused; with the
#      key back it loads again;
#   4. the store's own check: a store others can read (0755), one under a
#      directory others can write (0777, not sticky), and one reached by a
#      symbolic link are each not used -- GET /v1/fonts/glyphs says why, an
#      import is 503 glyph_store_unavailable -- and the shipped sets are
#      served all the same.
#
# release_signature_gate.sh holds the other direction: a glyph manifest's
# genuine signature offered to the update helper is refused.

require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require_relative 'gate_port'
require_relative 'build_flags'
require_relative 'glyph_store_fixture'

F = GlyphStoreFixture
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19574'), 10))
TOKEN = 'glyph-signature-gate'

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

def counters(port)
  F.http(port, TOKEN, 'GET', '/v1/fonts/glyphs')[1]['counters']
end

summary = {}
Dir.mktmpdir('blit-glyph-signature') do |work|
  keys = F.keys(work)
  glyphs = F.nerd_codes('fa', 24)
  names_path = File.join(work, 'unit-names.json')
  File.write(names_path, F.names_json('6.7.2', glyphs))

  # 1. The unit.
  unit = File.join(work, 'unit')
  sources = [File.join(F::ROOT, 'tools/tests/glyph_signature_gate.c'), File.join(F::ROOT, 'src/blit_font.c'),
             File.join(F::ROOT, 'src/blit_raster.c'),
             File.join(F::ROOT, 'vendor/monocypher/monocypher.c'), File.join(F::ROOT, 'vendor/monocypher/monocypher-ed25519.c')]
  built = system('tcc', '-std=c99', *BuildFlags.cflags('font'), *sources, *BuildFlags.libs('font'), '-lm', '-o', unit)
  check(built, 'the unit builds with tcc')
  unit_work = File.join(work, 'unit-work')
  Dir.mkdir(unit_work)
  out, err, status = Open3.capture3(unit, unit_work, F::NERD_FONT, F::SYMBOLS_FONT, names_path)
  warn err unless err.empty?
  check(status.success? && out.start_with?('GLYPH_SIGNATURE_UNIT_PASS'), "the unit: #{out.strip}")
  summary[:unit] = out[/cases=(\d+)/, 1]

  # 2. Over HTTP.
  store = F.store(work)
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    good = F.make_set(File.join(work, 'good'), name: 'x-sig', release: '6.7.2', glyphs: glyphs,
                      secret: keys[:secret], sign: keys[:sign])
    other_key = F.make_set(File.join(work, 'other'), name: 'x-sig', release: '6.7.2', glyphs: glyphs,
                           secret: keys[:other_secret], sign: keys[:sign])
    other_set = F.make_set(File.join(work, 'other-set'), name: 'x-sig', release: '6.7.2', glyphs: glyphs,
                           secret: keys[:secret], sign: keys[:sign], manifest_name: 'another')
    other_version = F.make_set(File.join(work, 'other-version'), name: 'x-sig', release: '6.7.2', glyphs: glyphs,
                               secret: keys[:secret], sign: keys[:sign],
                               names_text: F.names_json('6.7.3', glyphs))
    # The manifest says 6.7.2 for the names file it hashed, which says 6.7.3.
    # The release statement and its genuine signature (raw), signed by the
    # same trusted key, offered as a manifest.
    statement, = Open3.capture2(keys[:sign], 'statement', '0.3.138-1', Digest::SHA256.hexdigest(good['font']))
    release_sig, = Open3.capture2(keys[:sign], 'sign', keys[:secret], '0.3.138-1', Digest::SHA256.hexdigest(good['font']))
    release_parts = good.merge('manifest' => statement, 'signature' => [release_sig.strip].pack('H*'))
    bad_sig = good.merge('signature' => good['signature'].dup.tap { |s| s.setbyte(5, s.getbyte(5) ^ 1) })
    bad_hash = good.merge('licence' => good['licence'] + "changed\n")
    swapped_names = good.merge('names' => F.names_json('6.7.2', glyphs.first(20)))
    missing = good.except('licence')

    cases = [
      ['a bad signature', bad_sig, {}, 422, 'glyph_signature_invalid'],
      ['a valid signature by a key not in the release keys', other_key, {}, 422, 'glyph_signature_invalid'],
      ['the release statement and its genuine signature offered as a manifest', release_parts, {}, 422,
       'glyph_manifest_invalid'],
      ['a manifest naming another set', other_set, {}, 422, 'glyph_manifest_invalid'],
      ['a manifest of another version than the names file', other_version, {}, 422, 'glyph_names_invalid', :names],
      ['a part whose hash differs from the manifest', bad_hash, {}, 422, 'glyph_hash_mismatch'],
      ['a names file that is not the one signed', swapped_names, {}, 422, 'glyph_hash_mismatch'],
      ['a part that is not the transport header\'s', good, { shas: { 'font' => '0' * 64 } }, 422, 'glyph_hash_mismatch'],
      ['a missing part', missing, {}, 422, 'glyph_part_missing'],
      ['a signature without its manifest', good.except('manifest'), {}, 422, 'glyph_part_missing']
    ]
    cases.each do |label, parts, options, want_code, want_problem, parsed|
      before = F.digest(store)
      seen = counters(PORT)
      code, body = F.import(PORT, TOKEN, 'x-sig', parts, **options)
      after = counters(PORT)
      nothing_parsed = parsed == :names ?
        after['fonts_opened'] == seen['fonts_opened'] : # the release check needs the names file, never the font
        after['names_parsed'] == seen['names_parsed'] && after['fonts_opened'] == seen['fonts_opened']
      check(code == want_code && body.is_a?(Hash) && body['code'] == want_problem && F.digest(store) == before &&
            nothing_parsed,
            "#{label}: #{code} #{body.is_a?(Hash) ? body['code'] : body.inspect[0, 120]}, the store unchanged, " \
            "#{parsed == :names ? 'the font never opened' : 'nothing parsed'}")
    end
    code, sets = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-sig')
    check(code == 404, "after the refusals the set is unknown (#{code})")
    code, body = F.import(PORT, TOKEN, 'x-sig', good)
    check(code == 201 && body['verified'] == true && body['origin'] == 'catalogue' && body['allocated'] == glyphs.size,
          "the signed set installs: #{code} #{body.inspect[0, 200]}")
    _, glyph_listing = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs')
    entry = glyph_listing['sets'].find { |s| s['id'] == 'x-sig' }
    check(entry && entry['install']['state'] == 'loaded' && entry['install']['verified'] == true,
          'GET /v1/fonts/glyphs reports it loaded and verified')
    summary[:cases] = cases.size
  ensure
    F.stop(pid)
  end

  # 3. Revocation: the key file removed, a restart.
  FileUtils.mv(File.join(keys[:keys], 'gate-1.pub'), File.join(work, 'gate-1.pub.aside'))
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    _, listing = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs')
    entry = listing['sets'].find { |s| s['id'] == 'x-sig' }
    check(entry && entry['install']['state'] == 'refused' && entry['install']['reason'].to_s.include?('glyph_signature_invalid'),
          "the key removed, the stored set is refused at start (#{entry && entry['install'].inspect[0, 200]})")
    check(File.exist?(File.join(store, 'system/x-sig/font')), 'and stays in the store')
    code, = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-sig')
    _, fonts = F.http(PORT, TOKEN, 'GET', '/v1/fonts')
    check(code != 200 && fonts['sets'].none? { |s| s['id'] == 'x-sig' }, "and is not served (#{code})")
  ensure
    F.stop(pid)
  end
  FileUtils.mv(File.join(work, 'gate-1.pub.aside'), File.join(keys[:keys], 'gate-1.pub'))
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    code, = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets/x-sig')
    check(code == 200, "the key back, it loads again (#{code})")
  ensure
    F.stop(pid)
  end
end

# 4. The store's ownership check.
Dir.mktmpdir('blit-glyph-store-check') do |work|
  keys = F.keys(work)
  good = F.store(work, 'good')
  open_store = F.store(work, 'open')
  File.chmod(0o755, open_store)
  shared = File.join(work, 'shared')
  Dir.mkdir(shared)
  File.chmod(0o777, shared)
  under_shared = F.store(shared, 'store')
  link = File.join(work, 'link')
  File.symlink(good, link)
  parts = F.make_set(File.join(work, 'set'), name: 'x-chk', release: '1.0', glyphs: F.nerd_codes('fa', 2),
                     secret: keys[:secret], sign: keys[:sign])
  [['a store others can read (0755)', open_store, 'can be written by others than its owner (or read'],
   ['a store under a directory others can write', under_shared, 'can be written by others'],
   ['a store reached by a symbolic link', link, 'is not a directory (or is a link)']].each do |label, path, why|
    pid = F.start(PORT, store: path, keys: keys[:keys], token: TOKEN, root: work)
    begin
      _, listing = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs')
      code, body = F.import(PORT, TOKEN, 'x-chk', parts)
      _, sets = F.http(PORT, TOKEN, 'GET', '/v1/fonts/sets')
      check(listing['store']['ready'] == false && listing['store']['reason'].to_s.include?(why) &&
            code == 503 && body['code'] == 'glyph_store_unavailable' && sets['sets'].size == 18 &&
            Dir.children(path).empty?,
            "#{label}: not used (#{listing['store']['reason']}), import #{code}, the 18 shipped sets served")
    ensure
      F.stop(pid)
    end
  end
  pid = F.start(PORT, store: good, keys: keys[:keys], token: TOKEN, root: work)
  begin
    code, = F.import(PORT, TOKEN, 'x-chk', parts)
    check(code == 201, "the same set into a store that passes: #{code}")
  ensure
    F.stop(pid)
  end
end

if $failures.zero?
  puts "GLYPH_SIGNATURE_GATE_PASS unit=#{summary[:unit]} http_refusals=#{summary[:cases]} revoked=refused restored=loaded store_checks=3"
else
  warn "GLYPH_SIGNATURE_GATE_FAIL #{$failures} checks failed"
  exit 1
end
