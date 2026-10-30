#!/usr/bin/env ruby
# frozen_string_literal: true

# Issue #147. A font change resizes every view, and each view's program is
# told its new size in cells and in pixels -- the pixels are what Hibr sizes
# its pictures from (cn_cellpx: ws_ypixel / ws_row). Laid out as the owner's
# machine is: one Hibr desktop as three views, a primary and a join on one
# monitor and a join on another. The sweep shrinks and grows the grid and
# crosses odd cell heights, where Hibr's own bugs (hibr #409, #410) showed;
# this gate holds Blit's half of that boundary.
require 'json'
require 'net/http'
require 'fileutils'
require 'uri'
require 'tmpdir'
require 'timeout'
require_relative 'gate_server'

PORT = Integer(ENV.fetch('BLIT_GATE_PORT', '18997'))
ROOT = File.expand_path('../..', __dir__)
SERVER = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FAKE_DESKTOP = File.join(ROOT, 'build/blit/desktop')
FAKE_DESKTOP_IMPL = File.join(ROOT, 'build/blit/desktop-font-resize-gate.sh')
SIZES = [22, 17, 16, 19, 26, 18, 22].freeze

FileUtils.mkdir_p(File.dirname(FAKE_DESKTOP))
FileUtils.rm_f(FAKE_DESKTOP)
File.write(FAKE_DESKTOP_IMPL, "#!/bin/sh\nexec /bin/sh \"$@\"\n")
FileUtils.chmod(0o755, FAKE_DESKTOP_IMPL)
FileUtils.ln_s(FAKE_DESKTOP_IMPL, FAKE_DESKTOP)

# The view's program: on every SIGWINCH, the size its terminal reports.
REPORTER = <<~'PY'
  import fcntl, os, signal, struct, sys, termios, time
  def report(*_):
      rows, cols, xp, yp = struct.unpack('HHHH', fcntl.ioctl(0, termios.TIOCGWINSZ, b'\0' * 8))
      sys.stdout.write('\033[2J\033[HWS %d %d %d %d' % (rows, cols, xp, yp))
      sys.stdout.flush()
  signal.signal(signal.SIGWINCH, report)
  report()
  while True:
      time.sleep(60)
PY

work = Dir.mktmpdir('blit-font-resize-views')
reporter = File.join(work, 'report.py')
File.write(reporter, REPORTER)

env = gate_display_env
File.write(env.fetch('BLIT_DRM_MOCK_STATE'), "2 0 0 0\n1 1920 1080\n2 1280 720\n")
env = env.merge('BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'), 'BLIT_SETTINGS' => 'ignore',
                'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_COMMAND_ALLOWLIST' => FAKE_DESKTOP)
pid = Process.spawn(env, SERVER, PORT.to_s, out: '/tmp/blit-font-resize-views-gate.log', err: %i[child out])
sessions = []
BASE = URI("http://127.0.0.1:#{PORT}")

def request(method, path, payload = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  if payload
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(payload)
  end
  response = Net::HTTP.start(req.uri.hostname, req.uri.port, open_timeout: 2, read_timeout: 10) do |http|
    http.request(req)
  end
  [response.code.to_i, response.body.to_s.empty? ? {} : JSON.parse(response.body)]
end

def reported(session)
  cells = request('GET', "/v1/surfaces/#{session.fetch('surface_id')}/cells").last.fetch('cells')
  text = cells.select { |c| c.fetch('y') == 0 }.sort_by { |c| c.fetch('x') }.map { |c| c.fetch('text') }.join
  text[/WS (\d+) (\d+) (\d+) (\d+)/] ? Regexp.last_match.captures.map(&:to_i) : nil
end

checked = 0
begin
  Timeout.timeout(15) do
    sleep 0.1 until (request('GET', '/v1/version').first rescue 0) == 200
  end
  topology = request('GET', '/v1/topology').last
  panes = request('PUT', '/v1/topology', { 'revision' => topology.fetch('revision'),
                                          'layout' => { 'columns' => 1, 'rows' => 2 } }).last.fetch('panes')
  by_display = panes.group_by { |p| p.fetch('display_id') }.values
                    .map { |list| list.sort_by { |p| p.fetch('pixel_region').fetch('y') } }
  raise "expected two monitors, got #{by_display.size}" unless by_display.size == 2

  places = [[by_display[0][1], %w[--session views]], [by_display[0][0], %w[--session views --join]],
            [by_display[1][0], %w[--session views --join]]]
  places.each do |pane, arguments|
    code, body = request('POST', '/v1/terminal-sessions',
                         { 'command' => FAKE_DESKTOP, 'arguments' => ['-c', "exec python3 '#{reporter}'", *arguments],
                           'surface_mode' => 'terminal', 'pane_id' => pane.fetch('id') })
    raise "session create failed: #{code} #{body}" unless code == 201

    sessions << body
  end

  SIZES.each do |size|
    code, = request('PUT', '/v1/fonts', { 'font_id' => 'hack', 'size_px' => size })
    raise "PUT /v1/fonts #{size}: #{code}" unless code == 200

    cell = request('GET', '/v1/fonts').last.fetch('active').fetch('cell_pixels')
    grids = request('GET', '/v1/topology').last.fetch('panes').to_h { |p| [p.fetch('id'), p.fetch('cell_grid')] }
    sessions.each do |session|
      grid = grids.fetch(session.fetch('pane_id'))
      want = [grid.fetch('rows'), grid.fetch('columns'),
              grid.fetch('columns') * cell.fetch('width'), grid.fetch('rows') * cell.fetch('height')]
      got = nil
      Timeout.timeout(5) do
        sleep 0.05 until (got = reported(session)) == want
      end
    rescue Timeout::Error
      raise "#{size} px: #{session.fetch('pane_id')} reports rows/cols/xpixel/ypixel #{got.inspect}, its pane is #{want.inspect}"
    else
      summary = request('GET', "/v1/surfaces/#{session.fetch('surface_id')}/summary").last
      unless [summary.fetch('height'), summary.fetch('width')] == want.first(2)
        raise "#{size} px: #{session.fetch('pane_id')} surface #{summary['width']}x#{summary['height']}, pane #{want[1]}x#{want[0]}"
      end
      checked += 1
    end
  end
  puts "FONT_RESIZE_VIEWS_PASS views=#{sessions.size} sizes=#{SIZES.join(',')} checks=#{checked} " \
       'winsize=cells+pixels surface=matches'
ensure
  sessions.each do |s|
    request('DELETE', "/v1/terminal-sessions/#{s.fetch('id')}")
  rescue StandardError
    nil
  end
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
  FileUtils.rm_f(FAKE_DESKTOP)
  FileUtils.rm_f(FAKE_DESKTOP_IMPL)
  FileUtils.remove_entry(work) if File.directory?(work)
end
