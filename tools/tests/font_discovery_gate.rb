#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0195: the font list is discovered, not written by hand.
#   * every shipped text font in fonts.source.json is listed, by its family
#     name in kebab form, source "shipped", available, with an 11x22 cell at
#     22 px like every other
#   * the narrow-cell PSFs are pixel profiles; the square wide-cell PSF is
#     listed as rejected, with its reason
#   * a monospaced font copied into the local text directory appears on the
#     next listing, source "local", with no restart, and can be selected
#   * a proportional one is listed as rejected: "not monospaced"
#   * a font removed from the local directory leaves the list again

require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19549'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
MONO = ENV.fetch('BLIT_GATE_LOCAL_MONO', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
PROPORTIONAL = ENV.fetch('BLIT_GATE_LOCAL_PROPORTIONAL', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  [response.code.to_i, JSON.parse(response.body)]
end

def check(ok, why)
  raise "FONT_DISCOVERY_FAIL #{why}" unless ok
end

def kebab(name)
  name.downcase.scan(/[a-z0-9]+/).join('-')
end

manifest = JSON.parse(File.read(File.join(ROOT, 'assets/fonts/fonts.source.json')))
abort "font discovery gate: needs #{MONO} and #{PROPORTIONAL}" unless File.file?(MONO) && File.file?(PROPORTIONAL)

Dir.mktmpdir('blit-font-discovery') do |root|
  local = File.join(root, 'local')
  FileUtils.mkdir_p(File.join(local, 'text'))
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'), 'BLIT_FONT_LOCAL_ROOT' => local,
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    fonts = request('GET', '/v1/fonts')[1]
    outline = fonts['profiles'].select { |p| p['rasterization'] == 'native-antialiased' }
    check(outline.size == manifest['text'].size,
          "#{outline.size} outline profiles for #{manifest['text'].size} shipped text fonts")
    outline.each do |profile|
      check(profile['source'] == 'shipped' && profile['available'], "#{profile['id']}: #{profile}")
      check(profile['id'] == kebab(profile['title']), "#{profile['id']} is not its family name #{profile['title']}")
      code, reply = request('PUT', '/v1/fonts', { font_id: profile['id'], size_px: 22 })
      check(code == 200 && reply.dig('active', 'cell_pixels') == { 'width' => 11, 'height' => 22 },
            "#{profile['id']} at 22 px -> #{code} #{reply['active']&.fetch('cell_pixels', nil)}")
    end
    pixel = fonts['profiles'].select { |p| p['rasterization'] == 'bitmap-nearest' }.map { |p| p['id'] }.sort
    check(pixel == %w[blit-spleen16 blit-terminus16], "pixel profiles #{pixel}")
    wide = fonts['rejected'].find { |r| r['path'].end_with?('Blit-Unifont16.psf') }
    check(wide && wide['reason'] == 'a wide-cell font', "the wide-cell PSF is not reported: #{fonts['rejected']}")

    FileUtils.cp(MONO, File.join(local, 'text'))
    FileUtils.cp(PROPORTIONAL, File.join(local, 'text'))
    fonts = request('GET', '/v1/fonts')[1]
    added = fonts['profiles'].find { |p| p['source'] == 'local' }
    check(added && added['available'] && added['id'] == kebab(added['title']),
          "a local monospaced font did not appear: #{fonts['profiles'].map { |p| p['id'] }}")
    code, reply = request('PUT', '/v1/fonts', { font_id: added['id'], size_px: 22 })
    check(code == 200 && reply.dig('active', 'id') == added['id'], "selecting #{added['id']} -> #{code}")
    refused = fonts['rejected'].find { |r| r['path'].end_with?(File.basename(PROPORTIONAL)) }
    check(refused && refused['reason'] == 'not monospaced', "the proportional font is not refused: #{fonts['rejected']}")

    request('PUT', '/v1/fonts', { font_id: outline.first['id'] })
    FileUtils.rm(Dir.glob(File.join(local, 'text', '*')))
    fonts = request('GET', '/v1/fonts')[1]
    check(fonts['profiles'].none? { |p| p['source'] == 'local' }, 'a removed local font is still listed')
    puts "FONT_DISCOVERY_PASS shipped=#{outline.size} pixel=#{pixel.size} cell=11x22_each " \
         "local=#{added['id']}_live rejected=wide_cell,not_monospaced removal=live"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
