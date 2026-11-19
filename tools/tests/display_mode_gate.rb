#!/usr/bin/env ruby
# Gitea #60: a display's mode, chosen through PATCH /v1/displays/{id}
# configuration.mode, on the actual server with only libdrm mocked (two
# connectors, more modes, one the "monitor" refuses). A mode is tried: it
# reverts unless kept; kept, it is saved and a restart comes up at it in one
# modeset; the display keeps its place, the other display is untouched, and
# a session in its pane is resized with it.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'build_flags'
require_relative 'gate_env'
require_relative 'gate_port'
ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(ENV.fetch('BLIT_DISPLAY_MODE_PORT', '19531'))
BASE = URI("http://127.0.0.1:#{PORT}")
def check(ok, why) = (abort("DISPLAY_MODE_FAIL #{why}") unless ok)
def call(method, path, body = nil)
  q = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'PATCH' => Net::HTTP::Patch }.fetch(method).new(BASE + path)
  q['Authorization'] = 'Bearer mode-test'
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 10) { |h| h.request(q) }
  [r.code.to_i, r.body.to_s.empty? ? nil : JSON.parse(r.body)]
end
def get(path) = call('GET', path).last
def patch_mode(id, mode) = call('PATCH', "/v1/displays/#{id}", { configuration: { mode: mode } })
def await(why, limit = 13)
  Timeout.timeout(limit, RuntimeError, "timeout: #{why}") do
    loop do
      begin
        v = yield
        return v if v
      rescue Errno::ECONNREFUSED, Errno::ECONNRESET, EOFError
        nil
      end
      sleep 0.05
    end
  end
rescue RuntimeError => e
  abort "DISPLAY_MODE_FAIL #{e.message}"
end

Dir.mktmpdir('blit-display-mode') do |root|
  mock = File.join(root, 'drm.so')
  state = File.join(root, 'state')
  trace = File.join(root, 'trace')
  settings = File.join(root, 'settings.json')
  check(system('tcc', '-shared', '-fPIC', *BuildFlags.cflags('drm'),
               File.join(ROOT, 'tools/tests/fixtures/drm_hotplug_mock.c'), '-o', mock), 'mock compile failed')
  File.write(state, "2 0 0 0\n93 320 192\n94 320 192\n")
  device = File.join(root, 'drm')
  File.write(device, '')
  Dir.mkdir(File.join(root, 'input'))
  env = { 'BLIT_DISPLAY_BACKEND' => 'kms', 'BLIT_KMS_COMPOSITOR' => ENV.fetch('BLIT_DISPLAY_MODE_COMPOSITOR', ENV['BLIT_GATE_BACKEND'] == 'gles' ? 'gles' : 'cpu'),
          'BLIT_GLES_PLATFORM' => 'surfaceless', 'BLIT_DRM_DEVICE' => device,
          'BLIT_DRM_MOCK_STATE' => state, 'BLIT_DRM_MOCK_TRACE' => trace, 'LD_PRELOAD' => mock,
          'BLIT_DRM_MOCK_MODES' => '256x160@60,256x160@75,240x144@60',
          'BLIT_DRM_MOCK_FAIL_MODE' => '240x144', 'BLIT_DISPLAY_MODE_TRIAL_MS' => '1500',
          'BLIT_AUTH_TOKEN' => 'mode-test', 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_PANE_LAYOUT' => '1x1',
          'BLIT_SETTINGS_PATH' => settings, 'BLIT_INPUT_ROOT' => File.join(root, 'input'),
          'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf') }
  log = File.join(root, 'server.log')
  pid = nil
  start = lambda do
    pid = gate_spawn(env, BINARY, PORT.to_s, out: log, err: [:child, :out])
    await('startup') { get('/v1/version') }
    await('settings durable') { get('/v1/settings').dig('storage', 'state') == 'clean' }
  end
  stop = lambda do
    Process.kill('TERM', pid) rescue nil
    Timeout.timeout(10) { Process.wait(pid) } rescue nil
    pid = nil
  end
  display = ->(id) { get("/v1/displays/#{id}") }
  size = ->(id) { display.(id)['bounds'].values_at('width', 'height') }
  order = -> { get('/v1/displays')['displays'].map { |d| d['id'] } }
  traced = -> { File.exist?(trace) ? File.readlines(trace, chomp: true) : [] }
  results = []
  begin
    start.call
    check(order.call == %w[display-93 display-94], "inventory #{order.call}")
    compositor = env['BLIT_KMS_COMPOSITOR']
    active = -> { get('/v1/diagnostics').dig('render', 'outputs').map { |o| o.dig('compositor', 'active') } }
    check(active.call.uniq == [compositor], "the compositor is not #{compositor}: #{active.call}")
    d94 = display.('display-94')
    check(d94['capabilities'].include?('mode') && d94['configuration']['mode'] == 'auto', "display-94 #{d94.inspect}")
    check(d94['modes'].map { |m| [m['width'], m['height'], m['refresh_hz'].round] }.include?([256, 160, 75]),
          "modes not listed: #{d94['modes']}")
    session = call('POST', '/v1/terminal-sessions', { command: '/bin/cat', pane_id: 'pane-display-94-1' }).last
    columns = -> { get('/v1/topology')['panes'].find { |p| p['display_id'] == 'display-94' }.dig('cell_grid', 'columns') }
    wide = columns.call

    # Refused: not a mode, not listed, nothing to keep.
    code, body = patch_mode('display-94', 'big')
    check(code == 400 && body['code'] == 'invalid_display_mode', "a bad mode: #{code} #{body}")
    code, body = patch_mode('display-94', '1000x1000')
    check(code == 422 && body['code'] == 'display_mode_unlisted', "an unlisted mode: #{code} #{body}")
    code, body = patch_mode('display-94', 'keep')
    check(code == 409 && body['code'] == 'display_mode_not_on_trial', "keep with nothing on trial: #{code} #{body}")
    results << 'refused=bad,unlisted,keep-nothing'

    # Tried on the second display, not kept: reverted.
    File.write(trace, '')
    code, body = patch_mode('display-94', '256x160@75')
    check(code == 200 && body['bounds'].values_at('width', 'height') == [256, 160] &&
          body['active_mode']['refresh_hz'].round == 75 && body['configuration']['mode'] == '256x160@75' &&
          body.dig('configuration', 'mode_trial', 'revert_to') == 'auto', "the trial: #{code} #{body}")
    check(traced.call.include?('mode 256 160'), "no modeset at 256x160: #{traced.call}")
    # Presenting at it: a screenshot of the display is the new size.
    shot = lambda do |id|
      q = Net::HTTP::Get.new(BASE + "/v1/diagnostics/screenshots/#{id}")
      q['Authorization'] = 'Bearer mode-test'
      r = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 10) { |h| h.request(q) }
      next nil unless r.code == '200' && r.body.start_with?('BM')
      w, h = r.body[18, 8].unpack('l<l<')
      [w, h.abs]
    end
    await('a screenshot at 256x160') { shot.('display-94') == [256, 160] }
    check(order.call == %w[display-93 display-94], "the order changed: #{order.call}")
    check(size.('display-93') == [320, 192], 'the other display changed')
    await('the session resized with its display') { columns.call < wide }
    check(get("/v1/terminal-sessions/#{session['id']}")['process_id'] == session['process_id'], 'the session restarted')
    await('the trial reverted', 6) { size.('display-94') == [320, 192] }
    check(display.('display-94')['configuration']['mode'] == 'auto' && !display.('display-94')['configuration']['mode_trial'],
          "after the revert: #{display.('display-94')['configuration']}")
    await('the session back at its size') { columns.call == wide }
    check(get('/v1/diagnostics/log').to_s.include?('display_mode_reverted') || File.read(log).include?('not kept; back to auto'),
          'the revert was not said')
    check(active.call.uniq == [compositor], "after a mode change the compositor is not #{compositor}: #{active.call}")
    results << 'trial=applied,order-kept,other-untouched,session-resized,reverted'

    # Zoomed (ADR 0204): a mode keeps the zoom, the display the mode over it.
    zoom = call('PATCH', '/v1/displays/display-94', { configuration: { zoom: 200 } })
    check(zoom.first == 200 && size.('display-94') == [160, 96], "zoom 200: #{zoom.inspect}")
    patch_mode('display-94', '256x160@60')
    check(size.('display-94') == [128, 80] && display.('display-94')['configuration']['zoom'] == 200,
          "a mode under zoom 200: #{display.('display-94')}")
    await('the zoomed trial reverted', 6) { size.('display-94') == [160, 96] }
    call('PATCH', '/v1/displays/display-94', { configuration: { zoom: 100 } })
    results << 'zoom=kept-over-mode'

    # Kept: stays past the trial, saved.
    patch_mode('display-93', '256x160')
    code, body = patch_mode('display-93', 'keep')
    check(code == 200 && body['configuration']['mode'] == '256x160' && !body['configuration']['mode_trial'],
          "keep: #{code} #{body}")
    sleep 2
    check(size.('display-93') == [256, 160], 'a kept mode reverted')
    await('the kept mode saved') { (JSON.parse(File.read(settings)).dig('displays', 'DP-1', 'mode') rescue nil) == '256x160' }
    results << 'keep=stays,saved'

    # A mode the monitor refuses: put back where it was, said.
    code, body = patch_mode('display-93', '240x144')
    check(code == 422 && body['code'] == 'display_mode_failed' && body['detail'].include?('back at 256x160'),
          "a refused mode: #{code} #{body}")
    check(traced.call.include?('mode-refused 240 144'), 'the refused modeset was not tried')
    check(size.('display-93') == [256, 160] && order.call == %w[display-93 display-94],
          "after a refused mode: #{size.('display-93')} #{order.call}")
    results << 'refused-by-monitor=back,said'

    # Restart: the kept mode in one modeset, no preferred one first.
    stop.call
    File.write(trace, '')
    start.call
    check(size.('display-93') == [256, 160] && display.('display-93')['configuration']['mode'] == '256x160',
          "after a restart: #{display.('display-93')}")
    modes = traced.call.grep(/^mode /)
    check(modes.sort == ['mode 256 160', 'mode 320 192'], "the restart's modesets: #{modes}")
    results << 'restart=kept-mode,one-modeset'

    # Back to auto, kept: the setting goes.
    patch_mode('display-93', 'auto')
    patch_mode('display-93', 'keep')
    check(size.('display-93') == [320, 192], 'auto did not restore the preferred mode')
    await('the mode setting removed') { !File.read(settings).include?('"mode"') }
    results << 'auto=preferred,unsaved'
    puts "DISPLAY_MODE_PASS compositor=#{compositor} #{results.join(' ')} source=mocked_drm_not_physical"
  rescue SystemExit
    warn File.read(log).lines.last(30).join
    raise
  ensure
    stop.call if pid
  end
end
