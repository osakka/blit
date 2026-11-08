#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0227: Spleen is one text font of five masters, picked per size.
#   * `spleen` is listed once, with its five masters, and no master is a font
#     of its own; the retired `blit-spleen16` is gone and cannot be selected
#   * the master at each size follows the one rule: at 12, 16, 24, 32, 48 and
#     64 px the largest master whose design height divides the size (crisp);
#     at 20, 28 and 40 px the largest below it (scaled)
#   * at an exact multiple a glyph's pixels are purely on or off: in the
#     screenshot, the 'A' cell holds only the foreground and the background,
#     at 16 and 32 px (and at every crisp size above); scaled, the same text
#     has in-between coverage, which shows the check can see it
#   * a saved `blit-spleen16` falls back to the boot font, as any retired id
# BLIT_GATE_SPLEEN_SHOTS=<dir> also keeps the screenshots, spleen-<size>.bmp.

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_bmp'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19559'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
SHOTS = ENV.fetch('BLIT_GATE_SPLEEN_SHOTS', '')
MASTERS = %w[6x12 8x16 12x24 16x32 32x64].freeze
EXPECTED = { 12 => %w[6x12 crisp], 16 => %w[8x16 crisp], 24 => %w[12x24 crisp], 32 => %w[16x32 crisp],
             48 => %w[12x24 crisp], 64 => %w[32x64 crisp], 20 => %w[8x16 scaled],
             28 => %w[12x24 scaled], 40 => %w[16x32 scaled] }.freeze
TEXT = 'Spleen gAj@ 0123 {}[]|~'
abort("spleen master gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['Idempotency-Key'] = "spleen-#{rand(1 << 30)}" if method == 'POST'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 30) { |h| h.request(req) }
  [response.code.to_i, response.body.to_s]
end

def json(method, path, body = nil)
  code, text = request(method, path, body)
  [code, (JSON.parse(text) rescue text)]
end

$failures = []
def check(ok, why)
  return if ok

  $failures << why
  warn "SPLEEN_MASTER_FAIL #{why}"
end

def start(env)
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(20) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  pid
end

def stop(pid)
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

# The colours in a rectangle of the screenshot, with their counts.
def colours(bmp, x0, y0, width, height)
  seen = Hash.new(0)
  (y0...(y0 + height)).each { |y| (x0...(x0 + width)).each { |x| seen[bmp.pixel(x, y)] += 1 } }
  seen
end

Dir.mktmpdir('blit-spleen-master') do |work|
  settings = File.join(work, 'settings.json')
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'), 'BLIT_FONT_LOCAL_ROOT' => File.join(work, 'local'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => settings)
  pid = start(env)
  begin
    fonts = json('GET', '/v1/fonts').last
    ids = fonts.fetch('profiles').map { |p| p['id'] }
    spleen = fonts['profiles'].select { |p| p['id'] == 'spleen' }
    check(spleen.size == 1, "spleen listed #{spleen.size} times: #{ids}")
    spleen = spleen.first || {}
    check(spleen['title'] == 'Spleen' && spleen['source'] == 'shipped' && spleen['available'] &&
          spleen['rasterization'] == 'native-antialiased', "spleen profile #{spleen}")
    check(spleen['masters'] == MASTERS, "spleen masters #{spleen['masters'].inspect}")
    check(File.directory?(spleen['path'].to_s), "spleen's path is not its directory: #{spleen['path']}")
    alone = fonts['profiles'].select { |p| p['id'] =~ /\Aspleen-/ || p['path'].to_s.include?('/text/spleen/') }
    check(alone.empty?, "a master is listed as a font of its own: #{alone.map { |p| p['id'] }}")
    check(fonts['rejected'].none? { |r| r['path'].include?('/text/spleen/') }, "a master is rejected: #{fonts['rejected']}")
    check(!ids.include?('blit-spleen16'), "the retired blit-spleen16 is listed: #{ids}")
    code, = json('PUT', '/v1/fonts', { font_id: 'blit-spleen16' })
    check(code != 200, "the retired blit-spleen16 can be selected: #{code}")
    others = fonts['profiles'].reject { |p| p['id'] == 'spleen' }
    check(others.none? { |p| p.key?('masters') }, 'a single-file font reports masters')

    code, = json('POST', '/v1/terminal-sessions',
                 { 'command' => '/bin/sh', 'arguments' => ['-c', "printf '\\033[?25l\\033[2J\\033[H#{TEXT}'; sleep 600"],
                   'display_id' => 'display-1', 'surface' => { 'width' => 80, 'height' => 24 } })
    check(code.between?(200, 299), "terminal session -> #{code}")
    sleep 0.5
    pure = []
    # Smallest first: the line wraps only at 64 px, the last, 20 columns wide.
    EXPECTED.sort.each do |size, (master, fit)|
      code, reply = json('PUT', '/v1/fonts', { font_id: 'spleen', size_px: size })
      active = reply.is_a?(Hash) ? reply.fetch('active', {}) : {}
      check(code == 200 && active['id'] == 'spleen' && active.dig('cell_pixels', 'height') == size,
            "spleen at #{size} px -> #{code} #{active.slice('id', 'cell_pixels')}")
      check(active['master'] == master && active['master_fit'] == fit,
            "#{size} px draws #{active['master'].inspect} #{active['master_fit'].inspect}, not #{master} #{fit}")
      sleep 0.4
      shot = request('GET', '/v1/diagnostics/screenshots/display-1').last
      File.binwrite(File.join(SHOTS, "spleen-#{size}.bmp"), shot) unless SHOTS.empty?
      bmp = GateBmp.new(shot)
      width = active.dig('cell_pixels', 'width').to_i
      # The grid starts at the display's top left at these sizes: 640x384
      # holds whole cells at 12..64 px except where a remainder is centred,
      # so the band is located from the text's own ink, not assumed.
      band = colours(bmp, 0, 0, bmp.width, [size * 2, bmp.height].min)
      background = bmp.pixel(bmp.width - 1, 0)
      foreground = band.reject { |c, _| c == background }.max_by { |_, n| n }&.first
      if fit == 'crisp'
        cell = colours(bmp, TEXT.index('A') * width, 0, width, size)
        check(cell.keys.sort == [background, foreground].sort,
              "'A' at #{size} px is not pure on/off: #{cell.size} colours #{cell.keys.first(6)}")
        check(band.keys.sort == [background, foreground].sort,
              "the text at #{size} px has in-between coverage: #{band.size} colours")
        pure << size
      else
        check(band.size > 2, "the scaled text at #{size} px shows no in-between coverage, so the check is blind")
      end
    end
    check(pure.include?(16) && pure.include?(32), "the pure check did not run at 16 and 32 px: #{pure}")
  ensure
    stop(pid)
  end

  # A saved id that no longer exists falls back to the boot font. The
  # document the server stored names spleen and is read back first, so the
  # fallback is the retired id's doing, not an unreadable document's.
  Timeout.timeout(10) { sleep 0.1 until (JSON.parse(File.read(settings)).dig('font', 'font_id') rescue nil) == 'spleen' }
  stored = JSON.parse(File.read(settings))
  { 'spleen' => 'spleen', 'blit-spleen16' => 'blit-terminus16' }.each do |saved, expected|
    stored['font']['font_id'] = saved
    File.write(settings, JSON.generate(stored))
    pid = start(env.merge('BLIT_SETTINGS' => 'load'))
    begin
      active = json('GET', '/v1/fonts').last.fetch('active', {})
      check(active['id'] == expected, "a saved #{saved} starts as #{active['id'].inspect}, not #{expected}")
    ensure
      stop(pid)
    end
  end
end

unless $failures.empty?
  warn "spleen master gate: #{$failures.length} failure(s)"
  exit 1
end
puts "SPLEEN_MASTER_PASS listed=once masters=#{MASTERS.join(',')} " \
     "crisp=#{EXPECTED.select { |_, v| v[1] == 'crisp' }.map { |s, v| "#{s}:#{v[0]}" }.join(',')} " \
     "scaled=#{EXPECTED.select { |_, v| v[1] == 'scaled' }.map { |s, v| "#{s}:#{v[0]}" }.join(',')} " \
     'pure=12,16,24,32,48,64 retired=blit-spleen16'
