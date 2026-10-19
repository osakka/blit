#!/usr/bin/env ruby
# Bounded live font-size/geometry/capture probe. Does not switch renderers,
# kill applications, inject input, or claim desktop pixel correctness.
require 'json'
require 'net/http'
require 'tmpdir'
require 'fileutils'

base = URI(ENV.fetch('BLIT_LIVE','http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
sizes = ENV.fetch('BLIT_FONT_SIZES','17,19,23').split(',').map { |s| Integer(s,10) }
profile = ENV['BLIT_FONT_ID']
raise 'one to eight sizes, each 8..64 required' unless sizes.size.between?(1,8) && sizes.all? { |s| s.between?(8,64) }
out = ENV.fetch('BLIT_EVIDENCE_DIR') { Dir.mktmpdir('blit-font-size-') }
FileUtils.mkdir_p(out)
request = lambda do |method,path,payload=nil|
  q={'GET'=>Net::HTTP::Get,'PUT'=>Net::HTTP::Put}.fetch(method).new(base+path)
  q['Authorization']="Bearer #{token}";q['Content-Type']='application/json'
  q.body=JSON.generate(payload) if payload
  r=Net::HTTP.start(base.host,base.port,open_timeout:3,read_timeout:15) { |h| h.request(q) }
  raise "#{method} #{path}: HTTP #{r.code}" unless r.code.start_with?('2')
  r
end
get = ->(path) { JSON.parse(request.call('GET',path).body) }
put = ->(payload) { JSON.parse(request.call('PUT','/v1/fonts',payload).body) }
baseline = get.call('/v1/diagnostics/observation')
font = baseline.fetch('fonts').fetch('active')
if profile
  raise 'requested font profile is unavailable' unless
    baseline.fetch('fonts').fetch('profiles').any? { |p| p['id']==profile && p['available'] }
end
identity = ->(active) { active.reject { |key,_| key == 'raster_cache' } }
raise 'pixel font API not installed' unless font.key?('sizing') && font.key?('size_px')
backend = baseline.dig('diagnostics','health','backend')
renderer = baseline.dig('diagnostics','render','renderer')
regions = baseline.fetch('topology').fetch('panes').to_h { |p| [p['id'],p.fetch('pixel_region')] }
restore = font['sizing']=='pixels' ? {'size_px'=>font['size_px']} : {'scale'=>font['scale']}
restore['font_id'] = font.fetch('id')
last = nil
begin
  last = put.call({'font_id'=>profile}).fetch('active') if profile
  sizes.each do |size|
    if last
      current = get.call('/v1/fonts').fetch('active')
      raise 'font changed concurrently; refusing another size write' unless identity.call(current)==identity.call(last)
    end
    applied = put.call({'size_px'=>size}).fetch('active');last=applied
    raise 'exact pixel height not applied' unless applied['sizing']=='pixels' && applied['size_px']==size
    raise 'requested profile not retained' if profile && applied['id']!=profile
    observation = get.call('/v1/diagnostics/observation')
    raise 'presentation identity changed' unless observation.dig('diagnostics','health','backend')==backend &&
      observation.dig('diagnostics','render','renderer')==renderer
    observation.fetch('topology').fetch('panes').each do |pane|
      region = pane.fetch('pixel_region');grid = pane.fetch('cell_grid')
      raise 'physical pane split changed' unless region==regions.fetch(pane['id'])
      raise 'pane cell metrics differ from font' unless grid['cell_width']==applied.dig('cell_pixels','width') && grid['cell_height']==size
      raise 'incomplete or overflowing cell grid' unless grid['columns']>0 && grid['rows']>0 &&
        grid['origin_x']>=region['x'] && grid['origin_y']>=region['y'] &&
        grid['origin_x']+grid['pixel_width']<=region['x']+region['width'] &&
        grid['origin_y']+grid['pixel_height']<=region['y']+region['height']
    end
    observation.fetch('displays').fetch('displays').each do |display|
      capture = request.call('GET',"/v1/diagnostics/screenshots/#{display.fetch('id')}")
      raise 'capture is not BMP' unless capture.body.start_with?('BM')
      File.binwrite(File.join(out,"size-#{size}-#{display.fetch('id')}.bmp"),capture.body)
    end
    File.write(File.join(out,"size-#{size}.json"),JSON.pretty_generate(observation))
    puts "FONT_SIZE_GEOMETRY_PASS font=#{applied['id']} raster=#{applied['rasterization']} size_px=#{size} cells=#{applied['cell_pixels']} panes=#{regions.size} renderer=#{renderer}"
    $stdout.flush
  end
ensure
  if last
    current = get.call('/v1/fonts').fetch('active')
    raise 'font changed concurrently; refusing to overwrite operator choice' unless identity.call(current)==identity.call(last)
    restored = put.call(restore).fetch('active')
    raise 'font restoration mismatch' unless identity.call(restored)==identity.call(font)
    File.write(File.join(out,'restored.json'),JSON.pretty_generate(restored))
    puts "FONT_SIZE_BASELINE_RESTORED evidence=#{out}"
  end
end
puts 'FONT_SIZE_CAPTURE_PASS scope=geometry-and-capture-not-desktop-pixel-acceptance'
