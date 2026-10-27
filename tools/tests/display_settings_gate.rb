#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #69, epic #64 stage 5. A display's `configuration` was `{}`, and ADR
# 0064 recorded that as a decision rather than an omission: a mode change
# resizes every surface, and a Hibr desktop's cells come from its owner, so it
# needs issue #60's resize protocol first. What is left is the settings that
# resize nothing, and they divide by who is permitted to perform them.
#
# Measured on the live host: /sys/class/drm/card0-DP-1/dpms is mode 444 (a
# root write is EACCES) and /sys/class/backlight is empty, so brightness has no
# hardware to act on there and power cannot go through sysfs. Rotation was an
# SDL render transform; native KMS has none (ADR 0184), so it is refused.
#
# The assertions:
#
#   * `capabilities` is derived from the backend and the hardware. Fabricate a
#     backlight and `brightness` appears; take it away and it does not.
#   * rotation is refused as unsupported (422), and an invalid angle is still
#     a bad request (400); a mode change is not a writable property (400).
#   * brightness reaches the backlight's raw value and reads back from
#     actual_brightness.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19392'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")
# The dummy video driver's single display, and the cell box of the gate font.

abort("display gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil, headers = {})
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put,
            'POST' => Net::HTTP::Post, 'PATCH' => Net::HTTP::Patch,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  headers.each { |k, v| req[k] = v }
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.dump(body)
  end
  req['Idempotency-Key'] = "display-#{rand(1 << 30)}" if %w[POST PATCH].include?(method)
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 30) do |http|
    http.request(req)
  end
  [response.code.to_i, (response.body.to_s.empty? ? nil : response.body)]
end

def json(method, path, body = nil)
  code, text = request(method, path, body)
  [code, (text.nil? ? nil : (JSON.parse(text) rescue text))]
end

$failures = []
def check(condition, message)
  return if condition

  $failures << message
  warn "display gate: FAIL #{message}"
end

def verdict(name)
  before = $failures.length
  yield
  puts(before == $failures.length ? "PASS #{name}" : "FAIL #{name}")
end

$server = nil
def start(env)
  $server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT }.merge(env),
                  BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
  Timeout.timeout(20) do
    loop do
      begin
        break if request('GET', '/v1/version').first == 200
      rescue StandardError
        nil
      end
      sleep 0.2
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

def display
  json('GET', '/v1/displays').last.to_h.fetch('displays', []).first.to_h
end

def patch(configuration, headers = {})
  json('PATCH', '/v1/displays/display-1', { 'configuration' => configuration })
end

Dir.mktmpdir('blit-display') do |root|
  settings = File.join(root, 'settings.json')
  backlight = File.join(root, 'backlight')
  Dir.mkdir(backlight)
  Dir.mkdir(File.join(backlight, 'intel_backlight'))
  # 937 steps, so the percentage conversion is in its "more raw values than
  # percentages" class -- the class where percent -> raw -> percent is exact
  # and the reverse cannot be, which blit_audio_control.h already pins.
  File.write(File.join(backlight, 'intel_backlight', 'max_brightness'), "937\n")
  File.write(File.join(backlight, 'intel_backlight', 'brightness'), "468\n")
  File.write(File.join(backlight, 'intel_backlight', 'actual_brightness'),
             "468\n")

  # ---- capabilities are derived from the hardware, not declared -----------
  start('BLIT_SETTINGS_PATH' => settings)
  verdict 'a-display-without-a-backlight-has-no-brightness' do
    item = display
    check(item.fetch('capabilities', []).include?('mode-report'),
          "mode-report is missing: #{item['capabilities'].inspect}")
    check(!item.fetch('capabilities', []).include?('rotation'),
          "native KMS has no rotation transform: #{item['capabilities'].inspect}")
    check(!item.fetch('capabilities', []).include?('brightness'),
          'brightness is claimed with no backlight on the host')
    check(item.fetch('configuration', {}) == {},
          "configuration should be empty: #{item['configuration'].inspect}")
    code, problem = patch('rotation' => 90)
    check(code == 422 && problem.to_h['code'] == 'display_rotation_unsupported',
          "rotation is refused -> #{code} #{problem.inspect}")
    code, problem = patch('rotation' => 45)
    check(code == 400 && problem.to_h['code'] == 'invalid_display_rotation',
          "an invalid rotation is a bad request -> #{code} #{problem.inspect}")
    code, problem = json('PATCH', '/v1/displays/display-1',
                         { 'configuration' => { 'brightness_percent' => 50 } })
    check(code == 422, "brightness with no backlight -> #{code}")
    check(problem.to_h['code'] == 'display_brightness_unsupported',
          "brightness refusal code: #{problem.inspect}")
    # A property that is not writable at all is a different refusal from one
    # the hardware cannot do, and the difference is the point: a controller
    # that sees 422 knows to look at `capabilities`, and 400 means it sent
    # something Blit will never accept.
    code, problem = json('PATCH', '/v1/displays/display-1',
                         { 'configuration' => { 'mode' => '1920x1080' } })
    check(code == 400, "a mode change -> #{code}")
    check(problem.to_h['code'] == 'invalid_display_property',
          "mode refusal code: #{problem.inspect}")
  end

  # ---- a fabricated backlight --------------------------------------------
  verdict 'a-backlight-makes-brightness-a-capability' do
    stop
    start('BLIT_SETTINGS_PATH' => settings, 'BLIT_BACKLIGHT_ROOT' => backlight)
    item = display
    check(item.fetch('capabilities', []).include?('brightness'),
          "brightness should appear with a backlight: #{item['capabilities'].inspect}")
    check(item.dig('state', 'backlight') == 'intel_backlight',
          "the backlight should be named: #{item['state'].inspect}")
    # 468 of 937 is 49.946%, which rounds to 50.
    check(item.dig('configuration', 'brightness_percent') == 50,
          "468/937 should read as 50%: #{item['configuration'].inspect}")
    code, after = patch('brightness_percent' => 75)
    check(code == 200, "brightness 75 -> #{code} #{after.inspect}")
    raw = File.read(File.join(backlight, 'intel_backlight', 'brightness')).strip
    check(raw == '703', "75% of 937 should be 703 on disk, not #{raw}")
    # The read comes from actual_brightness, which a real driver updates and a
    # fabricated one does not -- so the item still reports the old percentage
    # until the sidecar is updated. That is the honest behaviour, and asserting
    # it keeps the two attributes from being quietly conflated.
    check(display.dig('configuration', 'brightness_percent') == 50,
          'the item should report actual_brightness, not the request')
    File.write(File.join(backlight, 'intel_backlight', 'actual_brightness'),
               "#{raw}\n")
    check(display.dig('configuration', 'brightness_percent') == 75,
          "after the panel caught up: #{display['configuration'].inspect}")
    code, problem = patch('brightness_percent' => 101)
    check(code == 400, "brightness 101 -> #{code} #{problem.inspect}")
  end
  stop
end

if $failures.empty?
  puts 'DISPLAY_SETTINGS_PASS capabilities=derived rotation=refused brightness=seam'
else
  warn "display gate: #{$failures.length} failure(s)"
  exit 1
end
