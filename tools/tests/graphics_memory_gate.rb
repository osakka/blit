#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0226: pictures bounded by memory, not by count. Four scenarios, each
# with its own server:
#
#   many     1000 small pictures in one session, 500 of them placed: every
#            placed one is on the screenshot (each has its own colour), the
#            GPU uploads nothing more while they stay on screen
#            (graphics_upload_bytes flat over the next frames; on the CPU
#            compositor it is 0 throughout), and the bytes the session and
#            /v1/diagnostics report are the pictures' own plus their textures.
#   session  a 1 MiB session budget: shown pictures arriving past it free the
#            unshown ones least recently shown first -- not oldest first --
#            and once only shown pictures are left the next is refused
#            ENOSPC.
#   server   a 1 MiB server budget over two sessions: the second session's
#            shown pictures free the unshown pictures of both, least
#            recently shown first, then are refused ENOSPC.
#   ceiling  4096 pictures on screen; picture 4097 is refused ENOSPC for the
#            ceiling.
#
# The programs take their steps from a FIFO, so their standard input holds
# only Kitty's replies, which they copy to a file the gate reads.

require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'zlib'
require_relative 'gate_port'
require_relative 'gate_server'
require_relative 'gate_bmp'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19661'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf')
BASE = URI("http://127.0.0.1:#{PORT}")
MIB = 1024 * 1024

abort("graphics memory gate: missing #{BINARY}") unless File.executable?(BINARY)

def check(ok, why)
  raise "GRAPHICS_MEMORY_FAIL #{why}" unless ok
end

def request(method, path, body = nil, revision = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post, 'PUT' => Net::HTTP::Put }.fetch(method)
  req = klass.new(BASE + path)
  req['Content-Type'] = 'application/json'
  req['If-Match'] = revision.to_s if revision
  req.body = JSON.generate(body) if body
  response = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 60) { |h| h.request(req) }
  text = response.body.to_s
  [response.code.to_i, text.empty? ? nil : (JSON.parse(text) rescue text)]
end

def get(path)
  code, value = request('GET', path)
  raise "GET #{path} -> #{code} #{value}" unless code == 200
  value
end

def wait_until(why, seconds = 20)
  Timeout.timeout(seconds) { sleep 0.05 until yield }
rescue Timeout::Error
  check(false, why)
end

def apc(control, payload = '')
  "\e_G#{control};#{payload}\e\\"
end

# One solid picture, s x v pixels of `rgb` (f=24) or `rgba` (f=32), sent in
# 4096-character chunks.
def picture(control, width, height, colour)
  raw = colour.pack('C*') * (width * height)
  data = [raw].pack('m0')
  format = colour.length == 3 ? 24 : 32
  chunks = data.scan(/.{1,4096}/m)
  chunks.each_with_index.map do |chunk, index|
    more = index + 1 < chunks.length ? 1 : 0
    head = index.zero? ? "#{control},f=#{format},s=#{width},v=#{height},m=#{more}" : "m=#{more}"
    apc(head, chunk)
  end.join
end

def trace(kind)
  get("/v1/diagnostics/trace?kind=#{kind}&limit=4096").fetch('events').map { |e| e.fetch('detail') }
end

def uploaded_bytes
  find = lambda do |value|
    case value
    when Hash
      return value['graphics_upload_bytes'] if value.key?('graphics_upload_bytes')
      value.each_value { |item| (got = find.call(item)) && (return got) }
      nil
    when Array
      value.each { |item| (got = find.call(item)) && (return got) }
      nil
    end
  end
  find.call(get('/v1/diagnostics/runtime'))
end

# A server with these server settings, applied live, for the block's length.
def with_server(root, settings)
  env = gate_display_env.merge(
    'BLIT_PSF_FONT' => FONT, 'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0',
    'BLIT_AUTH_TOKEN' => '', 'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  server = spawn(env, BINARY, PORT.to_s, out: File::NULL, err: File::NULL)
  begin
    Timeout.timeout(15) { loop { break if (request('GET', '/v1/version').first rescue nil) == 200; sleep 0.1 } }
    if settings
      current = get('/v1/settings')
      document = current['document'].empty? ? {} : current['document']
      document['server'] = (document['server'] || {}).merge(settings)
      code, reply = request('PUT', '/v1/settings', { document: document }, current['revision'])
      check(code == 200, "settings #{settings}: #{code} #{reply}")
    end
    yield
  ensure
    Process.kill('TERM', server) rescue nil
    Process.wait(server) rescue nil
  end
end

# A session whose program sends the file named by each line written to its
# FIFO, and copies what Blit answers to `replies`.
class Program
  attr_reader :id

  def initialize(root, name, columns: 80, rows: 30)
    @dir = File.join(root, name)
    Dir.mkdir(@dir)
    @fifo = File.join(@dir, 'fifo')
    system('mkfifo', @fifo) or raise 'mkfifo'
    # A background job's standard input is /dev/null unless it says otherwise.
    script = 'stty raw -echo; exec 3<&0; cat <&3 > "$1/replies" & ' \
             'while read -r step < "$1/fifo"; do [ "$step" = quit ] && break; cat "$1/$step"; done; ' \
             'sleep 600'
    code, created = request('POST', '/v1/terminal-sessions',
                            { command: '/bin/sh', arguments: ['-c', script, name, @dir],
                              display_id: 'display-1', surface: { width: columns, height: rows } })
    check(code == 201, "session #{name}: #{code} #{created}")
    @id = created.fetch('id')
    @steps = 0
  end

  def send(bytes)
    @steps += 1
    name = "step#{@steps}"
    File.binwrite(File.join(@dir, name), bytes)
    File.write(@fifo, "#{name}\n")
  end

  def state
    get("/v1/terminal-sessions/#{@id}")
  end

  def memory
    state.fetch('graphics_memory')
  end

  def ids
    state.fetch('graphics').map { |g| g.fetch('id') }.sort
  end

  def replies
    File.binread(File.join(@dir, 'replies')) rescue ''
  end
end

Dir.mktmpdir('blit-graphics-memory') do |root|
  # ---- many: 1000 pictures, 500 on screen. ----
  colour = ->(i) { [16 + i % 200, 40 + (i / 200) * 40, 201] }
  many = {}
  with_server(File.join(root, 'many').tap { |d| Dir.mkdir(d) }, nil) do
    server = get('/v1/diagnostics').fetch('graphics')
    check(server['max_images'] == 4096 && server['max_placements'] == 16_384,
          "ceilings published #{server.slice('max_images', 'max_placements')}")
    check(server.dig('budget', 'session_bytes') == 256 * MIB &&
          server.dig('budget', 'texture_bytes') == 512 * MIB &&
          server.dig('budget', 'server_bytes').between?(1, 2048 * MIB),
          "default budgets #{server['budget']}")
    program = Program.new(File.join(root, 'many'), 'many', columns: 100, rows: 40)
    program.send((1..1000).map { |i| picture("a=t,i=#{i},q=1", 8, 8, colour.call(i)) }.join)
    wait_until('1000 pictures did not arrive', 60) { program.memory['images'] == 1000 }
    # 500 placed, one 8x8 picture in every other cell of 13 rows, the cursor
    # kept where it is.
    program.send((1..500).map do |i|
      row = (i - 1) / 40 + 2
      column = ((i - 1) % 40) * 2 + 2
      "\e[#{row};#{column}H" + apc("a=p,i=#{i},C=1,q=1")
    end.join + "\e[39;1H")  # the cursor off the pictures
    wait_until('500 placements were not made', 30) { program.memory['placements'] == 500 }
    sleep 1.0
    shot = GateBmp.new(Net::HTTP.get_response(BASE + '/v1/diagnostics/screenshots/display-1').body)
    seen = {}
    shot.each_pixel { |_x, _y, rgb| seen[rgb] = true if rgb[2] == 201 }
    missing = (1..500).reject { |i| seen[colour.call(i)] }
    check(missing.empty?, "#{missing.length} of 500 placed pictures not drawn, e.g. #{missing.first(5)}")
    unplaced = (501..1000).select { |i| seen[colour.call(i)] }
    check(unplaced.empty?, "unplaced pictures drawn: #{unplaced.first(5)}")
    # Frames that redraw every picture: the screen's text changes whole.
    before = uploaded_bytes
    3.times do |k|
      program.send("\e[H\e[2J" + ("frame #{k} " * 300))
      sleep 0.4
    end
    after = uploaded_bytes
    check(before == after, "pictures on screen uploaded again: graphics_upload_bytes #{before} -> #{after}")
    check(GATE_BACKEND != 'gles' || before >= 500 * 8 * 8 * 3,
          "GLES uploaded #{before} bytes, less than the 500 pictures drawn")
    # The bytes reported are the pictures' own and their textures'.
    state = program.state
    pixels = state.fetch('graphics').sum { |g| g.fetch('bytes') }
    graphics = get('/v1/diagnostics').fetch('graphics')
    textures = graphics.dig('used', 'texture_bytes')
    check(pixels == 1000 * 8 * 8 * 3, "pictures hold #{pixels} bytes")
    check(GATE_BACKEND == 'gles' ? textures == 500 * 8 * 8 * 3 : textures.zero?,
          "texture cache holds #{textures} bytes on #{GATE_BACKEND}")
    check(state.dig('graphics_memory', 'bytes') == pixels + textures,
          "session reports #{state.dig('graphics_memory', 'bytes')}, pictures #{pixels} + textures #{textures}")
    check(graphics.dig('used', 'server_bytes') == pixels + textures,
          "server reports #{graphics.dig('used', 'server_bytes')} for one session of #{pixels + textures}")
    many = { pixels: pixels, textures: textures, uploaded: after }
  end

  # ---- session: a 1 MiB budget, pictures of 256 KiB. ----
  big = ->(i) { [i * 40 % 256, 90, 160, 255] }
  session_frees = []
  session_kept = nil
  with_server(File.join(root, 'session').tap { |d| Dir.mkdir(d) },
              { 'graphics_session_budget_mib' => 1 }) do
    check(get('/v1/diagnostics').dig('graphics', 'budget', 'session_bytes') == MIB, 'session budget setting not applied')
    program = Program.new(File.join(root, 'session'), 'session')
    # A, B, C, D unshown, then D and B shown in turn, each for a frame, so
    # least recently shown is A, C, D, B: not the order they came (oldest
    # first would free A, B, C, D).
    program.send((1..4).map { |i| picture("a=t,i=#{i},q=1", 256, 256, big.call(i)) }.join)
    wait_until('four pictures did not arrive') { program.ids == [1, 2, 3, 4] }
    [4, 2].each do |i|
      program.send("\e[2;2H" + apc("a=p,i=#{i},C=1,q=1"))
      sleep 0.5
      program.send(apc("a=d,d=i,i=#{i},q=1"))
      sleep 0.2
    end
    # Shown pictures arrive, one a frame, until one is refused.
    refused = nil
    (11..30).each do |i|
      program.send("\e[#{i - 8};#{i}H" + picture("a=T,i=#{i},C=1,q=1", 256, 256, big.call(i)))
      sleep 0.5
      if program.replies.include?("i=#{i};E")
        refused = i
        break
      end
    end
    check(refused, "no shown picture was refused: #{program.memory} ids=#{program.ids} replies=#{program.replies.inspect[0, 300]} freed=#{trace('image_freed').last(8)}")
    check(program.replies.include?("\e_Gi=#{refused};ENOSPC\e\\"),
          "refusal #{program.replies.inspect} is not ENOSPC")
    session_frees = trace('image_freed').grep(/reason=evicted-budget/).map { |l| l[/ i=(\d+)/, 1].to_i }
    check(session_frees == [1, 3, 4, 2], "freed #{session_frees}, not least recently shown first [1, 3, 4, 2]")
    memory = program.memory
    session_kept = program.ids
    check(session_kept == (11...refused).to_a, "left #{session_kept}, not the shown pictures 11..#{refused - 1}")
    check(memory['freed'] == { 'unshown' => 0, 'budget' => 4 } &&
          memory['refused'] == { 'ceiling' => 0, 'budget' => 1 },
          "frees and refusals reported #{memory.slice('freed', 'refused')}")
    check(memory['bytes'] <= MIB, "session holds #{memory['bytes']} bytes over its 1 MiB budget")
  end

  # ---- server: a 1 MiB server budget over two sessions. ----
  server_frees = []
  with_server(File.join(root, 'server').tap { |d| Dir.mkdir(d) },
              { 'graphics_server_budget_mib' => 1 }) do
    one = Program.new(File.join(root, 'server'), 'one', columns: 40, rows: 10)
    two = Program.new(File.join(root, 'server'), 'two', columns: 40, rows: 10)
    # Unshown A (one), C (two), B (one), in that order.
    one.send(picture('a=t,i=1,q=1', 256, 256, big.call(1)))
    wait_until('A did not arrive') { one.ids == [1] }
    two.send(picture('a=t,i=3,q=1', 256, 256, big.call(3)))
    wait_until('C did not arrive') { two.ids == [3] }
    one.send(picture('a=t,i=2,q=1', 256, 256, big.call(2)))
    wait_until('B did not arrive') { one.ids == [1, 2] }
    refused = nil
    (11..30).each do |i|
      two.send("\e[2;#{i - 9}H" + picture("a=T,i=#{i},C=1,q=1", 256, 256, big.call(i)))
      sleep 0.5
      if two.replies.include?("i=#{i};E")
        refused = i
        break
      end
    end
    check(refused, 'no shown picture was refused for the server budget')
    check(two.replies.include?("\e_Gi=#{refused};ENOSPC\e\\"), "refusal #{two.replies.inspect} is not ENOSPC")
    freed = trace('image_freed').grep(/reason=evicted-budget/)
    server_frees = freed.map { |l| [l[/session=session-(\d+)/, 1], l[/ i=(\d+)/, 1].to_i] }
    sessions = [one.id, two.id].map { |s| s.delete_prefix('session-') }
    # Least recently shown first across both sessions: A (one), C (two), B
    # (one). On the GPU compositor the shown pictures' textures count too,
    # so the refusal can come with B still kept: freeing it alone would not
    # have made room, and nothing is freed for nothing.
    order = [[sessions[0], 1], [sessions[1], 3], [sessions[0], 2]]
    check(server_frees.length >= 2 && server_frees == order.first(server_frees.length),
          "server budget freed #{server_frees}, not A, C, B across the sessions")
    graphics = get('/v1/diagnostics').fetch('graphics')
    left = one.ids.length * 256 * 256 * 4
    check(graphics.dig('used', 'server_bytes') + 256 * 256 * 4 - left > MIB,
          "refused although freeing the unshown pictures left would have made room: #{graphics['used']}")
    check(one.memory['freed']['budget'] + two.memory['freed']['budget'] == server_frees.length &&
          one.memory['freed']['budget'] >= 1 && two.memory['refused']['budget'] == 1,
          'the frees and the refusal are not each session\'s own')
  end

  # ---- ceiling: picture 4097. ----
  with_server(File.join(root, 'ceiling').tap { |d| Dir.mkdir(d) }, nil) do
    program = Program.new(File.join(root, 'ceiling'), 'ceiling', columns: 40, rows: 10)
    program.send((1..4096).map { |i| apc("a=T,f=24,s=1,v=1,i=#{i},C=1,q=1", '/wAA') }.join)
    wait_until('4096 pictures did not arrive', 120) { program.memory['images'] == 4096 }
    program.send(apc('a=T,f=24,s=1,v=1,i=4097,C=1,q=1', '/wAA'))
    wait_until('picture 4097 was not answered', 20) { program.replies.include?('i=4097;') }
    check(program.replies.include?("\e_Gi=4097;ENOSPC\e\\"), "picture 4097: #{program.replies[-40..].inspect}")
    memory = program.memory
    check(memory['images'] == 4096 && memory['placements'] == 4096 &&
          memory['refused'] == { 'ceiling' => 1, 'budget' => 0 } && memory['freed']['unshown'].zero?,
          "at the ceiling: #{memory}")
  end

  puts "GRAPHICS_MEMORY_PASS backend=#{GATE_BACKEND} many=1000 placed=500 drawn=500 " \
       "upload_bytes=#{many[:uploaded]}(flat) pixels=#{many[:pixels]} textures=#{many[:textures]} " \
       "session_freed=#{session_frees.join(',')}(least_recently_shown) kept=#{session_kept.length} refused=ENOSPC " \
       "server_freed=#{server_frees.map { |s, i| "#{s}:#{i}" }.join(',')} refused=ENOSPC " \
       'ceiling=4097:ENOSPC'
end
