#!/usr/bin/env ruby
# frozen_string_literal: true

# tap_to_click, the touchpad's own per-device setting, end to end through the
# API: offered only where it can be set (a touchpad, never a mouse), off by
# default, written with PATCH and read back, refused with 422 on a device
# that is not a touchpad and with 400 when it is not a boolean, and kept in
# the settings document Blit saves. The arithmetic a tap goes through is
# touchpad_gate.c's.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19253'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("tap setting gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def request(method, path, body: nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch }.fetch(method)
  req = klass.new(uri)
  req['Content-Type'] = 'application/json' if body
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) { |h| h.request(req) }
  parsed = begin
    response.body.to_s.empty? ? nil : JSON.parse(response.body)
  rescue JSON::ParserError
    response.body
  end
  [response.code.to_i, parsed]
end

def device(name)
  code, body = request('GET', '/v1/input/devices')
  raise "collection -> #{code}" unless code == 200

  body.fetch('devices').find { |d| d['name'] == name }
end

Dir.mktmpdir('blit-tap') do |root|
  File.write(File.join(root, 'event0'), '')
  File.write(File.join(root, 'event0.identity'),
             "bustype=0003\nvendor=046d\nproduct=c077\nversion=0111\nname=Gate Mouse\n" \
             "kinds=pointer\ncapabilities=rel-x,rel-y,buttons\n")
  File.write(File.join(root, 'event1'), '')
  File.write(File.join(root, 'event1.identity'),
             "bustype=0003\nvendor=05ac\nproduct=0252\nversion=0110\nname=bcm5974\n" \
             "kinds=pointer\ncapabilities=buttons,touchpad\n")
  settings = File.join(root, 'settings.json')
  server = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_PSF_FONT' => FONT,
                   'BLIT_INPUT_ROOT' => root, 'BLIT_SETTINGS_PATH' => settings,
                   'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
                 BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        begin
          break if request('GET', '/v1/version').first == 200
        rescue StandardError
          nil
        end
        sleep 0.2
      end
    end
    Timeout.timeout(15) { sleep 0.2 until device('Gate Mouse') && device('bcm5974') }
    mouse = device('Gate Mouse').fetch('id')
    pad = device('bcm5974').fetch('id')

    failures << 'a mouse is offered tap_to_click' if
      device('Gate Mouse').fetch('configuration').key?('tap_to_click')
    failures << "a touchpad is not offered tap_to_click off: #{device('bcm5974')['configuration']}" unless
      device('bcm5974').fetch('configuration')['tap_to_click'] == false

    code, = request('PATCH', "/v1/input/devices/#{pad}",
                    body: { 'configuration' => { 'tap_to_click' => true } })
    failures << "PATCH tap_to_click on a touchpad -> #{code}" unless code == 200
    failures << 'the touchpad does not read back tap_to_click on' unless
      device('bcm5974').fetch('configuration')['tap_to_click'] == true

    code, = request('PATCH', "/v1/input/devices/#{mouse}",
                    body: { 'configuration' => { 'tap_to_click' => true } })
    failures << "PATCH tap_to_click on a mouse -> #{code}, wanted 422" unless code == 422

    code, = request('PATCH', "/v1/input/devices/#{pad}",
                    body: { 'configuration' => { 'tap_to_click' => 'yes' } })
    failures << "PATCH tap_to_click \"yes\" -> #{code}, wanted 400" unless code == 400

    code, body = request('GET', '/v1/settings')
    kept = code == 200 && body.to_json.include?('"tap_to_click":true')
    failures << "the settings document does not keep tap_to_click: #{code}" unless kept

    code, = request('PATCH', "/v1/input/devices/#{pad}",
                    body: { 'configuration' => { 'tap_to_click' => false } })
    failures << "PATCH tap_to_click off -> #{code}" unless code == 200
    failures << 'the touchpad does not read back tap_to_click off' unless
      device('bcm5974').fetch('configuration')['tap_to_click'] == false
  ensure
    Process.kill('TERM', server) rescue nil
    Timeout.timeout(10) { Process.wait(server) } rescue nil
  end
end

if failures.empty?
  puts 'TAP_SETTING_PASS offered=touchpad_only default=off patch=200 mouse=422 not_boolean=400 kept=settings'
else
  failures.each { |f| warn "TAP_SETTING_FAIL #{f}" }
  exit 1
end
