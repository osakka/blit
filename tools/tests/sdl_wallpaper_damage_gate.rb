#!/usr/bin/env ruby
# frozen_string_literal: true

# Pixel regression gate for the live Hibr failure shape: a negative-z Kitty
# wallpaper must survive an incremental terminal damage repaint.

require 'base64'
require 'json'
require 'net/http'
require 'timeout'
require_relative 'gate_port'

port = gate_port(Integer(ENV.fetch('BLIT_WALLPAPER_DAMAGE_PORT', '19212')))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-wallpaper-damage-#{Process.pid}.log"
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'SDL_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def request(base, method, path, body = nil)
  klass = { get: Net::HTTP::Get, post: Net::HTTP::Post,
            put: Net::HTTP::Put, delete: Net::HTTP::Delete }.fetch(method)
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
  response.body.nil? || response.body.empty? ? nil : JSON.parse(response.body)
end

def screenshot(base)
  response = Net::HTTP.get_response(base + '/v1/diagnostics/screenshots/display-1')
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
  row = height - y - 1
  bmp.byteslice(offset + row * stride + x * bytes, bytes).bytes
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

id = nil
begin
  # 4x8 opaque red RGB image, placed over the first two-by-two cells and
  # explicitly below terminal text. It is large enough to sample the damaged
  # cell while keeping the PTY command below the bounded request size.
  rgb = ([255, 0, 0] * (4 * 8)).pack('C*')
  data = Base64.strict_encode64(rgb)
  kitty = "\\033_Ga=T,f=24,s=4,v=8,c=2,r=2,z=-1,i=77,m=0,q=2;#{data}\\033\\\\"
  delete = "\\033_Ga=d,d=I,i=77,q=2\\033\\\\"
  # Keep the child alive after the delete. A stopped session intentionally
  # retains its last protocol metadata for diagnostics, so checking that
  # metadata after exit cannot prove that the live scene was cleared.
  command = "printf '%b' \"$1\"; sleep 2; printf '%b' \"$2\"; printf 'DELETED\\n'; sleep 20"
  created = request(base, :post, '/v1/terminal-sessions',
                    'command' => '/bin/sh',
                    'arguments' => ['-c', command, 'sh', kitty, delete],
                    'surface' => {'width' => 20, 'height' => 8})
  id = created.fetch('id')

  Timeout.timeout(10) do
    loop do
      state = request(base, :get, "/v1/terminal-sessions/#{id}")
      break if state.fetch('graphics', []).any? do |image|
        image.fetch('id') == 77 && image.fetch('complete')
      end
      sleep 0.02
    end
  end
  sleep 0.15
  before = screenshot(base)
  inset = request(base, :get, '/v1/diagnostics').fetch('render').fetch('surface_inset_px')
  before_pixel = pixel(before, inset + 15, inset + 31)
  raise "wallpaper was not red before damage: #{before_pixel.inspect}" unless
    before_pixel[0, 3] == [0, 0, 255]
  baseline_surface_revision = request(
    base, :get, "/v1/terminal-sessions/#{id}").fetch('surface_revision')
  update = {'x' => 1, 'y' => 1, 'text' => 'X'}
  deferred = request(base, :put, "/v1/surfaces/surface-#{id}/cells",
          'base_revision' => baseline_surface_revision,
          'presentation' => 'defer', 'cells' => [update])
  request(base, :put, "/v1/surfaces/surface-#{id}/cells",
          'base_revision' => deferred.fetch('revision'),
          'presentation' => 'commit', 'cells' => [update])
  sleep 0.15
  after_pixel = pixel(screenshot(base), inset + 15, inset + 31)
  raise "wallpaper was lost after incremental damage: #{after_pixel.inspect}" unless
    after_pixel[0, 3] == [0, 0, 255]
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 10
  loop do
    state = request(base, :get, "/v1/terminal-sessions/#{id}")
    break if state.fetch('graphics', []).none? { |image| image.fetch('visible', false) }
    if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
      trace = request(base, :get,
                      '/v1/diagnostics/trace?kind=graphics_payload&limit=20')
      raise "wallpaper delete was not observed while session was alive: " \
            "graphics=#{state.fetch('graphics').inspect} trace=#{trace.inspect}"
    end
    sleep 0.05
  end
  sleep 0.15
  deleted_pixel = pixel(screenshot(base), inset + 15, inset + 31)
  raise "wallpaper remained after image delete: #{deleted_pixel.inspect}" unless
    deleted_pixel[0, 3] == [0, 0, 0]
  puts "SDL_WALLPAPER_DAMAGE_PASS before=#{before_pixel[0, 3].join(',')} " \
       "after=#{after_pixel[0, 3].join(',')} " \
       "deleted=#{deleted_pixel[0, 3].join(',')}"
ensure
  begin
    request(base, :delete, "/v1/terminal-sessions/#{id}") if id
  rescue StandardError
    nil
  end
end
