# frozen_string_literal: true

# ADR 0245: what glyph_signature_gate, glyph_ledger_gate, glyph_install_gate
# and glyph_sets_gate share: a scratch glyph store and keys directory (never
# /var/lib/blit or the real release keys), sets made from fonts the
# repository ships, signed with a throwaway key by tools/release/blit-sign's
# glyph-set mode, a server started on them, and the import itself.

require 'digest'
require 'fileutils'
require 'json'
require 'socket'
require 'timeout'
require 'tmpdir'
require_relative 'gate_server'

module GlyphStoreFixture
  ROOT = File.expand_path('../..', __dir__)
  FONTS = File.join(ROOT, 'assets/fonts')
  NERD_FONT = File.join(FONTS, 'SymbolsNerdFontMono-Regular.ttf')
  ICON_FONT = File.join(FONTS, 'BlitIcons-Regular.ttf')
  SYMBOLS_FONT = File.join(FONTS, 'NotoSansSymbols-Regular.ttf')
  LICENCE = File.join(FONTS, 'licenses/NerdFontsSymbolsOnly-LICENSE.txt')
  PARTS = %w[manifest signature names font licence].freeze

  module_function

  # Nerd Fonts' own names and codes (the names file the repository ships):
  # [[name, code], ...] for the prefix, bytewise by name.
  def nerd_codes(prefix = 'fa', count = nil)
    names = JSON.parse(File.read(File.join(FONTS, 'nerd-glyphnames-v3.4.0.json')))
    names.delete('METADATA')
    list = names.filter_map { |key, glyph| [key.sub("#{prefix}-", ''), glyph['code']] if key.start_with?("#{prefix}-") }
    list = list.select { |name, _| name.match?(/\A[a-z0-9][a-z0-9_-]{0,95}\z/) }.sort.uniq(&:last)
    count ? list.first(count) : list
  end

  # Blit's own icons: [[id, code], ...] from the catalogue (single-cell ones).
  def icon_codes(count = nil)
    catalogue = JSON.parse(File.read(File.join(FONTS, 'icons/catalogue.json')))
    list = catalogue.values.grep(Array).flatten.grep(Hash).filter_map do |e|
      next unless e['id'] && e['codepoint'] && !e['cells'] && (e['columns'] || 1) == 1 && (e['rows'] || 1) == 1

      [e['id'], e['codepoint'].downcase]
    end.uniq(&:first).sort
    count ? list.first(count) : list
  end

  # A blit-glyph-names file: glyphs is {name => code} or {name => {code:, aliases:, ...}}.
  def names_json(release, glyphs, licence: 'MIT')
    body = glyphs.to_h do |name, value|
      value = { 'code' => value } unless value.is_a?(Hash)
      [name, value.transform_keys(&:to_s)]
    end
    JSON.generate({ 'format' => 'blit-glyph-names', 'release' => release, 'licence' => licence, 'glyphs' => body })
  end

  # Build blit-sign and make two throwaway keys: keys/gate-1.pub (trusted)
  # and other/other-1.pub (not). Returns a hash of paths.
  def keys(work)
    sign = File.join(work, 'blit-sign')
    system(File.join(ROOT, 'tools/build-tcc.sh'), sign, 'sign', out: File::NULL) or abort 'blit-sign did not build'
    FileUtils.mkdir_p([File.join(work, 'keys'), File.join(work, 'other')])
    k = { sign: sign, keys: File.join(work, 'keys'), secret: File.join(work, 'gate.secret'),
          other_secret: File.join(work, 'other.secret') }
    system(sign, 'keygen', k[:secret], File.join(work, 'keys/gate-1.pub'), out: File::NULL) or abort 'keygen'
    system(sign, 'keygen', k[:other_secret], File.join(work, 'other/other-1.pub'), out: File::NULL) or abort 'keygen'
    k
  end

  # A set's parts in DIR: names, font, licence, and, with SECRET, the
  # manifest and signature blit-sign writes. Returns {part => bytes}.
  def make_set(dir, name:, release:, glyphs:, font: NERD_FONT, licence: LICENCE, secret: nil, sign: nil,
               manifest_name: nil, names_text: nil)
    FileUtils.rm_rf(dir)
    FileUtils.mkdir_p(dir)
    File.write(File.join(dir, 'names'), names_text || names_json(release, glyphs))
    FileUtils.cp(font, File.join(dir, 'font'))
    FileUtils.cp(licence, File.join(dir, 'licence'))
    if secret
      ok = system(sign, 'glyphs', secret, manifest_name || name.sub(/\Ax-/, ''), release,
                  File.join(dir, 'font'), File.join(dir, 'names'), File.join(dir, 'licence'), dir, out: File::NULL)
      abort "blit-sign glyphs failed for #{name}" unless ok
      File.rename(File.join(dir, 'glyphs.manifest'), File.join(dir, 'manifest'))
      File.rename(File.join(dir, 'glyphs.sig'), File.join(dir, 'signature'))
    end
    PARTS.to_h { |part| [part, File.exist?(File.join(dir, part)) ? File.binread(File.join(dir, part)) : nil] }.compact
  end

  # A fresh store directory (0700) under WORK.
  def store(work, name = 'store')
    path = File.join(work, name)
    FileUtils.rm_rf(path)
    Dir.mkdir(path, 0o700)
    File.chmod(0o700, path)
    path
  end

  # Every file under the store, with its bytes: "nothing changed" compares two.
  def digest(store)
    files = Dir.glob(File.join(store, '**', '*'), File::FNM_DOTMATCH).reject { |f| File.basename(f).start_with?('.') && File.directory?(f) }
    Digest::SHA256.hexdigest(files.sort.map do |f|
      rel = f.delete_prefix(store)
      File.file?(f) ? "#{rel}\0#{Digest::SHA256.file(f).hexdigest}\n" : "#{rel}/\n"
    end.join)
  end

  def start(port, store:, keys:, token:, root:, extra: {})
    env = gate_display_env(width: 640, height: 384).merge(
      'BLIT_BIND_ADDRESS' => '127.0.0.1', 'BLIT_AUTH_TOKEN' => token, 'BLIT_LOGIN_AUTOSTART' => '0',
      'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_FONT_ROOT' => FONTS, 'BLIT_FONT_LOCAL_ROOT' => File.join(root, 'local'),
      'BLIT_PSF_FONT' => File.join(FONTS, 'Blit-Terminus16.psf'), 'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'),
      'BLIT_GLYPH_STORE' => store, 'BLIT_GLYPH_KEYS_DIR' => keys
    ).merge(extra)
    pid = gate_spawn(env, ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server')), port.to_s,
                     out: File::NULL, err: File::NULL)
    Timeout.timeout(20) do
      loop do
        break if (http(port, token, 'GET', '/v1/version').first rescue nil) == 200

        sleep 0.1
      end
    end
    pid
  end

  def stop(pid)
    return unless pid

    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end

  def http(port, token, method, path, body: nil, headers: {})
    Socket.tcp('127.0.0.1', port, connect_timeout: 5) do |socket|
      head = +"#{method} #{path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
      head << "Authorization: Bearer #{token}\r\n" if token && !token.empty?
      headers.each { |k, v| head << "#{k}: #{v}\r\n" }
      head << "Content-Length: #{body.bytesize}\r\n" if body
      socket.write(head + "\r\n")
      socket.write(body) if body
      response = socket.read.to_s
      parse(response)
    end
  end

  def parse(response)
    response = response.sub(/\AHTTP\/1\.1 100 Continue\r\n\r\n/, '')
    head, text = response.split("\r\n\r\n", 2)
    code = head.to_s[/\AHTTP\/1\.1 (\d+)/, 1].to_i
    [code, (JSON.parse(text.to_s) rescue text.to_s)]
  end

  # PUT the parts (in the body's order) with X-Blit-Glyph-Parts. SHAS
  # overrides a part's transport hash; HEADER replaces the whole header.
  # The body is sent only after the server's 100 Continue; SENT, if given,
  # is called once the whole body is written.
  def import(port, token, set, parts, scope: 'system', shas: {}, header: nil, lengths: {}, sent: nil)
    order = PARTS.select { |p| parts.key?(p) }
    header ||= order.map do |p|
      "#{p}=#{lengths.fetch(p, parts[p].bytesize)}:#{shas.fetch(p, Digest::SHA256.hexdigest(parts[p]))}"
    end.join(' ')
    body = order.map { |p| parts[p].b }.join
    Socket.tcp('127.0.0.1', port, connect_timeout: 5) do |socket|
      head = +"PUT /v1/fonts/glyphs/#{scope}/#{set} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
      head << "Authorization: Bearer #{token}\r\n" if token && !token.empty?
      head << "Content-Length: #{body.bytesize}\r\nX-Blit-Glyph-Parts: #{header}\r\n\r\n"
      socket.write(head)
      first = +''
      Timeout.timeout(30) { first << socket.readpartial(65_536) until first.include?("\r\n\r\n") }
      return parse(first + (socket.read.to_s rescue '')) unless first.start_with?('HTTP/1.1 100')

      rest = first.sub(/\AHTTP\/1\.1 100 Continue\r\n\r\n/, '')
      begin
        socket.write(body)
      rescue Errno::EPIPE, Errno::ECONNRESET
        nil
      end
      sent&.call
      parse(rest + (socket.read.to_s rescue ''))
    end
  rescue EOFError, Errno::ECONNRESET => e
    [0, e.message]
  end
end
