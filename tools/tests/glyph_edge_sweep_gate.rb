#!/usr/bin/env ruby
# frozen_string_literal: true

# The compositors' cell edges. Full-cell glyphs (blocks, box lines, powerline,
# Blit's Chalk and Slate window boxes, their pressed states and the grow
# cells (ADR 0219), the minimise boxes (ADR 0222), and the button dots) are exact
# geometry that runs edge to edge, so a pixel lost at a cell's edge is a
# clipped button or a seam. This runs the real server on the DRM mock with the
# CPU compositor and with the GLES compositor, at the owner's 22 px and at
# 24, 32, 40, 48, 56 and 64 px (Hack), prints a scene of those glyphs across
# the whole width of a pane, takes the screenshot and compares every pixel of
# every cell against the production raster's own coverage for that glyph:
#   - a pixel the raster draws fully (255) must not be the background;
#   - a pixel the raster leaves clear (0) must be the background;
#   - partial coverage is anti-aliasing and is not compared.
# The cells run to the pane's last column (the remainder columns beyond are
# the background), and the pane's origin is read from the topology, so a
# half-pixel or one-pixel loss at a cell's edge, at the pane's edge or between
# neighbours fails here with its size, backend, glyph and pixel.

require 'json'
require 'net/http'
require 'open3'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19571'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
BASE = URI("http://127.0.0.1:#{PORT}")
FG = [255, 200, 0].freeze
BG = [40, 80, 160].freeze
SIZES = (ENV['BLIT_EDGE_SIZES'] || '22,24,32,40,48,56,64').split(',').map(&:to_i)
ROWS = [
  [0x2588], [0x2580, 0x2584, 0x258C, 0x2590], [0x2500, 0x2501], [0x2502, 0x2503],
  [0x2550, 0x2551, 0x2500, 0x2502], [0xE0B0, 0xE0B1, 0xE0B2, 0xE0B3],
  [0x10023A, 0x10023B, 0x10023C, 0x10025D, 0x10025E, 0x10025F], [0x25CF, 0x25A0, 0x25C6, 0x2501],
  # ADR 0219: the pressed boxes and the grow cells, Chalk's five then Slate's.
  (0x1002BB..0x1002BF).to_a, (0x1002C0..0x1002C4).to_a,
  # ADR 0222: the minimise box and the box pressed, Chalk's then Slate's.
  (0x1002CD..0x1002D0).to_a
].freeze

def fail!(why)
  abort "GLYPH_EDGE_FAIL #{why}"
end

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'PUT' => Net::HTTP::Put, 'POST' => Net::HTTP::Post, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

Dir.chdir(ROOT)
freetype = ENV.fetch('BLIT_FREETYPE_INCLUDE', "#{ROOT}/build/deps/prefix/include/freetype2")
failures = []
checked = 0
Dir.mktmpdir('blit-edge-sweep') do |work|
  harness = File.join(work, 'raster')
  fail!('the raster harness does not compile') unless system(
    'tcc', '-O2', '-std=c99', "-I#{freetype}", 'tools/tests/glyph_edge_sweep_gate.c', 'src/blit_font.c',
    'src/blit_raster.c', ENV.fetch('BLIT_FREETYPE_LIBRARY', '/lib/x86_64-linux-gnu/libfreetype.so.6'),
    ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'), '-lm', '-o', harness)
  backends = %w[cpu gles]
  backends.each do |backend|
    SIZES.each do |size|
      env = gate_display_env(backend: backend == 'gles' ? 'gles' : 'native').merge(
        'BLIT_PSF_FONT' => File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
        'BLIT_FONT_ROOT' => File.join(ROOT, 'assets/fonts'), 'BLIT_LOGIN_AUTOSTART' => '0',
        'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
        'BLIT_SETTINGS_PATH' => File.join(work, "settings-#{backend}-#{size}.json"))
      pid = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
      begin
        Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
        code, = request('PUT', '/v1/fonts', { font_id: 'hack', size_px: size })
        fail!("font #{size} -> #{code}") unless code == 200
        sleep 0.4
        if backend == 'gles'
          comp = request('GET', '/v1/diagnostics')[1].dig('render', 'outputs', 0, 'compositor')
          comp = comp['active'] || comp['mode'] || comp['name'] if comp.is_a?(Hash)
          unless comp.to_s.include?('gles')
            puts "GLYPH_EDGE_SKIP gles unavailable (#{comp.inspect})"
            next
          end
        end
        # Size first, then the grid, then the scene to its last column.
        probe = request('POST', '/v1/terminal-sessions',
                        { command: '/bin/sh', display_id: 'display-1', arguments: ['-c', 'sleep 600'] })
        fail!("probe session -> #{probe.first}") unless probe.first == 201
        sleep 0.8
        grid = request('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
        cw = grid['cell_width']; ch = grid['cell_height']
        cols = grid['columns']
        request('DELETE', "/v1/terminal-sessions/#{probe[1]['id']}")
        sleep 0.3
        sc = "\\033[48;2;#{BG.join(';')};38;2;#{FG.join(';')}m\\033[2J"
        ROWS.each_with_index do |row, i|
          line = Array.new(cols) { |c| [row[c % row.size]].pack('U') }.join
          sc += "\\033[#{i + 1};1H#{line}"
        end
        path = File.join(work, "scene-#{backend}-#{size}.sh")
        File.write(path, "printf '\\033[?25l#{sc}'; sleep 600\n")
        request('POST', '/v1/terminal-sessions',
                { command: '/bin/sh', display_id: 'display-1', arguments: [path] })
        sleep 1.5
        grid = request('GET', '/v1/topology')[1]['panes'][0]['cell_grid']
        shot = GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
        File.binwrite(ENV['BLIT_EDGE_SHOT'].sub('%', "#{backend}-#{size}"), shot.bytes) if ENV['BLIT_EDGE_SHOT']
        ox = grid['origin_x']; oy = grid['origin_y']
        cps = ROWS.flatten.uniq
        ref = {}
        out, _, st = Open3.capture3(harness, 'assets/fonts/Blit-Terminus16.psf', 'assets/fonts/text/Hack-Regular.ttf',
                                    'assets/fonts/NotoColorEmoji.ttf', cw.to_s, ch.to_s,
                                    stdin_data: cps.map { |c| c.to_s(16) }.join("\n"))
        fail!('the raster harness failed') unless st.success?
        out.each_line { |l| f = l.split; ref[f[0].to_i(16)] = f[1] unless f[1] == 'none' }
        ROWS.each_with_index do |row, r|
          grid['columns'].times do |c|
            cp = row[c % row.size]
            cov = ref[cp] or fail!("U+#{cp.to_s(16)} is not a coverage cell at #{cw}x#{ch}")
            ch.times do |y|
              cw.times do |x|
                v = cov[2 * (y * cw + x), 2].hex
                next unless v.zero? || v == 255
                px = ox + c * cw + x
                py = oy + r * ch + y
                # The pointer sits at the screen's centre (a 32 px sprite).
                next if (px - shot.width / 2).between?(-2, 34) && (py - shot.height / 2).between?(-2, 34)
                got = shot.pixel(px, py)
                checked += 1
                ink = got != BG
                next if v == 255 ? ink : !ink
                failures << format('%s %dpx %dx%d U+%X row %d col %d pixel %d,%d is %s (raster %s)',
                                   backend, size, cw, ch, cp, r, c, x, y, got.inspect, v == 255 ? 'ink' : 'clear')
              end
            end
          end
        end
        # The remainder: past the last column the pane is its background.
        rem = shot.width - (ox + cols * cw)
        puts "  #{backend} #{size}px cell #{cw}x#{ch} origin #{ox},#{oy} cols #{cols} remainder #{rem}px"
      ensure
        Process.kill('TERM', pid) rescue nil
        Process.wait(pid) rescue nil
      end
    end
  end
end
unless failures.empty?
  failures.first(20).each { |f| puts "  #{f}" }
  by = failures.map { |f| f.split.first(2).join(' ') }.tally
  puts "  by backend/size: #{by}"
  fail!("#{failures.length} edge pixels differ from the raster, of #{checked}")
end
puts "GLYPH_EDGE_PASS pixels=#{checked} sizes=#{SIZES.join(',')} backends=cpu,gles"
