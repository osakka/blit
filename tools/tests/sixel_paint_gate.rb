#!/usr/bin/env ruby
# frozen_string_literal: true

# Gitea #85: a sixel is paint. Text written into a cell, the same blank
# written again, and an erase each take the place of the sixel's pixels in
# their cells, as on every sixel terminal; cells not written over keep it.
# A solid red sixel is drawn at the top left of a one-pane display, then
# written over in three ways, and the screenshot read at cell centres.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

PORT = gate_port(Integer(ENV.fetch('BLIT_SIXEL_PAINT_PORT', '19531')))
BASE = URI("http://127.0.0.1:#{PORT}")

def req(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  request = klass.new(path)
  request['Content-Type'] = 'application/json'
  request.body = JSON.generate(body) if body
  Net::HTTP.start(BASE.host, BASE.port) { |http| http.request(request) }
end

def data(*args)
  response = req(*args)
  raise "#{args[0]} #{args[1]} -> #{response.code}: #{response.body}" unless response.code.start_with?('2')
  JSON.parse(response.body)
end

def screenshot
  bmp = req('GET', '/v1/diagnostics/screenshots/display-1').body
  w, h = bmp.byteslice(18, 8).unpack('l<2')
  bpp = bmp.byteslice(28, 2).unpack1('S<') / 8
  { bmp: bmp, w: w, h: h, bpp: bpp, stride: (w * bpp + 3) & ~3, offset: bmp.byteslice(10, 4).unpack1('L<') }
end

# [r, g, b] at screen pixel (x, y); a bottom-up BMP when the height is positive.
def pixel(shot, x, y)
  row = shot[:h].positive? ? shot[:h] - 1 - y : y
  at = shot[:offset] + row * shot[:stride] + x * shot[:bpp]
  b, g, r = shot[:bmp].byteslice(at, 3).bytes
  [r, g, b]
end

def red?(rgb)
  rgb[0] > 200 && rgb[1] < 60 && rgb[2] < 60
end

failures = []
check = lambda { |ok, what| failures << what unless ok }

Dir.mktmpdir('blit-sixel-paint') do |root|
  fifo = File.join(root, 'out')
  pid = gate_spawn({ **gate_display_env, 'BLIT_LOGIN_AUTOSTART' => '0',
                     'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'),
                     'BLIT_PSF_FONT' => File.join(GATE_ROOT, 'assets/fonts/Blit-Terminus16.psf') },
                   ENV.fetch('BLIT_GATE_BINARY', File.join(GATE_ROOT, 'build/blit/blit-server')),
                   PORT.to_s, out: File.join(root, 'server.log'), err: %i[child out])
  begin
    Timeout.timeout(10) do
      loop do
        begin
          break if req('GET', '/v1/version').code == '200'
        rescue Errno::ECONNREFUSED
          nil
        end
        sleep 0.02
      end
    end
    # The session prints what the gate drops into `cmd`, as the terminal's output.
    script = "stty raw -echo; while :; do if [ -f #{root}/cmd ]; then cat #{root}/cmd; rm -f #{root}/cmd; fi; sleep 0.05; done"
    pane = data('GET', '/v1/topology').fetch('panes').first
    session = data('POST', '/v1/terminal-sessions',
                   { command: '/bin/sh', arguments: ['-c', script], pane_id: pane.fetch('id') })
    region = pane.fetch('pixel_region')
    ox = region.fetch('x')
    oy = region.fetch('y')
    fonts = data('GET', '/v1/fonts')
    cw = fonts.dig('active', 'cell_width') || 8
    ch = fonts.dig('active', 'cell_height') || 16
    say = lambda do |text|
      File.binwrite("#{fifo}.part", text)
      File.rename("#{fifo}.part", File.join(root, 'cmd'))
      sleep 0.6
    end
    # Solid red, 6 cells wide and 3 rows tall (one band is six pixel rows).
    w = cw * 6
    bands = ((ch * 3) + 5) / 6
    sixel = "\eP0;1q\"1;1;#{w};#{bands * 6}#1;2;100;0;0#1" + (["!#{w}~"] * bands).join('-') + "\e\\"
    say.call("\e[2J\e[H#{sixel}")
    centre = ->(col, row) { [ox + col * cw + cw / 2, oy + row * ch + ch / 2] }
    corner = ->(col, row) { [ox + col * cw + 1, oy + row * ch + 1] }
    shot = screenshot
    check.call(red?(pixel(shot, *centre.call(0, 0))) && red?(pixel(shot, *centre.call(4, 2))),
               "the sixel was not drawn: #{pixel(shot, *centre.call(0, 0))}")
    say.call("\e[1;1HX")                  # a character into cell (0, 0)
    say.call("\e[2;3H\e[K")               # an erase from (2, 1) to the end of the line
    say.call("\e[3;1H \e[10;1H")          # a blank written again over a blank cell (0, 2); the cursor away
    shot = screenshot
    check.call(!red?(pixel(shot, *corner.call(0, 0))), 'a character written over the sixel left it showing')
    check.call(red?(pixel(shot, *centre.call(1, 0))), 'a cell not written over lost the sixel')
    check.call(!red?(pixel(shot, *centre.call(2, 1))) && !red?(pixel(shot, *centre.call(5, 1))),
               'an erase over the sixel left it showing')
    check.call(red?(pixel(shot, *centre.call(1, 1))), 'the cell left of the erase lost the sixel')
    check.call(!red?(pixel(shot, *centre.call(0, 2))), 'a blank written again over a blank cell left the sixel')
    check.call(red?(pixel(shot, *centre.call(1, 2))), 'the cell after the rewritten blank lost the sixel')
    data('POST', "/v1/terminal-sessions/#{session.fetch('id')}/input", { data: "\u0003" }) rescue nil
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end

abort("SIXEL_PAINT_FAIL #{failures.join('; ')}") unless failures.empty?
puts "SIXEL_PAINT_PASS backend=#{GATE_BACKEND} written=gone kept=kept erase=gone same-blank=gone"
