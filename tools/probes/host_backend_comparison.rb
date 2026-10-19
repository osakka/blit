#!/usr/bin/env ruby
# frozen_string_literal: true

# Bounded, disruptive physical comparison through the existing API. Save the
# baseline, disable startup login for isolated fixtures, verify each restarted
# presentation identity, then restore only the settings this probe changed.
require 'json'
require 'net/http'
require 'open3'
require 'tmpdir'
require 'fileutils'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
ROOT = File.expand_path('../..', __dir__)
OUT = ENV.fetch('BLIT_EVIDENCE_DIR') { Dir.mktmpdir('blit-backend-comparison-') }
RESTART_TIMEOUT = Float(ENV.fetch('BLIT_COMPARISON_RESTART_TIMEOUT', '40'))
raise 'restart timeout must be 1..120 seconds' unless RESTART_TIMEOUT.between?(1, 120)
FileUtils.mkdir_p(OUT)
class ComparisonHttpError < StandardError; end

def request(method, path, payload = nil, revision = nil)
  q = {'GET'=>Net::HTTP::Get, 'PUT'=>Net::HTTP::Put, 'POST'=>Net::HTTP::Post}.fetch(method).new(BASE + path)
  q['Authorization'] = "Bearer #{TOKEN}"
  q['If-Match'] = revision.to_s if revision
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(payload) if payload
  r = Net::HTTP.start(BASE.host, BASE.port, open_timeout: 3, read_timeout: 10) { |h| h.request(q) }
  raise ComparisonHttpError, "#{method} #{path}: HTTP #{r.code}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end

def save(document, revision)
  result = request('PUT', '/v1/settings', {document: document}, revision)
  raise 'settings save is not durable' unless result.dig('storage', 'state') == 'clean'
  result
end

def start_time(observation)
  observation.fetch('captured_monotonic_us') - observation.dig('runtime', 'performance', 'uptime_ms') * 1000
end

def park_pointer(observation)
  bounds = observation.fetch('displays').fetch('displays').first.fetch('bounds')
  x = bounds.fetch('x') + bounds.fetch('width') - 64
  y = bounds.fetch('y') + bounds.fetch('height') - 64
  request('POST', '/v1/input/inject', {kind: 'pointer_motion', x: x, y: y})
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 3
  loop do
    current = request('GET', '/v1/diagnostics/observation')
    pointer = current.dig('diagnostics', 'render', 'pointer')
    return current if pointer['valid'] && pointer['x'] == x && pointer['y'] == y && !pointer['dirty']
    raise 'pointer did not settle outside the pixel fixture' if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
    sleep 0.02
  end
end

def restart_and_await(backend, renderer = nil)
  previous = request('GET', '/v1/diagnostics/observation')
  begin
    request('POST', '/v1/control/restart')
  rescue EOFError, IOError, Errno::ECONNRESET
    # A supervised restart may close the acknowledgement connection.
  end
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + RESTART_TIMEOUT
  loop do
    begin
      current = request('GET', '/v1/diagnostics/observation')
      actual = current.fetch('diagnostics')
      if start_time(current) > start_time(previous) + 10_000 &&
         actual.dig('health', 'ready') && actual.dig('health', 'backend') == backend &&
         (renderer.nil? || (actual.dig('render', 'video_driver') == 'kmsdrm' && actual.dig('render', 'renderer') == renderer))
        return current
      end
    rescue SystemCallError, IOError, Net::ReadTimeout, Net::OpenTimeout, ComparisonHttpError
      nil
    end
    raise "restart did not reach #{backend}/#{renderer || 'native'}" if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
    sleep 0.2
  end
end

original = request('GET', '/v1/settings')
baseline = request('GET', '/v1/diagnostics/observation')
raise 'requires backend-selection API' unless original.fetch('effective').key?('display_backend_requested')
raise 'baseline is not ready' unless baseline.dig('diagnostics', 'health', 'ready')
raise 'requires a direct-KMS baseline for this comparison' unless baseline.dig('diagnostics', 'health', 'backend') == 'drm-kms'
File.write(File.join(OUT, 'baseline-settings.json'), JSON.pretty_generate(original))
File.write(File.join(OUT, 'baseline-observation.json'), JSON.pretty_generate(baseline))
changed = false
begin
  [['native', 'kms', nil], ['sdl-software', 'sdl', 'software'], ['sdl-gles2', 'sdl', 'opengles2']].each do |name, backend, renderer|
    state = request('GET', '/v1/settings')
    doc = state.fetch('document')
    (doc['server'] ||= {})['display_backend'] = backend
    doc['server']['renderer'] = renderer if renderer
    (doc['login'] ||= {})['enabled'] = false
    changed = true
    save(doc, state.fetch('revision'))
    observation = restart_and_await(backend == 'kms' ? 'drm-kms' : 'sdl3', renderer)
    raise 'login was not disabled for isolated fixtures' unless observation.dig('login', 'enabled') == false
    observation = park_pointer(observation)
    directory = File.join(OUT, name)
    FileUtils.mkdir_p(directory)
    File.write(File.join(directory, 'before.json'), JSON.pretty_generate(observation))
    %w[host_motion_agreement.rb host_kitty_composition.rb host_grid_font_edges.rb host_sixel.rb].each do |probe|
      output, error, status = Open3.capture3({'BLIT_AUTH_TOKEN'=>TOKEN, 'BLIT_LIVE'=>BASE.to_s, 'BLIT_EVIDENCE_DIR'=>directory},
        'ruby', File.join(ROOT, 'tools/probes', probe), BASE.authority)
      File.write(File.join(directory, probe + '.log'), output + error)
      raise "#{name}: #{probe} failed; see #{directory}" unless status.success?
      puts "#{name}: #{output.lines.last.to_s.strip}"
      $stdout.flush
    end
    if ENV['BLIT_COMPARISON_POINTER_CADENCE'] == '1'
      output, error, status = Open3.capture3(
        {'BLIT_AUTH_TOKEN'=>TOKEN, 'BLIT_LIVE'=>BASE.to_s, 'BLIT_EVIDENCE_DIR'=>directory,
         'BLIT_POINTER_ASYNC'=>'1','BLIT_POINTER_PATTERN'=>'sweep','BLIT_POINTER_STEP_PX'=>'12',
         'BLIT_POINTER_SECONDS'=>ENV.fetch('BLIT_POINTER_SECONDS','10'),
         'BLIT_POINTER_HZ'=>observation.dig('diagnostics','render','outputs').first.fetch('refresh_hz').to_s},
        'ruby',File.join(ROOT,'tools/probes/host_pointer_cadence.rb'))
      File.write(File.join(directory,'host_pointer_cadence.rb.log'),output + error)
      raise "#{name}: pointer measurement failed; see #{directory}" unless status.success?
      result = JSON.parse(output)
      puts "#{name}: POINTER_MEASURED offered_hz=#{result['offered_motion_hz']} outputs=#{JSON.generate(result['output_submissions'])}"
      $stdout.flush
    end
    File.write(File.join(directory, 'after.json'), JSON.pretty_generate(request('GET', '/v1/diagnostics/observation')))
  end
ensure
  if changed
    state = request('GET', '/v1/settings')
    doc = state.fetch('document')
    %w[display_backend renderer].each do |key|
      old = original.dig('document', 'server') || {}
      old.key?(key) ? doc['server'][key] = old[key] : doc['server'].delete(key)
    end
    old = original.dig('document', 'login') || {}
    old.key?('enabled') ? doc['login']['enabled'] = old['enabled'] : doc['login'].delete('enabled')
    save(doc, state.fetch('revision'))
    actual = baseline.fetch('diagnostics')
    restored = restart_and_await(actual.dig('health', 'backend'), actual.dig('render', 'direct_kms') ? nil : actual.dig('render', 'renderer'))
    pointer = baseline.dig('diagnostics', 'render', 'pointer')
    if pointer && pointer['valid']
      request('POST', '/v1/input/inject', {kind: 'pointer_motion', x: pointer['x'], y: pointer['y']})
      restored = request('GET', '/v1/diagnostics/observation')
    end
    File.write(File.join(OUT, 'restored-observation.json'), JSON.pretty_generate(restored))
    puts "BACKEND_BASELINE_RESTORED evidence=#{OUT}"
  end
end
puts "BACKEND_COMPARISON_PASS evidence=#{OUT}"
