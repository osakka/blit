#!/usr/bin/env ruby
# frozen_string_literal: true

# A picture under the text never shows through opaque cells, on whichever
# compositor BLIT_GATE_BACKEND picks (the suite runs it on both). Seen live
# on a GLES desktop: the wallpaper, an under-text Kitty image placed at the
# full pane size, was sent again, and the screen then showed it over cells
# that were opaque in Blit's model until a full repaint.
#
# A one-pixel image scaled to the whole pane at the wallpaper's z, under the
# cell backgrounds, over default-background cells: the pane is the image.
# Then the program erases the screen to the explicit background #000001
# (opaque: only the default background lets a picture under the text
# through) and writes a bright word. The erase damaged only cells that were
# not already blank, so the blank cells it made opaque were never repainted
# and the picture stayed on screen over them. From there, after each step,
# the pane shows its cells -- #000001 or the word -- and never the image:
#
#   * erased over the picture
#   * deleted (a=d,d=I) and placed again
#   * replaced in place by a=T with the same id and another colour
#   * deleted, placed and the screen redrawn inside synchronized output
#
# Then the right half of each row is erased to the default background: those
# cells show the image, exactly, and the left half still does not.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19595'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
W = 1024
H = 768
CELL_W = 8
CELL_H = 16
OPAQUE = [0, 0, 1].freeze
RED = [255, 0, 0].freeze
GREEN = [0, 255, 0].freeze
Z_WALLPAPER = -1_610_612_736

abort("under-text picture gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 20) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "UNDER_TEXT_PICTURE_FAIL #{why}" unless ok
end

# A wait for a state, not a measurement: most of them poll a whole-display
# screenshot, which a loaded host takes seconds to make, so the bound is
# generous (it was 5 s, and a run beside other gates ran out).
def wait_until(why, seconds = 30)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def screenshot
  GateBmp.new(Net::HTTP.get(BASE + '/v1/diagnostics/screenshots/display-1'))
end

env = gate_display_env(width: W, height: H).merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_PANE_LAYOUT' => '1x1', 'BLIT_AUTH_TOKEN' => '')
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-under-text-picture') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  # Each input line is one step. ED with a background set erases to it, so
  # after `opaque` every cell carries the explicit #000001.
  script = File.join(root, 'steps.sh')
  File.write(script, <<~'SH')
    stty -echo
    set -- $(stty size)
    rows=$1 columns=$2
    place() {
        printf '\033_Ga=T,f=24,s=1,v=1,i=5,c=%d,r=%d,z=%d,C=1,q=2;%s\033\\' \
            "$columns" "$rows" "$Z" "$1"
    }
    printf '\033[?25l'
    while read -r step; do
        case "$step" in
        place) place /wAA ;;
        opaque) printf '\033[48;2;0;0;1m\033[2J\033[3;3H\033[97mUNDER THE TEXT\033[H' ;;
        delete) printf '\033_Ga=d,d=I,i=5,q=2\033\\' ;;
        replace) place AP8A ;;
        sync)
            printf '\033[?2026h\033_Ga=d,d=I,i=5,q=2\033\\'
            place /wAA
            printf '\033[48;2;0;0;1m\033[2J\033[3;3H\033[97mUNDER THE TEXT\033[H\033[?2026l' ;;
        half)
            printf '\033[49m'
            row=1
            while [ "$row" -le "$rows" ]; do
                printf '\033[%d;%dH\033[K' "$row" $((columns / 2 + 1))
                row=$((row + 1))
            done
            printf '\033[H' ;;
        esac
    done
  SH
  server = gate_spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
  code, created = request('POST', '/v1/terminal-sessions',
                          { command: '/bin/sh', arguments: [script], pane_id: 'pane-display-1-1',
                            environment: { 'Z' => Z_WALLPAPER.to_s } })
  check(code == 201, "session: #{code} #{created}")
  session = created.fetch('id')
  request('POST', '/v1/input/inject', { kind: 'pointer_motion', x: 0, y: H - 1 })
  backend = GATE_BACKEND == 'gles' ? 'gles' : 'cpu'
  wait_until("the #{backend} compositor is not presenting") do
    get('/v1/diagnostics')['render']['outputs'].first.dig('compositor', 'active') == backend
  end
  inset = get('/v1/diagnostics')['render']['surface_inset_px']
  columns = (W - 2 * inset) / CELL_W
  rows = (H - 2 * inset) / CELL_H
  left = inset + (W - 2 * inset - columns * CELL_W) / 2
  top = inset + (H - 2 * inset - rows * CELL_H) / 2
  cells = lambda do |shot, first_column, last_column|
    (top...(top + rows * CELL_H)).flat_map do |y|
      ((left + first_column * CELL_W)...(left + last_column * CELL_W)).map { |x| shot.pixel(x, y) }
    end.tally
  end
  step = lambda do |name|
    request('POST', "/v1/terminal-sessions/#{session}/input", { data: "#{name}\n" })
    sleep 0.6
  end
  step.call('place')
  wait_until('the image was not stored') { get("/v1/terminal-sessions/#{session}")['graphics'].length == 1 }
  # All of it but the few pixels of the pointer, parked at the bottom left.
  wait_until('default-background cells do not show the image') do
    cells.call(screenshot, 0, columns)[RED].to_i >= rows * columns * CELL_W * CELL_H - 64
  end
  step.call('opaque')
  before = cells.call(screenshot, 0, columns)
  leaked = before.select { |colour, _| [RED, GREEN].include?(colour) }
  check(leaked.empty?, "erased to #000001 over the picture, it still shows (#{leaked.values.sum} pixels)")
  check(before[OPAQUE].to_i > rows * columns * 100 && !(before.keys - [OPAQUE]).empty?,
        "the pane is not #000001 cells and the word: #{before.first(4)}")
  # From here the cells exactly as they were: the image nowhere, the word
  # intact.
  shown = lambda do |what|
    now = cells.call(screenshot, 0, columns)
    leaked = now.select { |colour, _| [RED, GREEN].include?(colour) }
    check(leaked.empty? && now == before,
          "#{what}: the image shows through opaque cells (#{leaked.values.sum} pixels: #{leaked})")
  end

  step.call('delete')
  shown.call('deleted')
  step.call('place')
  shown.call('deleted and placed again')
  step.call('replace')
  shown.call('replaced in place by a=T')
  step.call('sync')
  shown.call('deleted and placed again inside synchronized output')
  step.call('replace')

  # Default-background cells let it through; opaque ones still do not.
  step.call('half')
  half = columns - columns / 2
  shot = screenshot
  right = cells.call(shot, columns / 2, columns)
  check(right == { GREEN => rows * half * CELL_W * CELL_H },
        "default-background cells do not show the image: #{right.first(4)}")
  left_half = cells.call(shot, 0, columns / 2)
  check(!left_half.key?(GREEN) && !left_half.key?(RED), 'the image shows through the opaque half')

  puts "UNDER_TEXT_PICTURE_PASS backend=#{backend} z=#{Z_WALLPAPER} opaque=#000001 " \
       'hidden=erased_over+deleted_and_placed+replaced+synchronized default_background=shows_image'
end
