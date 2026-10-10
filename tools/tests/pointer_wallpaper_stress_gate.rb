#!/usr/bin/env ruby
# frozen_string_literal: true

# Regression gate for the live Hibr failure shape: a large, opaque, colourful
# terminal surface is changing while the pointer moves.  The gate checks the
# public screenshot boundary for black holes outside the changed cell and
# requires both pointer-cache and pointer-plus-damage incremental paths.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'

port = gate_port(Integer(ENV.fetch('BLIT_WALLPAPER_POINTER_PORT', '19213')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-wallpaper-pointer-#{Process.pid}.log"
pid = Process.spawn({'SDL_VIDEODRIVER' => 'dummy',
                     'BLIT_RENDERER' => 'software',
                     'LD_LIBRARY_PATH' => 'build/sdl3-kms'},
                    'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

def request(base, method, path, body = nil)
  klass = {get: Net::HTTP::Get, post: Net::HTTP::Post,
           put: Net::HTTP::Put, delete: Net::HTTP::Delete}.fetch(method)
  uri = base + path
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                             read_timeout: 10) do |http|
    req = klass.new(uri)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    http.request(req)
  end
  raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
    response.code.start_with?('2')
  response.body.empty? ? nil : JSON.parse(response.body)
end

def screenshot(base)
  uri = base + '/v1/diagnostics/screenshots/display-1'
  response = Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                             read_timeout: 10) do |http|
    http.request(Net::HTTP::Get.new(uri))
  end
  raise "screenshot HTTP #{response.code}" unless response.is_a?(Net::HTTPSuccess)
  response.body
end

def pixel(bmp, x, y)
  raise 'not a BMP' unless bmp.byteslice(0, 2) == 'BM'
  offset = bmp.byteslice(10, 4).unpack1('V')
  width = bmp.byteslice(18, 4).unpack1('l<')
  height = bmp.byteslice(22, 4).unpack1('l<')
  bits = bmp.byteslice(28, 2).unpack1('v')
  raise "unsupported BMP depth #{bits}" unless [24, 32].include?(bits)
  bytes = bits / 8
  stride = ((width * bytes + 3) / 4) * 4
  raise "sample outside #{width}x#{height}" if x >= width || y >= height
  row = height - y - 1
  bmp.byteslice(offset + row * stride + x * bytes, bytes).bytes
end

begin
  at_exit do
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
  Timeout.timeout(8) do
    loop do
      begin
        request(base, :get, '/v1/version')
        break
      rescue StandardError
        sleep 0.05
      end
    end
  end

  width = 120
  height = 40
  palette = ['#14324a', '#4a2638', '#254a32', '#4a4220']
  cells = (0...height).flat_map do |y|
    (0...width).map do |x|
      {'x' => x, 'y' => y, 'text' => ' ',
       'style' => {'foreground' => '#ffffff',
                   'background' => palette[(x / 30 + y / 10) % palette.length]}}
    end
  end
  session = request(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh', 'arguments' => ['-c', 'sleep 12'],
                    'surface_mode' => 'external',
                    'surface' => {'width' => width, 'height' => height})
  id = session.fetch('id')
  path = "/v1/surfaces/surface-#{id}/cells"
  # The API request envelope is deliberately bounded.  Exercise the same
  # chunked frame contract used by Hibr instead of hiding an oversized request
  # behind a test-only limit.
  revision = 1
  chunks = cells.each_slice(32).to_a
  chunks.each_with_index do |chunk, chunk_index|
    mode = chunk_index + 1 == chunks.length ? 'commit' : 'defer'
    revision = request(base, :put, path,
                       'base_revision' => revision, 'presentation' => mode,
                       'cells' => chunk).fetch('revision')
  end
  sleep 0.15

  # These samples are deliberately away from the update cell and all pointer
  # locations.  A black value means the composed scene was partially cleared.
  samples = [[80, 80], [400, 80], [800, 80], [80, 400], [400, 400], [800, 600]]
  points = [[600, 500], [700, 500], [600, 600], [700, 600],
            [500, 500], [750, 550], [550, 650], [850, 650]]
  pointer_cache_before = request(base, :get, '/v1/diagnostics/runtime')
                                  .fetch('performance').fetch('pointer_cache_hits')
  samples.each_with_index do |(x, y), index|
    pointer_x, pointer_y = points[index]
    update = {'base_revision' => revision, 'presentation' => 'defer',
              'cells' => [{'x' => index + 1, 'y' => 1, 'text' => ' ',
                           'style' => {'foreground' => '#ffffff',
                                       'background' => palette[(index + 1) % palette.length]}}]}
    revision = request(base, :put, path, update).fetch('revision')
    pointer = Thread.new do
      request(base, :post, '/v1/input/inject',
              'kind' => 'pointer_motion', 'x' => pointer_x, 'y' => pointer_y)
    end
    commit = Thread.new do
      request(base, :put, path,
              'base_revision' => revision, 'presentation' => 'commit',
              'cells' => update.fetch('cells'))
    end
    pointer.join
    revision = commit.value.fetch('revision')
    sleep 0.04
    bmp = screenshot(base)
    values = samples.map { |sample_x, sample_y| pixel(bmp, sample_x, sample_y) }
    bad = values.each_with_index.select { |value, _| value[0, 3] == [0, 0, 0] }
    raise "black scene hole at #{bad.inspect}" unless bad.empty?
  end

  runtime = request(base, :get, '/v1/diagnostics/runtime')
  # Isolate the pointer-only path once the damage stress has completed.  The
  # concurrent iterations must update only the committed damage boundary.
  request(base, :post, '/v1/input/inject',
          'kind' => 'pointer_motion', 'x' => 900, 'y' => 650)
  sleep 0.05
  runtime = request(base, :get, '/v1/diagnostics/runtime')
  performance = runtime.fetch('performance')
  trace = request(base, :get, '/v1/diagnostics/trace?kind=render_path&limit=128')
  details = trace.fetch('events').map { |event| event.fetch('detail') }
  pointer_cache = performance.fetch('pointer_cache_hits') - pointer_cache_before
  race = details.any? do |detail|
    detail.include?('path=incremental-damage') &&
      detail.include?('target_changed=1') &&
      detail.include?('pointer_dirty=1')
  end
  raise 'pointer-cache path was not observed' unless pointer_cache > 0
  raise 'pointer-plus-damage incremental boundary was not observed' unless race
  puts "POINTER_WALLPAPER_STRESS_PASS samples=#{samples.length} " \
       "pointer_cache_hits=#{pointer_cache}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
