#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0213. A picture that is on screen, vanishes for a few presented frames
# and comes back is a blink, and the compositor can now say so: the runtime
# performance carries graphics_blink_frames and graphics_blink_events, and
# /v1/diagnostics/graphics-ledger says what was transmitted, placed, deleted
# and freed around it, and in which presented frame.
#
# The owner's case: Alt+Enter in a Hibr desktop made the screen blink five or
# six times. Hibr sends each frame as one synchronized update (CSI ? 2026 h
# ... l) whose pictures are `a=d,d=I` of the old id, then `a=T` of a new id
# in 4096-byte chunks (m=1), so this gate sends exactly that, over time
# slower than the display's frames, with another session's text driving the
# frames. Each case is one sequence and one answer:
#
#   split        the delete in one write, the new picture across several
#                later ones: the picture is absent for presented frames and
#                comes back -- the instrument must see it (blink events > 0,
#                the ledger showing delete, frames, the new picture), for
#                each delete kind Hibr and Boxer use (d=I new id, d=i and d=a
#                the same id again);
#   atomic       delete and a complete new picture in one write: zero;
#   synchronized Hibr's frame, slowly: zero;
#   full redraw  Hibr's frame, slowly, while another view appears (a pane
#                coming and going is what a full-screen change is): zero --
#                the redraw must not draw the held view's half-sent state.

require 'base64'
require 'fileutils'
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19637'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
# A big picture over most of the display: 640x480 RGB, 900 kB, 300 chunks.
WIDTH = Integer(ENV.fetch('BLIT_GATE_BLINK_WIDTH', '640'), 10)
HEIGHT = Integer(ENV.fetch('BLIT_GATE_BLINK_HEIGHT', '480'), 10)
COLUMNS = WIDTH / 8
ROWS = HEIGHT / 16
CHUNK = 4096
# Hibr's wallpaper layer, and the one above it.
Z_WALL = -1_610_612_736

abort("graphics blink gate: missing #{BINARY}") unless File.executable?(BINARY)

def request(method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 30) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def check(ok, why)
  raise "GRAPHICS_BLINK_FAIL #{why}" unless ok
end

def wait_until(why, seconds = 20)
  Timeout.timeout(seconds) { sleep 0.02 until yield }
rescue Timeout::Error
  check(false, why)
end

def counters
  get('/v1/diagnostics/runtime').fetch('performance')
end

def ledger(since = 0)
  get("/v1/diagnostics/graphics-ledger?since=#{since}&limit=512")
end

def frames
  ledger.fetch('present_frames').first.fetch('frames')
end

# One picture's APC sequence the way Hibr's kt_encode writes it: a=T with the
# cursor left, the size in cells and the layer, then chunks without keys.
def picture(id, width, height, rgb, z: Z_WALL + 2)
  data = Base64.strict_encode64(rgb * (width * height))
  pieces = data.scan(/.{1,#{CHUNK}}/m)
  pieces.each_with_index.map do |piece, index|
    more = index + 1 < pieces.length ? 1 : 0
    keys = index.zero? ? "a=T,f=24,q=2,C=1,i=#{id},s=#{width},v=#{height}," \
                         "c=#{COLUMNS},r=#{ROWS},z=#{z}," : 'q=2,'
    "\e_G#{keys}m=#{more};#{piece}\e\\"
  end
end

def apc(body)
  "\e_G#{body}\e\\"
end

SYNC_ON = "\e[?2026h"
SYNC_OFF = "\e[?2026l"

env = gate_display_env.merge(
  'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
  'BLIT_AUTH_TOKEN' => '')
server = nil
at_exit do
  if server
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

Dir.mktmpdir('blit-graphics-blink') do |root|
  env['BLIT_SETTINGS_PATH'] = File.join(root, 'settings.json')
  files = File.join(root, 'files')
  FileUtils.mkdir_p(files)
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }

  # The steps of a session are lines on its input: cat a file, sleep, make a
  # mark the gate waits for, wait for a mark the gate makes.
  script = 'stty -echo; d=$1; while read -r c a; do case "$c" in ' \
           'cat) cat "$d/$a";; sleep) sleep "$a";; mark) : > "$d/$a";; ' \
           'wait) until [ -e "$d/$a" ]; do sleep 0.01; done;; esac; done'
  put = ->(name, data) { File.binwrite(File.join(files, name), data) }
  marked = ->(name) { File.exist?(File.join(files, name)) }
  mark = ->(name) { FileUtils.touch(File.join(files, name)) }
  create = lambda do |command, arguments, width, height|
    code, made = request('POST', '/v1/terminal-sessions',
                         { command: command, arguments: arguments,
                           display_id: 'display-1', surface: { width: width, height: height } })
    check(code == 201, "session: #{code} #{made}")
    made.fetch('id')
  end
  script_session = lambda do
    create.call('/bin/sh', ['-c', script, 'blink-gate', files], 40, 10)
  end
  send_steps = lambda do |session, *lines|
    request('POST', "/v1/terminal-sessions/#{session}/input", { data: lines.join("\n") + "\n" })
  end
  close = ->(session) { request('DELETE', "/v1/terminal-sessions/#{session}") }

  # Another session's text is what makes the display present frames: a
  # picture that nothing redraws is never presented again, so nothing could
  # show it gone. 50 frames a second, as a program animating would.
  ticker = create.call('/bin/sh', ['-c', 'while :; do printf "\r%s" "$(date +%N)"; sleep 0.02; done'], 20, 2)

  red = "\xff\x00\x00".b
  blue = "\x00\x00\xff".b
  put.call('big_1', picture(1, WIDTH, HEIGHT, red).join)
  put.call('tiny_1', picture(1, 16, 16, red).join)
  put.call('sync_on', SYNC_ON)
  put.call('sync_off', SYNC_OFF)
  put.call('del_I1', apc('a=d,d=I,q=2,i=1'))
  put.call('del_i1', apc('a=d,d=i,q=2,i=1'))
  put.call('del_a', apc('a=d,d=a,q=2'))

  wait_frames = lambda do |count|
    target = frames + count
    wait_until("#{count} presented frames") { frames >= target }
  end
  # A picture, transmitted and on screen, as hold would have left it.
  show_first = lambda do |session, name|
    send_steps.call(session, "cat #{name}")
    wait_until("picture 1 complete") do
      ledger.fetch('events').any? do |e|
        e['kind'] == 'transmit' && e['image_id'] == 1 && e['reason'].start_with?('complete') &&
          e['session'] == "session-#{session.delete_prefix('session-')}"
      end
    end
    wait_frames.call(6)
  end
  # The new picture's id-2 chunks in `parts` files, and the sequence that
  # sends them one by one, `gap` seconds apart.
  parts = lambda do |id, color, count, width = WIDTH, height = HEIGHT|
    pieces = picture(id, width, height, color)
    size = (pieces.length / count.to_f).ceil
    pieces.each_slice(size).each_with_index.map do |slice, index|
      name = "p#{id}_#{index}"
      put.call(name, slice.join)
      name
    end
  end
  new_parts = parts.call(2, blue, 4)
  # The split cases send a quarter of the picture's area as the new one, over
  # the same cells, so the time it takes to arrive stays under the window on
  # a slow compositor as well as a fast one.
  quarter = [WIDTH / 2, HEIGHT / 2]
  split_new = parts.call(2, blue, 4, *quarter)
  split_same = parts.call(1, blue, 4, *quarter)
  # The split cases keep the picture away for fewer presented frames than the
  # detector's window (8), so a gap is a blink and not a deletion.
  GAP = '0.025'

  results = {}

  # split: absent for frames, then back. The same sequence for each kind.
  { 'd=I,new id' => ['del_I1', split_new, 2, 'delete-d=I'],
    'd=i,same id' => ['del_i1', split_same, 1, 'delete-d=i'],
    'd=a,same id' => ['del_a', split_same, 1, 'delete-d=a'] }.each do |label, (delete, pieces, id, cause)|
    session = script_session.call
    show_first.call(session, 'big_1')
    before = counters
    cursor = ledger.fetch('sequence')
    lines = ["cat #{delete}", "sleep #{GAP}"]
    pieces.each { |piece| lines.push("cat #{piece}", "sleep #{GAP}") }
    lines.push('mark split_done')
    send_steps.call(session, *lines)
    wait_until("#{label}: split sequence ran") { marked.call('split_done') }
    FileUtils.rm_f(File.join(files, 'split_done'))
    wait_frames.call(12)
    after = counters
    events = ledger(cursor).fetch('events')
    blinks = events.select { |e| e['kind'] == 'blink' }
    deleted = events.find { |e| e['kind'] == 'delete' && e['image_id'] == 1 }
    placed = events.find { |e| e['kind'] == 'place' && e['image_id'] == id }
    landed = events.find { |e| e['kind'] == 'transmit' && e['image_id'] == id &&
                                e['reason'].start_with?('complete') }
    check((after['graphics_blink_events'] - before['graphics_blink_events']) >= 1,
          "split #{label}: no blink counted: #{events.map { |e| [e["kind"], e["frame"], e["image_id"], e["reason"]] }.inspect}")
    # A new id is absent until its last chunk; the same id again (the data
    # kept by a lowercase delete) is placed on its old pixels at the first.
    least = id == 2 ? 2 : 1
    check(after['graphics_blink_frames'] - before['graphics_blink_frames'] >= least,
          "split #{label}: the picture was gone for at least #{least} presented frames")
    check(!blinks.empty? && blinks.first['reason'].start_with?(cause) && blinks.first['frames'] >= least,
          "split #{label}: the blink names its cause #{cause}: #{blinks.first.inspect}")
    check(deleted && placed && landed && blinks.first,
          "split #{label}: ledger lacks delete, place, transmit or blink: #{events.map { |e| e['kind'] }}")
    check(deleted['frame'] < landed['frame'] && deleted['sequence'] < landed['sequence'],
          "split #{label}: the ledger shows frames between the delete (#{deleted['frame']}) and the " \
          "new picture's last chunk (#{landed['frame']})")
    check(placed['cells']['width'] == COLUMNS && placed['cells']['height'] == ROWS &&
          placed['pixels']['width'] == WIDTH && landed['bytes'] == quarter[0] * quarter[1] * 3,
          "split #{label}: the ledger carries the rectangle and the bytes: #{placed.inspect} #{landed.inspect}")
    results["split #{label}"] = "#{blinks.first['frames']} frames"
    close.call(session)
    wait_frames.call(3)
  end

  # atomic: the delete and a whole new picture in one read.
  session = script_session.call
  show_first.call(session, 'big_1')
  put.call('atomic', apc('a=d,d=I,q=2,i=1') + picture(2, 16, 16, blue).join)
  before = counters
  send_steps.call(session, 'cat atomic')
  wait_until('atomic picture landed') do
    ledger.fetch('events').any? { |e| e['image_id'] == 2 && e['reason'].to_s.start_with?('complete') }
  end
  wait_frames.call(12)
  after = counters
  check(after['graphics_blink_events'] == before['graphics_blink_events'] &&
        after['graphics_blink_frames'] == before['graphics_blink_frames'],
        'atomic: delete and a whole new picture in one write must not blink')
  results['atomic'] = 'no blink'
  close.call(session)
  wait_frames.call(3)

  # synchronized: Hibr's frame -- delete, then the new picture in chunks,
  # slowly, inside one 2026 batch. Then the same while another view appears.
  [false, true].each do |redraw|
    label = redraw ? 'synchronized with a full redraw' : 'synchronized'
    session = script_session.call
    show_first.call(session, 'big_1')
    before = counters
    cursor = ledger.fetch('sequence')
    lines = ['cat sync_on', 'cat del_I1', 'sleep 0.2', "cat #{new_parts[0]}", 'sleep 0.2']
    lines.push("cat #{new_parts[1]}", 'mark mid', 'wait go') if redraw
    lines.push("cat #{new_parts[1]}", 'sleep 0.2') unless redraw
    lines.push("cat #{new_parts[2]}", 'sleep 0.2', "cat #{new_parts[3]}", 'cat sync_off', 'mark sync_done')
    send_steps.call(session, *lines)
    other = nil
    if redraw
      wait_until("#{label}: reached the middle of the batch") { marked.call('mid') }
      # A pane appearing and going: the view count changes, which is a whole
      # redraw of the output while the batch is open.
      3.times do
        other = create.call('/bin/sh', ['-c', 'sleep 5'], 10, 3)
        wait_frames.call(3)
        close.call(other)
        wait_frames.call(3)
      end
      mark.call('go')
    end
    wait_until("#{label}: the batch finished") { marked.call('sync_done') }
    FileUtils.rm_f(File.join(files, 'mid'))
    FileUtils.rm_f(File.join(files, 'go'))
    FileUtils.rm_f(File.join(files, 'sync_done'))
    wait_frames.call(12)
    after = counters
    events = ledger(cursor).fetch('events')
    blinks = events.select { |e| e['kind'] == 'blink' }
    check(after['graphics_blink_events'] == before['graphics_blink_events'] &&
          after['graphics_blink_frames'] == before['graphics_blink_frames'],
          "#{label}: Hibr's frame must not blink, but #{blinks.inspect} " \
          "(synchronized views drawn live: " \
          "#{after['graphics_synchronized_live_draws'] - before['graphics_synchronized_live_draws']})")
    live = after['graphics_synchronized_live_draws'] - before['graphics_synchronized_live_draws']
    # The redraw must really have drawn the held view, or the case proves nothing.
    check(!redraw || live >= 1, "#{label}: no whole redraw reached the held view")
    check(redraw || live.zero?, "#{label}: a held view was drawn without a whole redraw")
    results[label] = 'no blink'
    close.call(session)
    wait_frames.call(3)
  end

  # The barrier: a Hibr desktop's primary and join hold the output's scene for
  # one display interval while only one of them has drawn, then time out.
  # Within that interval a replace that lands whole is held and shows no gap;
  # slower, the timeout presents the gap -- and says so in the ledger.
  desktop = File.join(root, 'desktop')
  File.write(desktop, "#!/bin/sh\nexec /bin/sh \"$@\"\n")
  File.chmod(0o755, desktop)
  # A pair: the primary alone first, so its picture goes up without a
  # barrier, then the join, whose waiting for the primary's next write is what
  # the case measures. Pictures alone are not cells and do not arm the
  # barrier; a frame of Hibr's draws text too, so each write begins with some.
  desktop_pair = lambda do
    primary = create.call(desktop, ['-c', script, 'blink-gate', files, '--session', 'blit'], 40, 10)
    show_first.call(primary, 'big_1')
    joined = create.call(desktop, ['-c', 'sleep 60', 'join', '--session', 'blit', '--join'], 40, 10)
    wait_frames.call(6)
    [primary, joined]
  end

  primary, joined = desktop_pair.call
  before = counters
  cursor = ledger.fetch('sequence')
  put.call('atomic_text', "\e[1;1HHibr" + File.binread(File.join(files, 'atomic')))
  send_steps.call(primary, 'cat atomic_text')
  wait_until('barrier: atomic picture landed') do
    ledger(cursor).fetch('events').any? { |e| e['image_id'] == 2 && e['reason'].to_s.start_with?('complete') }
  end
  wait_frames.call(12)
  after = counters
  check(after['graphics_blink_events'] == before['graphics_blink_events'],
        'barrier: a replace that arrives whole inside the barrier must not blink')
  check(after['graphics_barrier_holds'] > before['graphics_barrier_holds'] &&
        ledger(cursor).fetch('events').any? { |e| e['kind'] == 'barrier' && e['reason'] == 'hold' },
        'barrier: the write armed the barrier and the ledger says so')
  results['barrier held'] = 'no blink'
  close.call(primary)
  close.call(joined)
  wait_frames.call(3)

  primary, joined = desktop_pair.call
  before = counters
  cursor = ledger.fetch('sequence')
  put.call('del_I1_text', "\e[1;1Hdesk" + apc('a=d,d=I,q=2,i=1'))
  lines = ['cat del_I1_text', "sleep #{GAP}"]
  slow = parts.call(3, red, 4, *quarter)
  slow.each { |piece| lines.push("cat #{piece}", "sleep #{GAP}") }
  lines.push('mark barrier_done')
  send_steps.call(primary, *lines)
  wait_until('barrier: slow replace ran') { marked.call('barrier_done') }
  wait_frames.call(12)
  after = counters
  events = ledger(cursor).fetch('events')
  check(after['graphics_blink_events'] > before['graphics_blink_events'],
        'barrier: a replace slower than the barrier interval must show as a blink')
  check(after['graphics_barrier_timeouts'] > before['graphics_barrier_timeouts'] &&
        events.any? { |e| e['kind'] == 'barrier' && e['reason'] == 'timeout' },
        "barrier: the barrier timed out and the ledger says so: #{events.map { |e| [e['kind'], e['reason']] }}")
  results['barrier timeout'] = "#{after['graphics_barrier_timeouts'] - before['graphics_barrier_timeouts']} timeouts"
  close.call(primary)
  close.call(joined)

  close.call(ticker)
  puts "GRAPHICS_BLINK_PASS #{results.map { |k, v| "#{k}=#{v}" }.join(' ')}"
end
