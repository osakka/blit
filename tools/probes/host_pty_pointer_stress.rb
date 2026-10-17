#!/usr/bin/env ruby
# Bounded live PTY animation concurrent with API pointer traffic. The producer
# finishes before pixel comparison; a matching capture revision is mandatory.
require 'json'
require 'net/http'
require 'uri'
require 'fileutils'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN', 'hellomonkeyboy')
OUT = ENV.fetch('BLIT_EVIDENCE_DIR', 'diagnostics/rendering-20261017/pty-pointer')

def req(method, path, body = nil)
  uri = BASE + path
  q = {'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post,
       'DELETE'=>Net::HTTP::Delete, 'PATCH'=>Net::HTTP::Patch}.fetch(method).new(uri)
  q['Authorization'] = "Bearer #{TOKEN}"
  if body
    q['Content-Type'] = 'application/json'
    q.body = JSON.generate(body)
  end
  r = Net::HTTP.start(uri.host, uri.port, open_timeout: 3, read_timeout: 30) { |http| http.request(q) }
  raise "#{method} #{path}: #{r.code}: #{r.body[0,200]}" unless r.code.start_with?('2')
  r
end
def data(path) = JSON.parse(req('GET', path).body)
def pixel(bmp, x, y)
  w = bmp[18,4].unpack1('l<'); h = bmp[22,4].unpack1('l<')
  bpp = bmp[28,2].unpack1('S<') / 8
  raise 'BMP coordinate/depth invalid' unless [3,4].include?(bpp) && x >= 0 && y >= 0 && x < w && y < h
  stride = (w * bpp + 3) & ~3
  offset = 54 + (h - 1 - y) * stride + x * bpp
  [bmp.getbyte(offset + 2), bmp.getbyte(offset + 1), bmp.getbyte(offset)]
end

pane = data('/v1/topology').fetch('panes').find { |p| p['surface_id'].nil? }
raise 'requires a free pane with at least 40x20 cells' unless pane &&
  pane.dig('cell_grid','columns') >= 40 && pane.dig('cell_grid','rows') >= 20
grid = pane.fetch('cell_grid'); display = pane.fetch('display_id')
before = data('/v1/diagnostics/runtime')
pointer = before.fetch('snapshot').fetch('render').fetch('pointer')
focused = data('/v1/terminal-sessions').fetch('sessions').find { |s| s['focused'] }
inset = before.dig('snapshot','render','surface_inset_px')
session = nil; mover = nil; stop = false; motions = 0; samples = []
FileUtils.mkdir_p(OUT)
File.write(File.join(OUT, 'runtime-before.json'), JSON.pretty_generate(before))
begin
  # The child writes genuine terminal bytes through its PTY. Each iteration
  # erases the old shape then draws the new one, repeatedly reversing both
  # axes. No API cell update supplies the animation state.
  script = <<~'SH'
    printf '\033[?25l\033[48;2;16;32;48m\033[2J'
    sleep 1
    printf '\033]0;BLIT_STRESS_ACTIVE\007'
    i=0; oldx=2; oldy=3
    while [ "$i" -lt 600 ]; do
      x=$((i * 17 % 34 + 2)); y=$((i * 7 % 16 + 3))
      printf '\033[%d;%dH    \033[%d;%dH\033[38;2;255;80;32m▀▄▀▄' "$oldy" "$oldx" "$y" "$x"
      oldx=$x; oldy=$y; i=$((i + 1))
      sleep 0.004
    done
    printf '\033[%d;%dH    \033[1;1H\033[38;2;190;210;190mBLIT_STRESS_DONE\033]0;BLIT_STRESS_DONE\007' "$oldy" "$oldx"
    sleep 120
  SH
  # Each API argument is bounded to 256 bytes. Concatenate bounded fragments
  # in the child instead of asking the server to accept an oversized argument.
  fragments = script.each_char.each_slice(180).map(&:join)
  raise 'workload exceeds its eight-fragment bound' if fragments.length > 8
  created = JSON.parse(req('POST','/v1/terminal-sessions',
    {'command'=>'/bin/sh','arguments'=>['-c',
      's=$1$2$3$4$5$6$7$8; eval "$s"','blit-pty-stress',*fragments],
      'pane_id'=>pane.fetch('id')}).body)
  session = created.fetch('id'); surface = created.fetch('surface_id')
  mover = Thread.new do
    until stop
      # Keep the pointer outside the 40x20 pixel oracle while still moving
      # within this terminal's physical pane on every request.
      req('POST','/v1/input/inject',{'kind'=>'pointer_motion',
        'x'=>grid['origin_x'] + 500 + motions % 200,
        'y'=>grid['origin_y'] + 400 + motions % 100})
      motions += 1
      sleep 0.003
    end
  end
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 25
  last_sample = 0
  loop do
    raise 'PTY animation did not finish within 25 seconds' if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
    # Poll the small title resource; serializing a whole 240x67 cell snapshot
    # every 30ms would itself contaminate the cadence measurement.
    status = data("/v1/terminal-sessions/#{session}")
    break if status['title'] == 'BLIT_STRESS_DONE'
    now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    if status['title'] == 'BLIT_STRESS_ACTIVE' && now - last_sample > 0.5
      samples << data('/v1/diagnostics/runtime')
      last_sample = now
    end
    sleep 0.03
  end
  stop = true; mover.value
  # Remove the pointer from the measured pane before checking its whole
  # background. This makes every blank pixel an assertion, not an exclusion.
  req('POST','/v1/input/inject',{'kind'=>'pointer_motion','x'=>0,'y'=>0})
  shot = req('GET',"/v1/diagnostics/screenshots/#{display}")
  state = data("/v1/surfaces/#{surface}/cells")
  revisions = (shot['X-Blit-Capture-Revisions'] || '').split(',').to_h { |p| p.split('=') }
  raise 'capture revision mismatch' unless revisions[surface].to_i == state.fetch('revision')
  File.binwrite(File.join(OUT,'final.bmp'),shot.body)
  File.write(File.join(OUT,'surface-final.json'),JSON.pretty_generate(state))
  File.write(File.join(OUT,'runtime-samples.json'),JSON.pretty_generate(samples))
  File.write(File.join(OUT,'render-trace.json'),
    JSON.pretty_generate(data('/v1/diagnostics/trace?kind=render_path&limit=256')))
  bad = []; checked = 0
  state.fetch('cells').each do |cell|
    next unless cell['text'] == ' '
    next unless cell.dig('style','background') == '#102030'
    grid['cell_height'].times do |dy|
      grid['cell_width'].times do |dx|
        x = grid['origin_x'] + inset + cell['x'] * grid['cell_width'] + dx
        y = grid['origin_y'] + inset + cell['y'] * grid['cell_height'] + dy
        next if x >= shot.body[18,4].unpack1('l<') || y >= shot.body[22,4].unpack1('l<')
        bad << [cell['x'],cell['y'],dx,dy] unless pixel(shot.body,x,y) == [16,32,48]
        checked += 1
      end
    end
  end
  raise "PTY_POINTER_FAIL stale_pixels=#{bad.length} sample=#{bad.first(5)}" unless bad.empty?
  minimum_pixels = (state['width'] - 2) * (state['height'] - 1) *
    grid['cell_width'] * grid['cell_height']
  raise 'pixel check was vacuous' unless checked >= minimum_pixels
  raise 'pointer stream was not exercised' unless motions > 100
  if samples.length > 1
    first = samples.first; last = samples.last
    dt = (last.dig('performance','uptime_ms') - first.dig('performance','uptime_ms')) / 1000.0
    a = first.dig('snapshot','render','outputs').find { |o| o['display_id'] == display }
    b = last.dig('snapshot','render','outputs').find { |o| o['display_id'] == display }
    hz = (b['present_calls'] - a['present_calls']) / dt
    puts "PTY_POINTER_CADENCE submission_hz=#{hz.round(3)} window_s=#{dt.round(3)} target_hz=#{b['refresh_hz']}"
    if ENV['BLIT_REQUIRE_REFRESH'] == '1' && hz < b['refresh_hz'] * 0.97
      raise "PTY_POINTER_REFRESH_FAIL submission_hz=#{hz.round(3)} minimum_hz=#{(b['refresh_hz'] * 0.97).round(3)}"
    end
  end
  puts "PTY_POINTER_PASS producer_frames=600 pointer_requests=#{motions} blank_pixels=#{checked} stale_pixels=0 revision=#{state['revision']} evidence=#{OUT}"
ensure
  stop = true
  mover.value if mover
  req('DELETE',"/v1/terminal-sessions/#{session}") if session
  req('PATCH',"/v1/terminal-sessions/#{focused['id']}",{'focused'=>true}) if focused
  if pointer['valid']
    req('POST','/v1/input/inject',{'kind'=>'pointer_motion','x'=>pointer['x'],'y'=>pointer['y']})
  end
end
