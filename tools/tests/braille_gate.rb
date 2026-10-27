#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0191: Braille is cell geometry. Under the Blit Modern outline profile
# at 22 px and 16 px, a session prints U+28FF (all eight dots), U+2800
# (none), and btop's U+2809 and U+28C0 in white on black. From the pane's
# cell grid each dot centre is checked lit or dark by its bit, and the gaps
# between dots dark -- so the dots are round, separate and on the grid, and
# no Braille falls back to '?'.
#
# BLIT_GATE_BACKEND=native (CPU compositor) or gles.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19545'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
W = 640
H = 384

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def check(ok, why)
  raise "BRAILLE_FAIL #{why}" unless ok
end

# Dot n (0..7) -> [column, row], from the Braille bit layout.
DOTS = [[0, 0], [0, 1], [0, 2], [1, 0], [1, 1], [1, 2], [0, 3], [1, 3]].freeze
GLYPHS = { 0x28FF => 0xFF, 0x2800 => 0x00, 0x2809 => 0x09, 0x28C0 => 0xC0 }.freeze

Dir.mktmpdir('blit-braille') do |root|
  env = gate_display_env(width: W, height: H).merge(
    'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
    'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  checked = 0
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    text = GLYPHS.keys.map { |cp| [cp].pack('U') }.join(' ')
    [22, 16].each do |size|
      code, = request('PUT', '/v1/fonts', { font_id: 'hack', size_px: size })
      check(code == 200, "font #{size}px -> #{code}")
      sleep 0.5
      code, session = request('POST', '/v1/terminal-sessions',
                              { command: '/bin/sh', display_id: 'display-1',
                                arguments: ['-c', "printf '\\033[?25l\\033[38;2;255;255;255;48;2;0;0;0m\\033[2J\\033[2;3H#{text}'; sleep 600"] })
      check(code == 201, "session -> #{code}")
      sleep 1.0
      cells = request('GET', "/v1/surfaces/#{session['surface_id']}/cells")[1]['cells']
      shown = cells.select { |c| c['y'] == 1 }.map { |c| c['text'] }.join
      check(shown.include?(text), "the surface does not hold the Braille: #{shown.inspect}")
      grid = request('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
      cw = grid['cell_width']
      ch = grid['cell_height']
      shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
      GLYPHS.each_with_index do |(cp, bits), index|
        x0 = grid['origin_x'] + (2 + index * 2) * cw
        y0 = grid['origin_y'] + 1 * ch
        DOTS.each_with_index do |(col, row), dot|
          px = x0 + (cw * (2 * col + 1) / 4.0).floor
          py = y0 + (ch * (2 * row + 1) / 8.0).floor
          lit = shot.pixel(px, py).max
          if bits[dot] == 1
            check(lit >= 160, "#{size}px U+#{cp.to_s(16).upcase} dot #{dot + 1} at #{px},#{py} is dark (#{lit})")
          else
            check(lit <= 40, "#{size}px U+#{cp.to_s(16).upcase} dot #{dot + 1} at #{px},#{py} is lit (#{lit})")
          end
          checked += 1
        end
        next unless bits == 0xFF
        # Between vertical neighbours, the gap is dark: the dots do not touch.
        3.times do |row|
          gx = x0 + (cw / 4.0).floor
          gy = y0 + (ch * (row + 1) / 4.0).floor
          check(shot.pixel(gx, gy).max <= 90, "#{size}px dots touch between rows #{row + 1} and #{row + 2} (#{shot.pixel(gx, gy)})")
        end
        gx = x0 + (cw / 2.0).floor
        gy = y0 + (ch / 8.0).floor
        check(shot.pixel(gx, gy).max <= 90, "#{size}px dots touch across the columns (#{shot.pixel(gx, gy)})")
      end
      request('DELETE', "/v1/terminal-sessions/#{session['id']}")
      sleep 0.3

      # Block Elements are cell geometry too: halves, quadrants (Claude
      # Code's logo is ▐▛███▜▌), the full block and the shades. Each is
      # sampled a quarter in from the cell's sides: [left/right, top/bottom].
      blocks = { 0x2580 => [[1, 1], [0, 0]], 0x2584 => [[0, 0], [1, 1]], 0x258C => [[1, 0], [1, 0]],
                 0x2590 => [[0, 1], [0, 1]], 0x259B => [[1, 1], [1, 0]], 0x259C => [[1, 1], [0, 1]],
                 0x2598 => [[1, 0], [0, 0]], 0x259D => [[0, 1], [0, 0]], 0x2596 => [[0, 0], [1, 0]],
                 0x2597 => [[0, 0], [0, 1]], 0x2588 => [[1, 1], [1, 1]] }
      line = blocks.keys.map { |cp| [cp].pack('U') }.join(' ') + ' ' + [0x2591, 0x2592, 0x2593].pack('U*')
      code, session = request('POST', '/v1/terminal-sessions',
                              { command: '/bin/sh', display_id: 'display-1',
                                arguments: ['-c', "printf '\\033[?25l\\033[38;2;255;255;255;48;2;0;0;0m\\033[2J\\033[2;3H#{line}'; sleep 600"] })
      check(code == 201, "block session -> #{code}")
      sleep 1.0
      shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
      blocks.each_with_index do |(cp, (top, bottom)), index|
        x0 = grid['origin_x'] + (2 + index * 2) * cw
        y0 = grid['origin_y'] + ch
        [[top, cw / 4, ch / 4], [bottom, cw / 4, 3 * ch / 4]].each do |(left_on, right_on), _, py|
          [[left_on, cw / 4], [right_on, 3 * cw / 4]].each do |on, px|
            v = shot.pixel(x0 + px, y0 + py).max
            want = on == 1 ? 'lit' : 'dark'
            check(on == 1 ? v >= 200 : v <= 30,
                  "#{size}px U+#{cp.to_s(16).upcase} is not #{want} at #{px},#{py} of its cell (#{v})")
            checked += 1
          end
        end
      end
      x0 = grid['origin_x'] + (2 + blocks.size * 2) * cw
      [[0, 64], [1, 128], [2, 192]].each do |i, level|
        v = shot.pixel(x0 + i * cw + cw / 2, grid['origin_y'] + ch + ch / 2).max
        check((v - level).abs <= 12, "#{size}px shade U+#{(0x2591 + i).to_s(16).upcase} is #{v}, expected about #{level}")
      end
      request('DELETE', "/v1/terminal-sessions/#{session['id']}")
      sleep 0.3
    end
    puts "BRAILLE_PASS backend=#{GATE_BACKEND} sizes=22,16 cells_checked=#{checked} braille_gaps=dark blocks=halves,quadrants,shades fallback=none"
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
