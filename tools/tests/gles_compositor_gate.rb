#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0181 step 2. The GLES compositor inside native KMS:
#   1. gles_frame_gate.c: a frame drawn by GLES (surfaceless) reads back
#      byte for byte, at 1024x768, 1920x2160 and a padded pitch.
#   2. Surfaceless test mode: the output reports `gles` with its renderer and
#      counts frames drawn through it; a screenshot is served.
#   3. GBM requested where there is none (the DRM mock is a plain file): the
#      output falls back to `cpu`, says why, and keeps presenting.
#   4. server.kms_compositor is validated, saved, and applied at the next start.
# Every other gate runs through GLES with BLIT_GATE_BACKEND=gles.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

frame = File.join(Dir.tmpdir, "blit-gles-frame-#{Process.pid}")
at_exit { File.delete(frame) if File.exist?(frame) }
abort 'GLES_COMPOSITOR_FAIL gles_frame_gate does not compile' unless
  system('tcc', '-O2', '-std=c99', '-I' + File.join(GATE_ROOT, 'build/deps/prefix/include'),
         File.join(GATE_ROOT, 'tools/tests/gles_frame_gate.c'),
         File.join(GATE_ROOT, 'src/blit_gles.c'), '-o', frame)
frame_line = `#{frame} 2>&1`.strip
abort "GLES_COMPOSITOR_FAIL #{frame_line}" unless $?.success?

def server(port, env)
  pid = spawn(env.merge('BLIT_LOGIN_AUTOSTART' => '0',
                        'BLIT_PSF_FONT' => File.join(GATE_ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf')),
              ENV.fetch('BLIT_GATE_BINARY', File.join(GATE_ROOT, 'build/blit/blit-server')),
              port.to_s, out: File::NULL, err: File::NULL)
  base = URI("http://127.0.0.1:#{port}")
  Timeout.timeout(15) do
    loop do
      begin
        break if Net::HTTP.get_response(base + '/v1/version').code == '200'
      rescue StandardError
        nil
      end
      sleep 0.1
    end
  end
  yield base
ensure
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def compositor(base)
  JSON.parse(Net::HTTP.get(base + '/v1/diagnostics')).dig('render', 'outputs', 0, 'compositor')
end

def screenshot_ok(base)
  response = Net::HTTP.get_response(base + '/v1/diagnostics/screenshots/display-1')
  response.code == '200' && response.body.start_with?('BM')
end

port = gate_port(Integer(ENV.fetch('BLIT_GLES_COMPOSITOR_PORT', '19613')))
server(port, gate_display_env(backend: 'gles')) do |base|
  first = compositor(base)
  abort "GLES_COMPOSITOR_FAIL surfaceless output is not on GLES: #{first.inspect}" unless
    first['active'] == 'gles' && first['requested'] == 'gles' && !first['renderer'].empty?
  abort 'GLES_COMPOSITOR_FAIL no screenshot from the GLES output' unless screenshot_ok(base)
  Net::HTTP.post(base + '/v1/input/inject', JSON.generate(kind: 'pointer_motion', x: 300, y: 200),
                 'Content-Type' => 'application/json')
  sleep 0.5
  after = compositor(base)
  abort "GLES_COMPOSITOR_FAIL frames are not drawn through GLES: #{after.inspect}" unless
    after['gles_frames'] > first['gles_frames']
end

gbm = gate_display_env(backend: 'gles').merge('BLIT_GLES_PLATFORM' => nil)
server(port, gbm) do |base|
  state = compositor(base)
  abort "GLES_COMPOSITOR_FAIL a failed GBM start did not fall back: #{state.inspect}" unless
    state['active'] == 'cpu' && state['requested'] == 'gles' && state['reason'].include?('gbm')
  abort 'GLES_COMPOSITOR_FAIL no screenshot after the fallback' unless screenshot_ok(base)
end

# 4. The saved setting selects it at the next start, as the API is the only
#    way to configure a host: server.kms_compositor (cpu|gles).
Dir.mktmpdir('blit-gles-setting') do |root|
  env = gate_display_env(backend: 'native').merge(
    'BLIT_GLES_PLATFORM' => 'surfaceless', 'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  put = lambda do |base, document, revision|
    request = Net::HTTP::Put.new(base + '/v1/settings')
    request['Content-Type'] = 'application/json'
    request['If-Match'] = revision.to_s
    request.body = JSON.generate(document: document)
    Net::HTTP.start(base.host, base.port) { |http| http.request(request) }.code
  end
  server(port, env) do |base|
    abort 'GLES_COMPOSITOR_FAIL default is not cpu' unless compositor(base)['active'] == 'cpu'
    settings = JSON.parse(Net::HTTP.get(base + '/v1/settings'))
    server_settings = settings['document'].fetch('server', {})
    code = put.call(base, settings['document'].merge('server' => server_settings.merge('kms_compositor' => 'opengl')),
                    settings['revision'])
    abort "GLES_COMPOSITOR_FAIL an invalid compositor was accepted: #{code}" unless code == '400'
    code = put.call(base, settings['document'].merge('server' => server_settings.merge('kms_compositor' => 'gles')),
                    settings['revision'])
    abort "GLES_COMPOSITOR_FAIL saving gles failed: #{code}" unless code == '200'
  end
  server(port, env) do |base|
    effective = JSON.parse(Net::HTTP.get(base + '/v1/settings')).dig('effective', 'kms_compositor')
    state = compositor(base)
    abort "GLES_COMPOSITOR_FAIL the saved compositor did not apply: #{effective} #{state.inspect}" unless
      effective == 'gles' && state['active'] == 'gles'
  end
end

puts "GLES_COMPOSITOR_PASS #{frame_line.sub('GLES_FRAME_PASS ', 'frame=')} " \
     'surfaceless=gles fallback=cpu_with_reason setting=saved_and_applied'
