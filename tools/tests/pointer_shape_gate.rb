#!/usr/bin/env ruby
# frozen_string_literal: true

# blit#138, OSC 22. Two panes side by side. A asks whether Blit can hide the
# pointer, then hides it; B asks for the I-beam. The gate checks the answer,
# that the pointer is gone over A and only over A, that B shows the exact
# I-beam art from the shared source, and that RIS (B) and the program
# leaving (A) give the display's own pointer back.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_POINTER_SHAPE_PORT', '19524')))
BASE = URI("http://127.0.0.1:#{PORT}")

def req(method, path, body = nil, revision = nil)
  q = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'PUT' => Net::HTTP::Put,
        'DELETE' => Net::HTTP::Delete }.fetch(method).new(BASE + path)
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  q['If-Match'] = revision.to_s if revision
  r = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 5) { |h| h.request(q) }
  raise "#{method} #{path}: HTTP #{r.code}: #{r.body[0, 200]}" unless r.code.start_with?('2')
  r.body
end

def data(*args)
  body = req(*args)
  body.nil? || body.empty? ? nil : JSON.parse(body)
end

def screenshot
  bmp = req('GET', '/v1/diagnostics/screenshots/display-1')
  w, h = bmp.byteslice(18, 8).unpack('l<2')
  bpp = bmp.byteslice(28, 2).unpack1('S<') / 8
  { bmp: bmp, w: w, h: h.abs, bpp: bpp, stride: (w * bpp + 3) & ~3,
    offset: bmp.byteslice(10, 4).unpack1('L<') }
end

def pixel(shot, x, y)
  pos = shot[:offset] + (shot[:h] - 1 - y) * shot[:stride] + x * shot[:bpp]
  [shot[:bmp].getbyte(pos + 2), shot[:bmp].getbyte(pos + 1), shot[:bmp].getbyte(pos)]
end

def region(shot, cx, cy, radius = 40)
  (cy - radius...cy + radius).flat_map do |y|
    (cx - radius...cx + radius).map { |x| pixel(shot, x, y) }
  end
end

def move(x, y)
  data('POST', '/v1/input/inject', { kind: 'pointer_motion', x: x, y: y })
  sleep 0.25
end

# Every non-transparent pixel of the stamp, over black, at the hotspot.
def stamp_mismatches(shot, stamp, x, y)
  w, h, hx, hy = stamp.unpack('L<4')
  rgba = stamp.byteslice(16..)
  bad = 0
  h.times do |sy|
    w.times do |sx|
      p = (sy * w + sx) * 4
      alpha = rgba.getbyte(p + 3)
      next if alpha.zero?
      got = pixel(shot, x - hx + sx, y - hy + sy)
      want = 3.times.map { |c| rgba.getbyte(p + c) * alpha / 255 }
      bad += 1 unless got.zip(want).all? { |a, b| (a - b).abs <= 1 }
    end
  end
  bad
end

def cells_text(session)
  cells = data('GET', "/v1/surfaces/surface-#{session}/cells")
  cells.fetch('cells').sort_by { |c| [c['y'], c['x']] }.map { |c| c['text'].to_s }.join
end

Dir.mktmpdir('blit-pointer-shape') do |root|
  helper = File.join(root, 'art')
  abort('cannot compile the pointer source') unless
    system('tcc', '-O2', File.join(ROOT, 'tools/tests/pointer_art_gate.c'), '-o', helper)
  stamps = {}
  { arrow: 0, text: 3 }.each do |name, index|
    path = File.join(root, name.to_s)
    system(helper, '--stamp', index.to_s, '0', '24', path) || raise("stamp #{name} failed")
    stamps[name] = File.binread(path)
  end
  pid = spawn({ 'SDL_VIDEODRIVER' => 'dummy', 'BLIT_RENDERER' => 'software',
                'BLIT_LOGIN_AUTOSTART' => '0',
                'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'),
                'BLIT_INPUT_ROOT' => root,
                'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'),
                'LD_LIBRARY_PATH' => File.join(ROOT, 'build/sdl3-kms') },
              ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server')),
              PORT.to_s, out: File.join(root, 'server.log'), err: %i[child out])
  begin
    Timeout.timeout(10) do
      loop do
        begin
          break if data('GET', '/v1/version')
        rescue Errno::ECONNREFUSED
          nil
        end
        sleep 0.02
      end
    end
    topology = data('GET', '/v1/topology')
    topology = data('PUT', '/v1/topology',
                    { revision: topology.fetch('revision'), layout: { columns: 2, rows: 1 } },
                    topology.fetch('revision'))
    panes = topology.fetch('panes').sort_by { |p| p.dig('pixel_region', 'x') }
    raise "expected two panes: #{panes.inspect}" unless panes.length == 2
    centre = panes.map do |p|
      r = p.fetch('pixel_region')
      [r.fetch('x') + r.fetch('width') / 2, r.fetch('y') + r.fetch('height') / 2]
    end

    query = "stty -icanon -echo min 0 time 5; " \
            "printf '\\033]22;?none,text,zoom-in,__current__\\033\\\\'; " \
            "dd bs=1 count=40 2>/dev/null | od -An -c | tr -d ' \\n'; " \
            "printf '\\033]22;none\\033\\\\'; stty sane; read x; exit 0"
    a = data('POST', '/v1/terminal-sessions',
             { command: '/bin/sh', arguments: ['-c', query], pane_id: panes[0].fetch('id') })
    b = data('POST', '/v1/terminal-sessions',
             { command: '/bin/sh', pane_id: panes[1].fetch('id'),
               arguments: ['-c', "printf '\\033]22;text\\033\\\\'; read x; " \
                                    "printf '\\033]22;>wait,move\\033\\\\\\033]22;<\\033\\\\'; read x; " \
                                    "printf '\\033c'; sleep 60"] })

    text = ''
    Timeout.timeout(10) do
      loop do
        text = cells_text(a.fetch('id'))
        break if text.include?('22;')
        sleep 0.05
      end
    end
    raise "OSC 22 query answer was #{text.inspect}, wanted 1,1,0,0" unless text.include?(']22;1,1,0,0')
    sleep 0.3

    move(*centre[1])
    over_b = screenshot
    move(*centre[0])
    over_a = screenshot
    raise 'the pointer is drawn over the pane that hid it' unless
      region(over_a, *centre[0]) == region(over_b, *centre[0])
    bad = stamp_mismatches(over_b, stamps[:text], *centre[1])
    raise "the I-beam over B differs from its art in #{bad} pixels" unless bad.zero?
    raise 'B shows the I-beam with the pointer elsewhere' if region(over_a, *centre[1]) == region(over_b, *centre[1])
    effective = data('GET', '/v1/settings').fetch('effective')
    shown = effective.fetch('pointer_shown')
    raise "over A the API should report hidden: #{shown.inspect}" unless shown['hidden'] == true
    raise "the API's own pointer changed: #{effective['pointer'].inspect}" unless
      effective['pointer'] == { 'shape' => 'arrow', 'palette' => 'pearl', 'size_px' => 24, 'shadow' => true }
    asked = data('GET', "/v1/terminal-sessions/#{b.fetch('id')}")['pointer_shape']
    raise "B's request is not published: #{asked.inspect}" unless asked == { 'requested' => 'text', 'depth' => 1 }
    move(*centre[1])
    shown = data('GET', '/v1/settings').dig('effective', 'pointer_shown')
    raise "over B the API should report text: #{shown.inspect}" unless
      shown == { 'shape' => 'text', 'hidden' => false }
    # A's `none` stops at the pane boundary: B's first column shows B's shape.
    edge = [panes[1].dig('pixel_region', 'x') + 3, centre[1][1]]
    move(*edge)
    shown = data('GET', '/v1/settings').dig('effective', 'pointer_shown')
    raise "A's hide reached into B's edge: #{shown.inspect}" unless shown == { 'shape' => 'text', 'hidden' => false }

    data('POST', "/v1/terminal-sessions/#{b.fetch('id')}/input", { data: "\r" })
    sleep 0.3
    asked = data('GET', "/v1/terminal-sessions/#{b.fetch('id')}")['pointer_shape']
    raise "push wait,move then pop should leave wait over text: #{asked.inspect}" unless
      asked == { 'requested' => 'wait', 'depth' => 2 }
    move(centre[1][0] + 1, centre[1][1])
    raise 'the busy shape is not shown over B' unless
      data('GET', '/v1/settings').dig('effective', 'pointer_shown', 'shape') == 'wait'
    data('POST', "/v1/terminal-sessions/#{b.fetch('id')}/input", { data: "\r" })
    sleep 0.3
    move(centre[1][0] + 1, centre[1][1])
    move(*centre[1])
    bad = stamp_mismatches(screenshot, stamps[:arrow], *centre[1])
    raise "after RIS the pointer over B is not the display's own (#{bad} pixels differ)" unless bad.zero?

    data('POST', "/v1/terminal-sessions/#{a.fetch('id')}/input", { data: "\r" })
    Timeout.timeout(10) do
      sleep 0.05 until data('GET', "/v1/terminal-sessions/#{a.fetch('id')}")['state'] != 'running'
    end
    move(centre[0][0] + 1, centre[0][1])
    move(*centre[0])
    bad = stamp_mismatches(screenshot, stamps[:arrow], *centre[0])
    raise "after A exited the pointer over A is not back (#{bad} pixels differ)" unless bad.zero?

    puts 'POINTER_SHAPE_PASS query=1,1,0,0 hidden=pane_a_only text=exact push_pop=wait ris=restored exit=restored'
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
