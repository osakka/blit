#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0218: fonts.hinting through the API and the settings document. Each of
# full, light and none is accepted, reported, stored and re-rendered; anything
# else is refused and changes nothing; the choice survives a restart; and the
# default is full, which is what text was drawn with before the setting
# existed. The raster-level claims (byte-identical default, a fitted icon that
# ignores the setting) are font_hinting_gate.c's.

require 'digest'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19352'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
abort("font hinting gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "hinting-#{rand(1 << 30)}" if method == 'POST'
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) { |http| http.request(req) }
  [response.code.to_i, response.body.to_s]
end

def json(method, path, body = nil, headers = {})
  code, text = request(method, path, body, headers)
  [code, (JSON.parse(text) rescue text)]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "font hinting gate: FAIL #{message}"
end

$server = nil
def start(env)
  $server = spawn({ **gate_display_env, 'BLIT_BIND_ADDRESS' => '127.0.0.1',
                    'BLIT_AUTH_TOKEN' => '', 'BLIT_LOGIN_AUTOSTART' => '0',
                    'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_FONT_SCALE' => '1',
                    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts') }.merge(env),
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

def hinting = json('GET', '/v1/fonts').last.dig('active', 'hinting')

# What the display shows, as a digest: a change that re-renders changes it.
def shown
  sleep 0.3
  Digest::SHA256.hexdigest(request('GET', '/v1/diagnostics/screenshots/display-1').last)
end

Dir.mktmpdir('blit-hinting') do |root|
  document = File.join(root, 'settings.json')
  env = { 'BLIT_SETTINGS_PATH' => document }
  start(env)

  code, fonts = json('GET', '/v1/fonts')
  check(code == 200 && fonts.dig('active', 'hinting') == 'full',
        "the default is full, got #{fonts.is_a?(Hash) ? fonts.dig('active', 'hinting').inspect : fonts}")
  hack = fonts.fetch('profiles').find { |p| p['id'] == 'hack' && p['available'] }
  abort('font hinting gate: the hack profile is not available') unless hack

  code, = json('PUT', '/v1/fonts', { 'font_id' => 'hack', 'size_px' => 12 })
  check(code == 200, "selecting hack at 12px -> #{code}")
  code, = json('POST', '/v1/terminal-sessions',
               { 'command' => '/bin/sh',
                 'arguments' => ['-c', "printf '\\033[?25l\\033[2J\\033[HgSa@Wks egSa@Wks'; sleep 600"],
                 'display_id' => 'display-1', 'surface' => { 'width' => 128, 'height' => 48 } })
  check(code.between?(200, 299), "terminal session -> #{code}")
  sleep 0.5

  seen = {}
  %w[none light full].each do |value|
    code, reply = json('PUT', '/v1/fonts', { 'hinting' => value })
    check(code == 200 && reply.is_a?(Hash) && reply.dig('active', 'hinting') == value,
          "hinting #{value} is accepted and reported, got #{code} #{reply.inspect[0, 200]}")
    check(hinting == value, "hinting #{value} reads back")
    check(reply.is_a?(Hash) && reply.dig('active', 'size_px') == 12 &&
          reply.dig('active', 'id') == 'hack', "hinting #{value} leaves the font and size alone")
    seen[value] = shown
  end
  check(seen.values.uniq.length == 3,
        'none, light and full each re-render the display differently')

  # Refusals change nothing.
  %w[Full medium hinted].push('', 1, nil, true).each do |bad|
    code, reply = json('PUT', '/v1/fonts', { 'hinting' => bad })
    check(code == 400 && reply.is_a?(Hash) && reply['code'] == 'invalid_font_hinting',
          "hinting #{bad.inspect} is refused with invalid_font_hinting, got #{code} #{reply.inspect[0, 120]}")
  end
  check(hinting == 'full', 'a refused hinting leaves the setting as it was')
  check(shown == seen['full'], 'a refused hinting leaves the display as it was')

  # The same setting again is not a font change.
  _, before = json('GET', '/v1/fonts')
  json('PUT', '/v1/fonts', { 'hinting' => 'full' })
  _, after = json('GET', '/v1/fonts')
  check(before.dig('active', 'raster_cache', 'renders') <= after.dig('active', 'raster_cache', 'renders'),
        'an unchanged hinting does not rebuild the raster cache')

  # It is part of the settings document, stored with the font.
  json('PUT', '/v1/fonts', { 'hinting' => 'none' })
  Timeout.timeout(10) { sleep 0.1 until File.exist?(document) && File.read(document).include?('"hinting"') }
  _, settings = json('GET', '/v1/settings')
  check(settings.dig('document', 'font', 'hinting') == 'none' &&
        settings.dig('document', 'font', 'font_id') == 'hack',
        "the settings document stores hinting with the font, got #{settings.dig('document', 'font').inspect}")

  # The settings door validates it as well.
  replacement = Marshal.load(Marshal.dump(settings.fetch('document')))
  replacement['font']['hinting'] = 'sharp'
  code, reply = json('PUT', '/v1/settings', { 'document' => replacement },
                     { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 400 && reply.is_a?(Hash) && reply['code'] == 'invalid_settings_font',
        "a settings document with a bad hinting is refused, got #{code} #{reply.inspect[0, 120]}")
  check(hinting == 'none', 'a refused settings document leaves hinting as it was')

  replacement['font']['hinting'] = 'light'
  code, = json('PUT', '/v1/settings', { 'document' => replacement },
               { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 200 && hinting == 'light', "a settings document can set hinting, got #{code} #{hinting.inspect}")

  # Survives a restart.
  json('PUT', '/v1/fonts', { 'hinting' => 'none' })
  Timeout.timeout(10) do
    sleep 0.1 until JSON.parse(File.read(document)).dig('font', 'hinting') == 'none'
  end
  stop
  start(env)
  check(hinting == 'none', "hinting survives a restart, got #{hinting.inspect}")
  _, fonts = json('GET', '/v1/fonts')
  check(fonts.dig('active', 'id') == 'hack' && fonts.dig('active', 'size_px') == 12,
        'the font and size survive with it')

  # A stored document with no hinting is full.
  _, settings = json('GET', '/v1/settings')
  bare = Marshal.load(Marshal.dump(settings.fetch('document')))
  bare['font'].delete('hinting')
  code, = json('PUT', '/v1/settings', { 'document' => bare }, { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 200 && hinting == 'full', "a document without hinting means full, got #{code} #{hinting.inspect}")

  # Reset restores the boot default.
  json('PUT', '/v1/fonts', { 'hinting' => 'light' })
  code, = json('DELETE', '/v1/settings')
  check(code == 200 && hinting == 'full', "resetting settings restores full, got #{code} #{hinting.inspect}")
end

unless $failures.empty?
  warn "font hinting gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'FONT_HINTING_API_PASS'
