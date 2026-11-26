#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0245 phase 2a: an installed glyph set drawn, over HTTP, into a scratch
# store, signed with a throwaway key. BLIT_GATE_BACKEND=native (the CPU
# compositor) or gles; run it under both.
#
#   - first under the pixel font (Terminus, whose raster declines what the
#     PSF has and draws the rest from the stack), then under Hack:
#   - a session prints Nerd Font Awesome glyphs at their own codes and the
#     ledger's codepoints a set of the same font will be given -- before
#     the set exists, so those cells draw the replacement and are cached;
#   - the signed set is imported over HTTP: without a restart each ledger
#     codepoint draws exactly what FreeType draws for the font at its own
#     code through the same fitted path (the stack's Nerd face, pixel for
#     pixel), and the Glyph Protocol's q answers status=system for it;
#   - after a font-size change the same holds; after DELETE the cells draw
#     the replacement again and q answers status= empty;
#   - the administrator's unsigned set (origin own) is imported into system
#     and reported verified: false wherever sets are reported;
#   - the font checks, each before any glyph loads and each with nothing
#     changed (the store's files, the ledger, GET /v1/fonts): a collection,
#     a face that is not scalable, not a font, a code the cmap lacks, a font
#     over 16 MiB (refused from the header, before a byte is staged), a
#     names file over 8,192 glyphs; an upload cut short leaves nothing;
#   - the routes: user scope is 2b's (501), the bearer token is required,
#     a malformed id is 400 malformed_query, the licence is served, the
#     limits are reported.

require 'json'
require 'net/http'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_bmp'
require_relative 'glyph_store_fixture'

F = GlyphStoreFixture
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19576'), 10))
TOKEN = 'glyph-install-gate'
PROBE = File.join(F::ROOT, 'tools/tests/glyph_protocol_probe.rb')

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

def api(method, path, body = nil)
  F.http(PORT, TOKEN, method, path, body: body && JSON.generate(body),
                                    headers: body ? { 'Content-Type' => 'application/json' } : {})
end

def shot
  Socket.tcp('127.0.0.1', PORT) do |s|
    s.write("GET /v1/diagnostics/screenshots/display-1 HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer #{TOKEN}\r\n" \
            "Connection: close\r\n\r\n")
    GateBmp.new(s.read.split("\r\n\r\n", 2)[1])
  end
end

# A cell's pixels, as a string of grey levels.
def cell(image, grid, column, row)
  cw = grid['cell_width']
  ch = grid['cell_height']
  x0 = grid['origin_x'] + column * cw
  y0 = grid['origin_y'] + row * ch
  (0...ch).flat_map { |y| (0...cw).map { |x| image.pixel(x0 + x, y0 + y).max } }.pack('C*')
end

def cells(columns)
  sleep 0.6
  image = shot
  grid = api('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
  columns.map { |c| cell(image, grid, c, 1) }
end

# The Glyph Protocol's q for each codepoint, from a program in a session.
def q_answers(cps)
  Dir.mktmpdir('blit-glyph-install-q') do |work|
    input = File.join(work, 'cases.json')
    output = File.join(work, 'results.json')
    File.write(input, JSON.generate(cps.map { |cp| { 'hex' => "\e_25a1;q;cp=#{cp.to_s(16)}\e\\".unpack1('H*'), 'timeout' => 5 } }))
    _, created = api('POST', '/v1/terminal-sessions', { 'command' => '/bin/sh',
                                                        'arguments' => ['-c', "ruby #{PROBE} #{input} #{output}; sleep 0.1"],
                                                        'environment' => {}, 'surface' => { 'width' => 80, 'height' => 24 } })
    id = created.fetch('id')
    Timeout.timeout(60) do
      sleep 0.05 while %w[starting running].include?(api('GET', "/v1/terminal-sessions/#{id}")[1]['state'])
    end
    api('DELETE', "/v1/terminal-sessions/#{id}")
    JSON.parse(File.read(output)).map { |r| [r['before'].to_s].pack('H*')[/status=([a-z]*)/, 1] }
  end
end

summary = {}
Dir.mktmpdir('blit-glyph-install') do |work|
  keys = F.keys(work)
  glyphs = F.nerd_codes('fa', 3)
  store = F.store(work)
  pid = F.start(PORT, store: store, keys: keys[:keys], token: TOKEN, root: work)
  begin
    ledger_cps = glyphs.map(&:first).sort.each_with_index.map { |_, i| 0x102000 + i }
    by_name = glyphs.to_h
    own = glyphs.map(&:first).sort.map { |name| by_name[name].to_i(16) }
    line = (own + ledger_cps).map { |cp| [cp].pack('U') }.join(' ')
    columns = (0...6).map { |i| 2 + i * 2 }
    set = F.make_set(File.join(work, 'fa'), name: 'x-fa', release: '6.7.2', glyphs: glyphs,
                     secret: keys[:secret], sign: keys[:sign])

    # The pixel font first.
    _, session = api('POST', '/v1/terminal-sessions',
                     { command: '/bin/sh', display_id: 'display-1',
                       arguments: ['-c', "printf '\\033[?25l\\033[38;2;255;255;255;48;2;0;0;0m\\033[2J\\033[2;3H#{line}'; sleep 600"] })
    pixel_before = cells(columns)
    code, = F.import(PORT, TOKEN, 'x-fa', set)
    pixel_after = cells(columns)
    check(code == 201 && pixel_before[3] != pixel_after[3] && (0...3).all? { |i| pixel_after[3 + i] == pixel_after[i] },
          'Terminus (a pixel font): each ledger codepoint draws as the font at its own code once imported')
    check(q_answers(ledger_cps) == %w[system system system], 'Terminus: q answers status=system')
    api('DELETE', '/v1/fonts/glyphs/system/x-fa')
    check(cells(columns)[3] == pixel_before[3] && q_answers(ledger_cps) == ['', '', ''],
          'Terminus: removed, the replacement and status= empty again')
    api('DELETE', "/v1/terminal-sessions/#{session['id']}")

    code, = api('PUT', '/v1/fonts', { font_id: 'hack', size_px: 22 })
    check(code == 200, "Hack at 22 px (#{code})")
    code, session = api('POST', '/v1/terminal-sessions',
                        { command: '/bin/sh', display_id: 'display-1',
                          arguments: ['-c', "printf '\\033[?25l\\033[38;2;255;255;255;48;2;0;0;0m\\033[2J\\033[2;3H#{line}'; sleep 600"] })
    check(code == 201, "a session printing #{line.inspect} (#{code})")
    before = cells(columns)
    check(before[3, 3].uniq.size == 1 && before[3] != before[0], 'before the import the ledger codepoints draw the replacement')
    check(q_answers(ledger_cps) == ['', '', ''], 'and q answers status= empty for them')

    code, body = F.import(PORT, TOKEN, 'x-fa', set)
    check(code == 201 && body['verified'] == true, "the signed set imported: #{code} #{body.inspect[0, 160]}")
    after = cells(columns)
    3.times do |i|
      diff = after[3 + i].bytes.zip(after[i].bytes).count { |a, b| (a - b).abs > 0 }
      big = after[3 + i].bytes.zip(after[i].bytes).map { |a, b| (a - b).abs }.max
      warn "DETAIL #{i}: differing=#{diff} max=#{big} same_as_before=#{after[3 + i] == before[3]}" unless after[3 + i] == after[i]
      check(after[3 + i] == after[i] && after[i] != before[3],
            "U+#{ledger_cps[i].to_s(16).upcase} draws #{glyphs.map(&:first).sort[i]} as the font draws U+#{own[i].to_s(16).upcase}, " \
            'without a restart')
    end
    check(q_answers(ledger_cps) == %w[system system system], 'q answers status=system for each')

    # A redraw: an update points the first name at the second's code. Its
    # codepoint stays; the cell shows the new picture without a restart.
    names = glyphs.map(&:first).sort
    redraw = glyphs.to_h.merge(names[0] => by_name[names[1]])
    set = F.make_set(File.join(work, 'fa'), name: 'x-fa', release: '6.7.3', glyphs: redraw,
                     secret: keys[:secret], sign: keys[:sign])
    code, body = F.import(PORT, TOKEN, 'x-fa', set)
    redrawn = cells(columns)
    check(code == 201 && body['allocated'].zero? && redrawn[3] == after[1] && redrawn[3] != after[3],
          "a redraw (#{names[0]} now the font's U+#{by_name[names[1]].upcase}): the same codepoint, the new picture")
    set = F.make_set(File.join(work, 'fa'), name: 'x-fa', release: '6.7.4', glyphs: glyphs,
                     secret: keys[:secret], sign: keys[:sign])
    F.import(PORT, TOKEN, 'x-fa', set)
    check(cells(columns)[3] == after[0], 'and back')
    summary[:import_us] = body['import_us']

    code, = api('PUT', '/v1/fonts', { font_id: 'hack', size_px: 16 })
    resized = cells(columns)
    check(code == 200 && (0...3).all? { |i| resized[3 + i] == resized[i] } && resized[0] != after[0],
          'after a size change (16 px) each still matches the font at its own code')
    api('PUT', '/v1/fonts', { font_id: 'hack', size_px: 22 })

    code, = api('DELETE', '/v1/fonts/glyphs/system/x-fa')
    removed = cells(columns)
    check(code == 200 && removed[3, 3] == before[3, 3], "after DELETE (#{code}) they draw the replacement again")
    check(q_answers(ledger_cps) == ['', '', ''], 'and q answers status= empty again')
    api('DELETE', "/v1/terminal-sessions/#{session['id']}")

    # The administrator's own set.
    own_set = F.make_set(File.join(work, 'mine'), name: 'x-mine', release: '1.0', glyphs: F.nerd_codes('dev', 4))
    code, body = F.import(PORT, TOKEN, 'x-mine', own_set)
    check(code == 201 && body['origin'] == 'own' && body['verified'] == false,
          "an unsigned set imported as the administrator's own: #{code} #{body.inspect[0, 160]}")
    _, fonts = api('GET', '/v1/fonts')
    entry = fonts['sets'].find { |s| s['id'] == 'x-mine' }
    check(entry && entry['source'] == 'installed' && entry['install']['verified'] == false &&
          entry['install']['origin'] == 'own' && entry['install']['state'] == 'loaded',
          "GET /v1/fonts reports it installed, own, unverified (#{entry.inspect[0, 240]})")
    code, text = F.http(PORT, TOKEN, 'GET', '/v1/fonts/glyphs/system/x-mine/licence')
    check(code == 200 && text == File.read(F::LICENCE), "its licence is served (#{code})")

    # The font checks: each refused before a glyph loads, nothing changed.
    ttf = File.binread(F::SYMBOLS_FONT)
    tables = ttf.byteslice(4, 2).unpack1('n')
    shifted = ttf.dup
    tables.times do |t|
      at = 12 + 16 * t + 8
      shifted[at, 4] = [ttf.byteslice(at, 4).unpack1('N') + 20].pack('N')
    end
    collection = ['ttcf', 0x00010000, 2, 20, 20].pack('a4NNNN') + shifted
    bdf = "STARTFONT 2.1\nFONT -gate-gate-medium-r-normal--8-80-75-75-c-80-iso10646-1\nSIZE 8 75 75\n" \
          "FONTBOUNDINGBOX 8 8 0 0\nSTARTPROPERTIES 2\nFONT_ASCENT 8\nFONT_DESCENT 0\nENDPROPERTIES\nCHARS 1\n" \
          "STARTCHAR A\nENCODING 65\nSWIDTH 500 0\nDWIDTH 8 0\nBBX 8 8 0 0\nBITMAP\n#{"FF\n" * 8}ENDCHAR\nENDFONT\n"
    base = own_set
    font_cases = [
      ['a collection', base.merge('font' => collection), 'collection'],
      ['a face that is not scalable (BDF)', base.merge('font' => bdf), 'not_scalable'],
      ['not a font', base.merge('font' => 'not a font' * 100), 'not_a_font'],
      ['a code the cmap lacks', base.merge('names' => F.names_json('1.0', { 'gone' => '10fff0' })), 'code_unmapped']
    ]
    font_cases.each do |label, parts, reason|
      digest = F.digest(store)
      _, fonts_before = api('GET', '/v1/fonts')
      code, body = F.import(PORT, TOKEN, 'x-bad', parts)
      _, fonts_after = api('GET', '/v1/fonts')
      check(code == 422 && body['code'] == 'glyph_font_rejected' && body['detail'].to_s.start_with?(reason) &&
            F.digest(store) == digest && fonts_after['sets'] == fonts_before['sets'],
            "#{label}: #{code} #{body['code'] rescue body} #{body['detail'] rescue ''}, nothing changed")
    end
    digest = F.digest(store)
    code, body = F.import(PORT, TOKEN, 'x-bad', base, lengths: { 'font' => (16 << 20) + 1 })
    check(code == 413 && body['code'] == 'glyph_too_large' && F.digest(store) == digest,
          "a font over 16 MiB: #{code} from the header, nothing staged")
    many = (0...8193).to_h { |i| [format('g%05d', i), 'f015'] }
    code, body = F.import(PORT, TOKEN, 'x-bad', base.merge('names' => F.names_json('1.0', many)))
    check(code == 422 && body['code'] == 'glyph_names_invalid' && F.digest(store) == digest,
          "a names file of 8,193 glyphs: #{code} #{body['code'] rescue body}")
    # Cut short: a body that stops; the connection closed.
    Socket.tcp('127.0.0.1', PORT) do |s|
      s.write("PUT /v1/fonts/glyphs/system/x-cut HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer #{TOKEN}\r\n" \
              "Content-Length: 300\r\nX-Blit-Glyph-Parts: names=100:#{'0' * 64} font=100:#{'0' * 64} licence=100:#{'0' * 64}\r\n\r\n")
      s.readpartial(100)
      s.write('x' * 50)
    end
    sleep 0.3
    check(Dir.children(File.join(store, 'staging')).empty? && F.digest(store) == digest,
          'an upload cut short leaves nothing in staging')

    # Routes.
    code, body = F.import(PORT, TOKEN, 'x-mine', base, scope: 'users/1000')
    check(code == 501 && body['code'] == 'glyph_scope_unsupported', "user scope is phase 2b's: #{code}")
    code, = F.import(PORT, nil, 'x-mine', base)
    check(code == 401, "the bearer token is required (#{code})")
    code, body = F.import(PORT, TOKEN, 'X-Bad', base)
    check(code == 400 && body['code'] == 'malformed_query', "a malformed id: #{code}")
    code, body = F.import(PORT, TOKEN, 'blit', base)
    check(code == 400, "a set id without x- is refused (#{code})")
    code, body = F.http(PORT, TOKEN, 'DELETE', '/v1/fonts/glyphs/system/x-none')
    check(code == 404 && body['code'] == 'glyph_set_not_installed', "DELETE of a set not installed: #{code}")
    _, listing = api('GET', '/v1/fonts/glyphs')
    check(listing['limits']['font_bytes'] == 16 << 20 && listing['limits']['glyphs_per_set'] == 8192 &&
          listing['ledger']['first'] == '102000' && listing['ledger']['last'] == '10fffd',
          'GET /v1/fonts/glyphs reports the limits and the block')
  ensure
    F.stop(pid)
  end
end

if $failures.zero?
  puts "GLYPH_INSTALL_GATE_PASS backend=#{GATE_BACKEND} fonts=terminus,hack drawn=3 redrawn=yes resized=yes removed=replacement own=unverified " \
       "font_checks=4 limits=2 import_us=#{summary[:import_us]}"
else
  warn "GLYPH_INSTALL_GATE_FAIL #{$failures} checks failed"
  exit 1
end
