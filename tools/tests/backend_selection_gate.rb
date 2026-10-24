#!/usr/bin/env ruby
# frozen_string_literal: true
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_BACKEND_GATE_PORT', '19531')))
BASE = URI("http://127.0.0.1:#{PORT}")
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))

def req(method, path, body = nil, revision = nil)
  q = {'GET'=>Net::HTTP::Get, 'PUT'=>Net::HTTP::Put,
       'POST'=>Net::HTTP::Post, 'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE + path)
  q['Content-Type'] = 'application/json'
  q['If-Match'] = revision.to_s if revision
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(BASE.host, BASE.port, open_timeout: 2, read_timeout: 5) { |h| h.request(q) }
  [r.code.to_i, r.body.to_s.empty? ? nil : JSON.parse(r.body)]
end

def get(path)
  code, value = req('GET', path)
  raise "GET #{path}: #{code}" unless code == 200
  value
end

Dir.mktmpdir('blit-backend-selection') do |root|
  path = File.join(root, 'settings.json')
  pid = nil
  start = lambda do |backend, mode = nil|
    # SDL path only (tests switching between SDL and native); retired at the GLES cutover.
    env = {**gate_display_env(backend: 'sdl'), 'BLIT_RENDERER'=>'software',
           'BLIT_DISPLAY_BACKEND'=>backend, 'BLIT_DRM_DEVICE'=>File.join(root, 'absent-drm'),
           'BLIT_LOGIN_AUTOSTART'=>'0', 'BLIT_AUTH_TOKEN'=>'', 'BLIT_BIND_ADDRESS'=>'127.0.0.1',
           'BLIT_SETTINGS_PATH'=>path, 'BLIT_SETTINGS'=>mode, 'BLIT_INPUT_ROOT'=>root,
           'BLIT_PSF_FONT'=>File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'),
           'LD_LIBRARY_PATH'=>File.join(ROOT, 'build/sdl3-kms')}
    pid = spawn(env, BINARY, PORT.to_s, out: File.join(root, 'log'), err: [:child, :out])
    Timeout.timeout(10) do
      loop do
        begin
          break if get('/v1/version')
        rescue SystemCallError
          sleep 0.02
        end
      end
    end
  end
  stop = lambda { Process.kill('TERM', pid); Process.wait(pid); pid = nil }
  save = lambda do |backend|
    state = get('/v1/settings')
    state['document']['server']['display_backend'] = backend
    req('PUT', '/v1/settings', {document: state['document']}, state['revision'])
  end
  begin
    start.call('sdl')
    settings = get('/v1/settings')
    raise 'missing backend default' unless settings.dig('effective', 'display_backend_requested') == 'sdl'
    raise 'unexpected restart request' unless settings.dig('effective', 'presentation_restart_required') == false
    settings['document']['server']['renderer'] = 'opengles2'
    code, changed = req('PUT', '/v1/settings', {document: settings['document']}, settings['revision'])
    raise 'SDL renderer restart requirement missing' unless code == 200 && changed.dig('effective', 'presentation_restart_required')
    changed['document']['server']['renderer'] = 'software'
    code, changed = req('PUT', '/v1/settings', {document: changed['document']}, changed['revision'])
    raise 'restored SDL renderer still pending' unless code == 200 && changed.dig('effective', 'presentation_restart_required') == false
    code, session = req('POST', '/v1/terminal-sessions', {command: '/bin/cat', display_id: 'display-1'})
    raise 'session fixture failed' unless code == 201
    code, saved = save.call('kms')
    raise 'backend save failed' unless code == 200 && saved.dig('storage', 'state') == 'clean'
    raise 'save switched live backend' unless get('/v1/diagnostics').dig('health', 'backend') == 'sdl3'
    raise 'save replaced the live terminal' unless get("/v1/terminal-sessions/#{session['id']}")['process_id'] == session['process_id']
    raise 'backend restart requirement missing' unless saved.dig('effective', 'presentation_restart_required') == true
    raise 'backend not durable' unless JSON.parse(File.read(path)).dig('server', 'display_backend') == 'kms'
    bytes = File.binread(path)
    revision = saved['revision']
    [nil, true, 1, {}, [], 'ksm', '', 'opengles2'].each do |invalid|
      code, problem = save.call(invalid)
      raise "invalid backend accepted: #{invalid.inspect}" unless code == 400 && problem['code'] == 'invalid_settings_server'
      raise 'invalid input mutated settings' unless File.binread(path) == bytes && get('/v1/settings')['revision'] == revision
    end
    Dir.mkdir(path + '.new')
    code, failed = save.call('sdl')
    raise 'failed durable save reported success' unless code == 503 && failed['code'] == 'settings_write_failed'
    raise 'failed save replaced last durable selection' unless File.binread(path) == bytes
    req('DELETE', "/v1/terminal-sessions/#{session['id']}")
    stop.call
    Dir.rmdir(path + '.new')
    start.call('sdl')
    raise 'stored KMS did not override SDL boot default' unless get('/v1/diagnostics').dig('render', 'direct_kms') == true
    raise 'absent KMS device falsely ready' unless get('/v1/diagnostics').dig('health', 'ready') == false
    code, = save.call('sdl')
    raise 'recovery save failed' unless code == 200
    stop.call
    start.call('kms')
    raise 'stored SDL did not override KMS boot default' unless get('/v1/diagnostics').dig('health', 'backend') == 'sdl3'
    raise 'restored selection still pending' unless get('/v1/settings').dig('effective', 'presentation_restart_required') == false
    code, reset = req('DELETE', '/v1/settings', nil, get('/v1/settings')['revision'])
    raise 'reset did not restore KMS boot default' unless code == 200 && reset.dig('effective', 'display_backend_requested') == 'kms'
    raise 'reset switched live output' unless reset.dig('effective', 'display_backend_active') == 'sdl3' && reset.dig('effective', 'presentation_restart_required')
    save.call('kms')
    bytes = File.binread(path)
    stop.call
    start.call('sdl', 'ignore')
    raise 'recovery mode applied persisted backend' unless get('/v1/settings').dig('effective', 'display_backend_requested') == 'sdl'
    raise 'recovery mode changed saved selection' unless File.binread(path) == bytes
    puts 'BACKEND_SELECTION_PASS deferred=yes invalid=8 durable=yes failed_save=last_good precedence=both reset=boot_default recovery=ignore unavailable_kms=api_alive'
  ensure
    stop.call if pid
  end
end
