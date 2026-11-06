#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0220: fonts.icon_colour through the API and the settings document. Each
# of colour, black and white is accepted, reported, stored and re-rendered;
# anything else is refused and changes nothing; the choice survives a restart;
# and the default is colour, which is what the icons were drawn with before
# the setting existed. The raster-level claims (byte-identical default, the
# knock-out badge, text and chrome that ignore the setting) are
# font_icon_colour_gate.c's.

require 'digest'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19353'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
abort("font icon colour gate: missing #{BINARY}") unless File.executable?(BINARY)

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
  req['Idempotency-Key'] = "icon-colour-#{rand(1 << 30)}" if method == 'POST'
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
  warn "font icon colour gate: FAIL #{message}"
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

def icon_colour = json('GET', '/v1/fonts').last.dig('active', 'icon_colour')

# What the display shows, as a digest: a change that re-renders changes it.
def shown
  sleep 0.3
  Digest::SHA256.hexdigest(request('GET', '/v1/diagnostics/screenshots/display-1').last)
end

Dir.mktmpdir('blit-icon-colour') do |root|
  document = File.join(root, 'settings.json')
  env = { 'BLIT_SETTINGS_PATH' => document }
  start(env)

  code, fonts = json('GET', '/v1/fonts')
  check(code == 200 && fonts.dig('active', 'icon_colour') == 'colour',
        "the default is colour, got #{fonts.is_a?(Hash) ? fonts.dig('active', 'icon_colour').inspect : fonts}")
  hack = fonts.fetch('profiles').find { |p| p['id'] == 'hack' && p['available'] }
  abort('font icon colour gate: the hack profile is not available') unless hack

  code, = json('PUT', '/v1/fonts', { 'font_id' => 'hack', 'size_px' => 22 })
  check(code == 200, "selecting hack at 22px -> #{code}")
  # hibr-pen, battery-4, hui (both columns), blit, then text and a window glyph.
  icons = '\\364\\200\\200\\201 \\364\\200\\200\\215 \\364\\200\\200\\230\\364\\200\\200\\231 \\364\\200\\200\\200 hui'
  code, = json('POST', '/v1/terminal-sessions',
               { 'command' => '/bin/sh',
                 'arguments' => ['-c', "printf '\\033[?25l\\033[2J\\033[H#{icons}'; sleep 600"],
                 'display_id' => 'display-1', 'surface' => { 'width' => 320, 'height' => 64 } })
  check(code.between?(200, 299), "terminal session -> #{code}")
  sleep 0.5

  seen = {}
  %w[black white colour].each do |value|
    code, reply = json('PUT', '/v1/fonts', { 'icon_colour' => value })
    check(code == 200 && reply.is_a?(Hash) && reply.dig('active', 'icon_colour') == value,
          "icon_colour #{value} is accepted and reported, got #{code} #{reply.inspect[0, 200]}")
    check(icon_colour == value, "icon_colour #{value} reads back")
    check(reply.is_a?(Hash) && reply.dig('active', 'size_px') == 22 &&
          reply.dig('active', 'id') == 'hack' && reply.dig('active', 'hinting') == 'full',
          "icon_colour #{value} leaves the font, size and hinting alone")
    seen[value] = shown
  end
  check(seen.values.uniq.length == 3,
        'colour, black and white each re-render the display differently')

  # Refusals change nothing.
  ['Black', 'grey', 'mono', '', 1, nil, true].each do |bad|
    code, reply = json('PUT', '/v1/fonts', { 'icon_colour' => bad })
    check(code == 400 && reply.is_a?(Hash) && reply['code'] == 'invalid_font_icon_colour',
          "icon_colour #{bad.inspect} is refused with invalid_font_icon_colour, got #{code} #{reply.inspect[0, 120]}")
  end
  check(icon_colour == 'colour', 'a refused icon_colour leaves the setting as it was')
  check(shown == seen['colour'], 'a refused icon_colour leaves the display as it was')

  # The same setting again is not a font change.
  _, before = json('GET', '/v1/fonts')
  json('PUT', '/v1/fonts', { 'icon_colour' => 'colour' })
  _, after = json('GET', '/v1/fonts')
  check(before.dig('active', 'raster_cache', 'renders') <= after.dig('active', 'raster_cache', 'renders'),
        'an unchanged icon_colour does not rebuild the raster cache')

  # It is part of the settings document, stored with the font.
  json('PUT', '/v1/fonts', { 'icon_colour' => 'white' })
  Timeout.timeout(10) { sleep 0.1 until File.exist?(document) && File.read(document).include?('"icon_colour"') }
  _, settings = json('GET', '/v1/settings')
  check(settings.dig('document', 'font', 'icon_colour') == 'white' &&
        settings.dig('document', 'font', 'font_id') == 'hack',
        "the settings document stores icon_colour with the font, got #{settings.dig('document', 'font').inspect}")

  # The settings door validates it as well.
  replacement = Marshal.load(Marshal.dump(settings.fetch('document')))
  replacement['font']['icon_colour'] = 'grey'
  code, reply = json('PUT', '/v1/settings', { 'document' => replacement },
                     { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 400 && reply.is_a?(Hash) && reply['code'] == 'invalid_settings_font',
        "a settings document with a bad icon_colour is refused, got #{code} #{reply.inspect[0, 120]}")
  check(icon_colour == 'white', 'a refused settings document leaves icon_colour as it was')

  replacement['font']['icon_colour'] = 'black'
  code, = json('PUT', '/v1/settings', { 'document' => replacement },
               { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 200 && icon_colour == 'black', "a settings document can set icon_colour, got #{code} #{icon_colour.inspect}")

  # Survives a restart.
  json('PUT', '/v1/fonts', { 'icon_colour' => 'white' })
  Timeout.timeout(10) do
    sleep 0.1 until JSON.parse(File.read(document)).dig('font', 'icon_colour') == 'white'
  end
  stop
  start(env)
  check(icon_colour == 'white', "icon_colour survives a restart, got #{icon_colour.inspect}")
  _, fonts = json('GET', '/v1/fonts')
  check(fonts.dig('active', 'id') == 'hack' && fonts.dig('active', 'size_px') == 22,
        'the font and size survive with it')

  # A stored document with no icon_colour is colour.
  _, settings = json('GET', '/v1/settings')
  bare = Marshal.load(Marshal.dump(settings.fetch('document')))
  bare['font'].delete('icon_colour')
  code, = json('PUT', '/v1/settings', { 'document' => bare }, { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 200 && icon_colour == 'colour', "a document without icon_colour means colour, got #{code} #{icon_colour.inspect}")

  # Reset restores the boot default.
  json('PUT', '/v1/fonts', { 'icon_colour' => 'black' })
  code, = json('DELETE', '/v1/settings')
  check(code == 200 && icon_colour == 'colour', "resetting settings restores colour, got #{code} #{icon_colour.inspect}")
end

unless $failures.empty?
  warn "font icon colour gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'FONT_ICON_COLOUR_API_PASS'
