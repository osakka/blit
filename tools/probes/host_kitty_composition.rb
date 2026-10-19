#!/usr/bin/env ruby
# frozen_string_literal: true

# Live direct-KMS Kitty composition probe.
#
# This is deliberately independent of Hibr. It creates one API-owned terminal
# in a free pane, paints a known cell background, places a 2x2 RGB Kitty image
# at the origin, captures the physical framebuffer, and then deletes the image
# and captures again. The test checks both composition and invalidation:
# image quadrants must be exact, untouched cells must stay exact, and deleted
# image pixels must return to the cell background.

require 'base64'
require 'json'
require 'net/http'
require 'securerandom'
require 'uri'

host = ARGV.fetch(0, '192.168.10.54:8080')
token = ENV.fetch('BLIT_AUTH_TOKEN') { ARGV.fetch(1, 'hellomonkeyboy') }
base = URI("http://#{host}")
image_id = Integer(ENV.fetch('BLIT_KITTY_IMAGE_ID', '9001'), 10)

def request(base, token, method, path, body = nil)
  uri = base + path
  klass = {
    'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
    'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete
  }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{token}"
  if body
    req['Content-Type'] = 'application/json'
    req['Idempotency-Key'] = "live-kitty-#{SecureRandom.hex(8)}"
    req.body = JSON.dump(body)
  end
  Net::HTTP.start(uri.hostname, uri.port, read_timeout: 120) do |http|
    http.request(req)
  end
end

def json(base, token, path)
  response = request(base, token, 'GET', path)
  abort "GET #{path} -> #{response.code} #{response.body}" unless response.code == '200'
  JSON.parse(response.body)
end

def fail_request(message, response)
  abort "#{message}: HTTP #{response.code} #{response.body}"
end

def capture(base, token, display_id)
  response = request(base, token, 'GET',
                     "/v1/diagnostics/screenshots/#{display_id}")
  fail_request('screenshot failed', response) unless response.code == '200'
  revisions = {}
  (response['X-Blit-Capture-Revisions'] || '').split(',').each do |pair|
    id, revision = pair.split('=')
    revisions[id] = Integer(revision, 10) if id && revision
  end
  {
    body: response.body,
    revisions: revisions,
    frame: response['X-Blit-Capture-Frame'],
    presented: response['X-Blit-Capture-Presented']
  }
end

def pixel(bmp, width, height, x, y)
  bits = bmp.getbyte(28) | (bmp.getbyte(29) << 8)
  bytes = bits / 8
  raise "unsupported BMP depth #{bits}" unless [3, 4].include?(bytes)
  stride = (width * bytes + 3) & ~3
  stored_width, stored_height = bmp.byteslice(18, 8).unpack('l<2')
  raise 'capture geometry mismatch' unless stored_width == width && stored_height.abs == height
  row = stored_height > 0 ? height - 1 - y : y
  offset = bmp.byteslice(10, 4).unpack1('V') + row * stride + x * bytes
  [bmp.getbyte(offset + 2), bmp.getbyte(offset + 1), bmp.getbyte(offset)]
end

def coherent_capture(base, token, display_id, surface_id, direct_kms)
  12.times do
    shot = capture(base, token, display_id)
    state = json(base, token, "/v1/surfaces/#{surface_id}/summary")
    reported = shot[:revisions][surface_id]
    presented_ok = shot[:presented] == 'true' ||
                   (direct_kms && shot[:presented].nil?)
    return [shot, state] if reported && reported == state['revision'] && presented_ok
  end
  abort 'no coherent direct-KMS capture for the graphics composition check'
end

topology = json(base, token, '/v1/topology')
display_id = topology.fetch('outputs').first.fetch('display_id')
display = json(base, token, '/v1/displays').fetch('displays')
                       .find { |item| item['id'] == display_id }
abort "display #{display_id} missing" unless display
width = display.fetch('bounds').fetch('width')
height = display.fetch('bounds').fetch('height')
diagnostics = json(base, token, '/v1/diagnostics')
direct_kms = diagnostics.dig('render', 'direct_kms') == true
inset = diagnostics.fetch('render').fetch('surface_inset_px')
pane = topology.fetch('panes').find { |item| item['surface_id'].nil? }
abort 'no free pane for live Kitty composition probe' unless pane
grid = pane.fetch('cell_grid')
columns = [24, grid.fetch('columns')].min
rows = [10, grid.fetch('rows')].min
# The topology's cell-grid origin is already in display coordinates. The
# pixel_region is a containing rectangle, not another offset to add.
surface_origin_x = grid.fetch('origin_x') + inset
surface_origin_y = grid.fetch('origin_y') + inset
cell_w = grid.fetch('cell_width')
cell_h = grid.fetch('cell_height')
background = [0x10, 0x20, 0x30]
background_hex = '#102030'
surface = nil
session = nil

begin
  # Terminal input is PTY input, not terminal output: writing an escape to
  # `/input` would make a shell consume it.  Emit the protocol from the child
  # itself so the terminal parser receives it exactly as Hibr would.
  rgb = [255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0].pack('C*')
  payload = Base64.strict_encode64(rgb)
  kitty = "\e[?25l\e[1;1H\e_Ga=T,f=24,s=2,v=2,c=1,r=1,i=#{image_id},m=0,C=1,q=2;#{payload}\e\\"
  deleted_kitty = "\e_Ga=d,d=I,i=#{image_id},q=2\e\\"
  created = request(base, token, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c',
                    'printf "%s" "$1"; sleep 5; printf "%s" "$2"; ' \
                    'sleep 30',
                    'blit-kitty-composition', kitty, deleted_kitty],
    'pane_id' => pane.fetch('id')
  })
  fail_request('session create failed', created) unless created.code == '201'
  session_json = JSON.parse(created.body)
  session = session_json.fetch('id')
  surface = session_json.fetch('surface_id')

  blanket = []
  (0...rows).each do |y|
    (0...columns).each do |x|
      blanket << {
        'x' => x, 'y' => y, 'text' => ' ',
        'style' => {'foreground' => '#ffffff', 'background' => background_hex}
      }
    end
  end
  blanket.each_slice(40) do |chunk|
    summary = json(base, token, "/v1/surfaces/#{surface}/summary")
    painted = request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells",
                      {'base_revision' => summary.fetch('revision'), 'cells' => chunk})
    fail_request('background paint failed', painted) unless %w[200 204].include?(painted.code)
  end

  deadline = Time.now + 10
  image = nil
  while Time.now < deadline
    state = json(base, token, "/v1/terminal-sessions/#{session}")
    image = state.fetch('graphics').find { |item| item['id'] == image_id }
    break if image && image['complete'] && image['visible']
    sleep 0.1
  end
  abort "Kitty image did not become visible: #{image.inspect}" unless image
  abort "unexpected image geometry: #{image.inspect}" unless
    image['width'] == 2 && image['height'] == 2 && image['format'] == 24 &&
    image['placement_width'] == cell_w && image['placement_height'] == cell_h

  shot, state = coherent_capture(base, token, display_id, surface, direct_kms)
  if (directory = ENV['BLIT_EVIDENCE_DIR'])
    require 'fileutils'
    FileUtils.mkdir_p(directory)
    File.binwrite(File.join(directory, 'kitty-visible.bmp'), shot[:body])
    File.write(File.join(directory, 'kitty-visible.json'), JSON.pretty_generate({capture: shot.reject { |key, _| key == :body }, surface: state}))
  end
  ix = surface_origin_x + image.fetch('x')
  iy = surface_origin_y + image.fetch('y')
  samples = {
    red: pixel(shot[:body], width, height, ix + cell_w / 4, iy + cell_h / 4),
    green: pixel(shot[:body], width, height, ix + (cell_w * 3) / 4, iy + cell_h / 4),
    blue: pixel(shot[:body], width, height, ix + cell_w / 4, iy + (cell_h * 3) / 4),
    yellow: pixel(shot[:body], width, height,
                  ix + (cell_w * 3) / 4, iy + (cell_h * 3) / 4),
    blank: pixel(shot[:body], width, height,
                 surface_origin_x + 2 * cell_w + cell_w / 2,
                 surface_origin_y + 2 * cell_h + cell_h / 2)
  }
  expected = {red: [255, 0, 0], green: [0, 255, 0],
              blue: [0, 0, 255], yellow: [255, 255, 0], blank: background}
  puts "composition sample image=#{image.inspect} samples=#{samples.inspect}"
  abort "composition mismatch samples=#{samples.inspect}" unless samples == expected

  deadline = Time.now + 10
  while Time.now < deadline
    state = json(base, token, "/v1/terminal-sessions/#{session}")
    break unless state.fetch('graphics').any? { |item| item['id'] == image_id && item['visible'] }
    sleep 0.1
  end
  shot, = coherent_capture(base, token, display_id, surface, direct_kms)
  File.binwrite(File.join(directory, 'kitty-deleted.bmp'), shot[:body]) if directory
  restored = pixel(shot[:body], width, height,
                   ix + cell_w / 2, iy + cell_h / 2)
  abort "deleted image left pixels behind: #{restored.inspect}" unless restored == background

  puts "KITTY_COMPOSITION_PASS display=#{display_id} image=#{image_id} " \
       "direct_kms=#{direct_kms} pixels=exact delete=exact frame=#{shot[:frame]}"
ensure
  if session
    deleted = request(base, token, 'DELETE', "/v1/terminal-sessions/#{session}")
    warn "cleanup failed: HTTP #{deleted.code} #{deleted.body}" unless
      %w[200 204].include?(deleted.code)
  end
end
