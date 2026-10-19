#!/usr/bin/env ruby
# frozen_string_literal: true

# Live direct-KMS motion agreement probe.
#
# It creates one API-owned surface in a free pane, paints a known background,
# moves a small block through that surface, and accepts a screenshot only when
# its capture revision matches the cell revision read afterward. Blank cells
# must remain solid background pixels. This is the live counterpart to the
# local motion-trail gate and is deliberately independent of Hibr.

require 'json'
require 'net/http'
require 'securerandom'
require 'uri'

host = ARGV.fetch(0, '192.168.10.54:8080')
token = ENV.fetch('BLIT_AUTH_TOKEN') { ARGV.fetch(1, 'hellomonkeyboy') }
steps = Integer(ARGV.fetch(2, '12'), 10)
attempts = Integer(ENV.fetch('BLIT_LIVE_MOTION_ATTEMPTS', '8'), 10)
base = URI("http://#{host}")

def request(base, token, method, path, body = nil)
  uri = base + path
  klass = { 'GET' => Net::HTTP::Get, 'POST' => Net::HTTP::Post,
            'PUT' => Net::HTTP::Put, 'DELETE' => Net::HTTP::Delete }.fetch(method)
  req = klass.new(uri)
  req['Authorization'] = "Bearer #{token}"
  if body
    req['Content-Type'] = 'application/json'
    req['Idempotency-Key'] = "live-motion-#{SecureRandom.hex(8)}"
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

def check_blank_pixels(capture, state, origin_x, origin_y, cell_w, cell_h,
                       width, height, background, rows, columns)
  grid = {}
  state.fetch('cells').each { |cell| grid[[cell.fetch('x'), cell.fetch('y')]] = cell }
  cursor = state.fetch('cursor', {})
  cursor_cell = [cursor.fetch('x', -1), cursor.fetch('y', -1)]
  mismatches = []
  (0...rows).each do |y|
    (0...columns).each do |x|
      cell = grid[[x, y]]
      next unless cell && (cell['text'] || ' ') == ' '
      next unless cell.fetch('style', {}).fetch('background', nil)
      next if [x, y] == cursor_cell
      bad = 0
      (0...cell_h).each do |dy|
        (0...cell_w).each do |dx|
          px = origin_x + x * cell_w + dx
          py = origin_y + y * cell_h + dy
          next if px >= width || py >= height
          bad += 1 if pixel(capture[:body], width, height, px, py) != background
        end
      end
      mismatches << [x, y, bad] if bad.positive?
    end
  end
  mismatches
end

topology = json(base, token, '/v1/topology')
output = topology.fetch('outputs').first
display_id = output.fetch('display_id')
display = json(base, token, '/v1/displays').fetch('displays')
                       .find { |item| item['id'] == display_id }
abort "display #{display_id} missing" unless display
width = display.fetch('bounds').fetch('width')
height = display.fetch('bounds').fetch('height')
diagnostics = json(base, token, '/v1/diagnostics')
inset = diagnostics.fetch('render').fetch('surface_inset_px')
direct_kms = diagnostics.fetch('render').fetch('direct_kms') == true
pane = topology.fetch('panes').find { |item| item['surface_id'].nil? }
abort 'no free pane for live motion probe' unless pane
grid = pane.fetch('cell_grid')
columns = [30, grid.fetch('columns')].min
rows = [16, grid.fetch('rows')].min
# The topology's cell-grid origin is already in display coordinates. The
# pixel_region is a containing rectangle, not another offset to add.
origin_x = grid.fetch('origin_x') + inset
origin_y = grid.fetch('origin_y') + inset
cell_w = grid.fetch('cell_width')
cell_h = grid.fetch('cell_height')
background = [0x10, 0x20, 0x30]
background_hex = '#102030'
surface = nil
session = nil
accepted = 0
discarded = 0
failures = []

begin
  created = request(base, token, 'POST', '/v1/terminal-sessions', {
    'command' => '/bin/sh',
    'arguments' => ['-c', 'trap : TERM; while :; do sleep 1; done'],
    'pane_id' => pane.fetch('id')
  })
  fail_request('session create failed', created) unless created.code == '201'
  created_json = JSON.parse(created.body)
  session = created_json.fetch('id')
  surface = created_json.fetch('surface_id')

  blanket = []
  (0...rows).each do |y|
    (0...columns).each do |x|
      blanket << {
        'x' => x, 'y' => y, 'text' => ' ',
        'style' => {'foreground' => '#ffffff', 'background' => background_hex}
      }
    end
  end
  # The live HTTP request buffer is intentionally bounded. Keep the initial
  # full-region paint in the same small chunks as the local motion gate.
  blanket.each_slice(50) do |chunk|
    initial = json(base, token, "/v1/surfaces/#{surface}/summary")
    painted = request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells",
                      {'base_revision' => initial.fetch('revision'),
                       'cells' => chunk})
    fail_request('initial paint failed', painted) unless %w[200 204].include?(painted.code)
  end

  previous = nil
  steps.times do |step|
    updates = []
    if previous
      (0...4).each do |x|
        (0...2).each do |y|
          updates << {'x' => previous + x, 'y' => 2 + y, 'text' => ' ',
                      'style' => {'foreground' => '#ffffff',
                                  'background' => background_hex}}
        end
      end
    end
    current = step + 1
    (0...4).each do |x|
      (0...2).each do |y|
        updates << {'x' => current + x, 'y' => 2 + y, 'text' => 'O',
                    'style' => {'foreground' => '#000000',
                                'background' => '#d0a040'}}
      end
    end
    summary = json(base, token, "/v1/surfaces/#{surface}/summary")
    update = request(base, token, 'PUT', "/v1/surfaces/#{surface}/cells",
                     {'base_revision' => summary.fetch('revision'),
                      'cells' => updates})
    fail_request("motion update #{step} failed", update) unless
      %w[200 204].include?(update.code)

    used = nil
    attempts.times do
      shot = capture(base, token, display_id)
      state = json(base, token, "/v1/surfaces/#{surface}/cells")
      reported = shot[:revisions][surface]
      presented_ok = shot[:presented] == 'true' || (direct_kms && shot[:presented].nil?)
      if reported.nil? || !presented_ok || reported != state.fetch('revision')
        discarded += 1
        next
      end
      used = [shot, state, reported]
      break
    end
    if used.nil?
      failures << {step: step, reason: 'no coherent capture', attempts: attempts}
    else
      shot, state, revision = used
      if (directory = ENV['BLIT_EVIDENCE_DIR']) && (step == 0 || step == steps - 1)
        require 'fileutils'
        FileUtils.mkdir_p(directory)
        name = File.join(directory, format('motion-%02d', step))
        File.binwrite(name + '.bmp', shot[:body])
        File.write(name + '.json', JSON.pretty_generate({capture: shot.reject { |key, _| key == :body }, surface: state}))
      end
      mismatches = check_blank_pixels(shot, state, origin_x, origin_y, cell_w,
                                      cell_h, width, height, background, rows,
                                      columns)
      if mismatches.empty?
        accepted += 1
      else
        failures << {step: step, reason: 'unaccounted pixels',
                     revision: revision, sample: mismatches.first(8)}
      end
    end
    previous = current
  end
ensure
  if session
    deleted = request(base, token, 'DELETE', "/v1/terminal-sessions/#{session}")
    warn "cleanup failed: HTTP #{deleted.code} #{deleted.body}" unless
      %w[200 204].include?(deleted.code)
  end
end

if failures.empty?
  puts "MOTION_PASS steps=#{steps} accepted=#{accepted} discarded=#{discarded} " \
       "direct_kms=#{direct_kms} display=#{display_id}"
  exit 0
end

puts "MOTION_FAIL steps=#{steps} accepted=#{accepted} discarded=#{discarded} " \
     "failures=#{failures.inspect} direct_kms=#{direct_kms} display=#{display_id}"
exit 1
