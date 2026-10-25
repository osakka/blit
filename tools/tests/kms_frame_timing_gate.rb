#!/usr/bin/env ruby
# frozen_string_literal: true

# 0.2.20 late compose. Under continuous terminal output, a frame composed
# right after the previous flip completes waits most of a refresh to be
# shown; composed `lead` before the next vblank, it waits about `lead`. The
# DRM mock's periodic mode (BLIT_DRM_MOCK_VBLANK_HZ) completes each flip at
# the first vblank boundary after its submission, as a display does, so the
# submit-to-flip time measures where in the refresh the frame was composed.
#
# Asserted, on one server switched live from the default (immediate) to
# server.kms_frame_timing=late (an invalid value is refused):
# - late compose waits at most 65% as long per flip (the lead grows on a
#   busy machine, so the bound is relative);
# - it presents at least 85% as many frames;
# - under 5% of its flips miss their vblank, and the lead stays in bounds.

require 'json'
require 'tmpdir'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))
HZ = 60

abort("kms-frame-timing gate: missing #{BINARY}") unless File.executable?(BINARY)
# Scheduling does not depend on the compositor; the surfaceless GLES backend
# reads every frame back on a software rasteriser, which measures that
# instead. The host's GPU timing is measured on the host.
unless GATE_BACKEND == 'native'
  puts "KMS_FRAME_TIMING_SKIP the CPU compositor measures scheduling (#{GATE_BACKEND})"
  exit 0
end

def get(port, path)
  uri = URI("http://127.0.0.1:#{port}#{path}")
  JSON.parse(Net::HTTP.start(uri.host, uri.port, read_timeout: 30) do |h|
    h.request(Net::HTTP::Get.new(uri))
  end.body)
end

def post(port, path, body)
  uri = URI("http://127.0.0.1:#{port}#{path}")
  req = Net::HTTP::Post.new(uri)
  req['Content-Type'] = 'application/json'
  req['Idempotency-Key'] = "frame-timing-#{rand(1 << 30)}"
  req.body = JSON.dump(body)
  JSON.parse(Net::HTTP.start(uri.host, uri.port, read_timeout: 30) { |h| h.request(req) }.body)
end

def put_settings(port, document, revision)
  uri = URI("http://127.0.0.1:#{port}/v1/settings")
  req = Net::HTTP::Put.new(uri)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s
  req.body = JSON.generate(document: document)
  Net::HTTP.start(uri.host, uri.port) { |h| h.request(req) }.code
end

def sample(port)
  output = -> { get(port, '/v1/diagnostics').fetch('render').fetch('outputs').first }
  sleep 2.0
  a = output.call
  t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  sleep 4.0
  b = output.call
  seconds = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t
  flips = b['flip']['count'] - a['flip']['count']
  {
    fps: (b['present_calls'] - a['present_calls']) / seconds,
    flip_ms: flips.positive? ? (b['flip']['total_us'] - a['flip']['total_us']) / 1000.0 / flips : 99,
    late: b['flip']['late'] - a['flip']['late'],
    slot_misses: b['flip']['slot_misses'] - a['flip']['slot_misses'],
    on_time: b['flip']['on_time'] - a['flip']['on_time'],
    lead_us: b['flip']['lead_us'], timing: b['flip']['timing']
  }
end

# One server, default timing (immediate), then switched live to late
# through the settings resource -- the way a host is A/B tested.
port = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19263'), 10))
late = immediate = nil
setting_failures = setting_failures
Dir.mktmpdir('blit-frame-timing') do |root|
  env = { **gate_display_env, 'BLIT_PSF_FONT' => FONT,
          'BLIT_DRM_MOCK_VBLANK_HZ' => HZ.to_s,
          'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json') }
  server = spawn(env, BINARY, port.to_s, out: '/dev/null', err: '/dev/null')
  begin
    Timeout.timeout(15) do
      loop do
        break if (get(port, '/v1/version') rescue nil)
        sleep 0.2
      end
    end
    # Output that changes continuously, faster than the refresh.
    post(port, '/v1/terminal-sessions',
         'command' => '/bin/sh',
         'arguments' => ['-c', 'i=0; while :; do i=$((i+1)); printf "\r%08d" $i; done'],
         'display_id' => 'display-1')
    immediate = sample(port)
    settings = get(port, '/v1/settings')
    server_settings = settings['document'].fetch('server', {})
    code = put_settings(port, settings['document'].merge(
      'server' => server_settings.merge('kms_frame_timing' => 'soon')), settings['revision'])
    setting_failures << "an invalid kms_frame_timing was accepted: #{code}" unless code == '400'
    code = put_settings(port, settings['document'].merge(
      'server' => server_settings.merge('kms_frame_timing' => 'late')), settings['revision'])
    setting_failures << "saving kms_frame_timing failed: #{code}" unless code == '200'
    effective = get(port, '/v1/settings').dig('effective', 'kms_frame_timing')
    setting_failures << "effective kms_frame_timing is #{effective.inspect}" unless effective == 'late'
    late = sample(port)
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end
failures = []
failures << "timing modes not reported: #{immediate[:timing]}/#{late[:timing]}" unless
  immediate[:timing] == 'immediate' && late[:timing] == 'late'
failures << format('late compose waits %.1f ms per flip, immediate %.1f: expected at most 65%%',
                   late[:flip_ms], immediate[:flip_ms]) unless
  late[:flip_ms] <= immediate[:flip_ms] * 0.65
# A loop stall through a late slot costs a frame immediate keeps; on a
# loaded build machine that is a few percent.
failures << format('late compose presents %.1f/s against %.1f/s', late[:fps], immediate[:fps]) unless
  late[:fps] >= immediate[:fps] * 0.85
total = late[:late] + late[:on_time]
failures << "#{late[:late]} of #{total} late-compose flips missed their vblank" unless
  total.positive? && late[:late] <= total * 0.05
failures << "lead #{late[:lead_us]} us out of bounds" unless
  late[:lead_us].between?(2000, 1_000_000 / HZ / 2)
unless failures.empty?
  failures.each { |f| warn "kms-frame-timing gate: FAIL #{f}" }
  warn "kms-frame-timing gate: immediate=#{immediate.inspect} late=#{late.inspect}"
  exit 1
end
puts format('KMS_FRAME_TIMING_PASS flip_wait_ms immediate=%.1f late=%.1f fps=%.0f/%.0f ' \
            'late_flips=%d/%d slot_misses=%d lead_us=%d', immediate[:flip_ms], late[:flip_ms],
            immediate[:fps], late[:fps], late[:late], total, late[:slot_misses], late[:lead_us])
