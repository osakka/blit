#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0221 removed the global fonts.icon_colour setting (ADR 0220); the icon
# pen is a cell attribute now (SGR 68). A settings document saved by 0.3.48
# still holds "font": {..., "icon_colour": ...}. This gate writes such a
# document (with "black", not the old default, so a value that was still
# applied would show) and starts the server on it:
#
# - the document loads: the stored font and size are applied, not the boot
#   font, and nothing in it is refused;
# - the key is not reported back: GET /v1/fonts' active font has no
#   icon_colour, and the settings document and the effective settings the
#   server reports have none, before and after the document is next written;
# - the icon draws in colour, the pen of a cell with no SGR 68: its pixels
#   are the same as on a server started with no icon_colour in the document;
# - PUT /v1/fonts with only icon_colour is refused as an empty request
#   (font_configuration_empty), and a settings document carrying it is
#   accepted with the key dropped, so nothing about it is a setting any more.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19353'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
abort("font icon colour settings gate: missing #{BINARY}") unless File.executable?(BINARY)

def json(method, path, body = nil, headers = {})
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 30) { |http| http.request(req) }
  text = response.body.to_s
  [response.code.to_i, (JSON.parse(text) rescue text)]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "font icon colour settings gate: FAIL #{message}"
end

$server = nil
def start(document)
  $server = gate_spawn({ **gate_display_env, 'BLIT_BIND_ADDRESS' => '127.0.0.1',
                    'BLIT_AUTH_TOKEN' => '', 'BLIT_LOGIN_AUTOSTART' => '0',
                    'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_FONT_SCALE' => '1',
                    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'),
                    'BLIT_SETTINGS_PATH' => document },
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  Timeout.timeout(20) do
    loop do
      break if (json('GET', '/v1/version').first rescue nil) == 200

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

def deep_key?(value, key)
  case value
  when Hash then value.key?(key) || value.values.any? { |v| deep_key?(v, key) }
  when Array then value.any? { |v| deep_key?(v, key) }
  else false
  end
end

# The battery-2 icon in a cell with no SGR 68, as the screenshot shows it.
def icon_pixels
  code, session = json('POST', '/v1/terminal-sessions',
                       { 'command' => '/bin/sh', 'display_id' => 'display-1',
                         'arguments' => ['-c', "printf '\\033[?25l\\033[2J\\033[H\\033[48;2;30;30;34;38;2;230;230;235m\\364\\200\\200\\213'; sleep 600"] })
  check(code == 201, "session -> #{code}")
  sleep 1.0
  grid = json('GET', '/v1/topology').last['panes'][0]['cell_grid']
  shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
  pixels = (0...grid['cell_height']).flat_map do |dy|
    (0...grid['cell_width']).map { |dx| shot.pixel(grid['origin_x'] + dx, grid['origin_y'] + dy) }
  end
  json('DELETE', "/v1/terminal-sessions/#{session['id']}")
  pixels
end

Dir.mktmpdir('blit-icon-colour-settings') do |root|
  document = File.join(root, 'settings.json')
  # A document as this server writes it, with hack at 22 px.
  start(document)
  code, = json('PUT', '/v1/fonts', { 'font_id' => 'hack', 'size_px' => 22 })
  check(code == 200, "selecting hack at 22 px -> #{code}")
  Timeout.timeout(10) { sleep 0.1 until File.exist?(document) && File.read(document).include?('"size_px"') }
  plain = icon_pixels
  stop
  stored = JSON.parse(File.read(document))
  check(stored.dig('font', 'font_id') == 'hack' && !deep_key?(stored, 'icon_colour'),
        "the server wrote #{stored['font'].inspect}")

  # As 0.3.48 saved it: the same, with icon_colour in the font object.
  stored['font']['icon_colour'] = 'black'
  File.write(document, JSON.generate(stored))
  start(document)
  code, fonts = json('GET', '/v1/fonts')
  active = fonts.is_a?(Hash) ? fonts['active'] : {}
  check(code == 200 && active['id'] == 'hack' && active['size_px'] == 22,
        "a document with font.icon_colour does not load: active #{active.inspect[0, 200]}")
  check(!active.key?('icon_colour'), "GET /v1/fonts reports icon_colour: #{active['icon_colour'].inspect}")
  code, settings = json('GET', '/v1/settings')
  check(code == 200 && settings.dig('document', 'font', 'font_id') == 'hack' &&
        settings.dig('document', 'font', 'size_px') == 22,
        "the settings document does not hold the stored font: #{settings.inspect[0, 300]}")
  check(!deep_key?(settings, 'icon_colour'),
        "GET /v1/settings reports icon_colour: #{settings.inspect[0, 300]}")
  check(icon_pixels == plain, 'the icon does not draw in colour on the old document')

  # The key means nothing to either door now.
  code, reply = json('PUT', '/v1/fonts', { 'icon_colour' => 'white' })
  check(code == 400 && reply.is_a?(Hash) && reply['code'] == 'font_configuration_empty',
        "PUT /v1/fonts {icon_colour} -> #{code} #{reply.inspect[0, 160]}, not font_configuration_empty")
  replacement = Marshal.load(Marshal.dump(settings.fetch('document')))
  replacement['font']['icon_colour'] = 'white'
  code, reply = json('PUT', '/v1/settings', { 'document' => replacement },
                     { 'If-Match' => settings.fetch('revision').to_s })
  check(code == 200, "a settings document carrying icon_colour is refused: #{code} #{reply.inspect[0, 160]}")
  _, settings = json('GET', '/v1/settings')
  check(!deep_key?(settings, 'icon_colour'), 'icon_colour came back after a settings write')
  Timeout.timeout(10) { sleep 0.1 until File.exist?(document) && !File.read(document).include?('icon_colour') }
  check(!File.read(document).include?('icon_colour'), 'the rewritten document keeps icon_colour')
end

unless $failures.empty?
  warn "font icon colour settings gate: #{$failures.length} failure(s)"
  exit 1
end
puts 'FONT_ICON_COLOUR_SETTINGS_PASS stored_font.icon_colour=ignored reported=no rewritten=dropped ' \
     'icon=colour put_fonts=font_configuration_empty'
