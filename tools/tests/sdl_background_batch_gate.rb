#!/usr/bin/env ruby
# frozen_string_literal: true

# Prove that truecolor terminal backgrounds remain pixel-correct while the
# SDL renderer uses its batched background path for wallpaper-like surfaces.

require 'json'
require 'net/http'
require 'timeout'

port = Integer(ENV.fetch('BLIT_BACKGROUND_PORT', '19263'))
base = URI("http://127.0.0.1:#{port}")
log = "/tmp/blit-background-batch-#{Process.pid}.log"
env = {
  'SDL_VIDEODRIVER' => 'dummy',
  'BLIT_RENDERER' => 'software',
  'LD_LIBRARY_PATH' => 'build/sdl3-kms'
}
pid = Process.spawn(env, 'build/blit/blit-server', port.to_s,
                    out: log, err: [:child, :out])

at_exit do
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

def request(base, method, path, body = nil)
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post }.fetch(method)
  uri = base + path
  Net::HTTP.start(uri.hostname, uri.port, open_timeout: 2,
                  read_timeout: 3) do |http|
    req = klass.new(uri)
    if body
      req['Content-Type'] = 'application/json'
      req.body = JSON.generate(body)
    end
    response = http.request(req)
    raise "#{method} #{path}: HTTP #{response.code}: #{response.body}" unless
      response.code.start_with?('2')
    response.body.nil? || response.body.empty? ? nil : JSON.parse(response.body)
  end
end

Timeout.timeout(8) do
  loop do
    begin
      request(base, 'GET', '/v1/server')
      break
    rescue StandardError
      sleep 0.05
    end
  end
end

# Fill a 128x48 terminal surface with a truecolor background. The repeated
# cell workload is intentionally close to the largest ordinary SDL pane.
command = %q(i=0; while [ $i -lt 6144 ]; do printf '\033[48;2;255;0;0mX'; i=$((i+1)); done; printf '\033[0m')
session = request(base, 'POST', '/v1/terminal-sessions',
                  'command' => '/bin/sh',
                  'arguments' => ['-lc', command],
                  'surface' => { 'width' => 128, 'height' => 48 })

Timeout.timeout(8) do
  loop do
    break if request(base, 'GET', '/v1/diagnostics/runtime')
                 .fetch('performance').fetch('background_quads') > 0
    sleep 0.05
  end
end

before = request(base, 'GET', '/v1/diagnostics/runtime')
bmp = Net::HTTP.get(base + '/v1/diagnostics/screenshots/display-1')
File.binwrite("/tmp/blit-background-batch-#{Process.pid}.bmp", bmp)

offset = bmp.byteslice(10, 4).unpack1('V')
width = bmp.byteslice(18, 4).unpack1('l<')
height = bmp.byteslice(22, 4).unpack1('l<')
bits = bmp.byteslice(28, 2).unpack1('v')
raise 'unexpected screenshot format' unless bits == 24 || bits == 32
bytes_per_pixel = bits / 8
stride = ((width * bytes_per_pixel + 3) / 4) * 4
pixel = offset + (height - 1 - 5) * stride + 5 * bytes_per_pixel
bgr = bmp.byteslice(pixel, 3).bytes
raise "background pixel is not red: #{bgr.inspect}" unless bgr == [0, 0, 255]

performance = before.fetch('performance')
raise 'background batch submission counter did not advance' unless
  performance.fetch('background_batches') > 0
raise "background runs were not coalesced: #{performance.fetch('background_quads')}" unless
  performance.fetch('background_quads') <= 48
puts "SDL_BACKGROUND_BATCH_PASS batches=#{performance.fetch('background_batches')} " \
     "quads=#{performance.fetch('background_quads')} render_ms=#{performance.fetch('last_render_ms')}"
