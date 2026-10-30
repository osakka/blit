#!/usr/bin/env ruby
# Read-only direct-KMS framebuffer oracle. Canonical art, not a second drawing
# implementation. Opaque pixels need no inference about the desktop behind.
require 'json'
require 'net/http'
require 'tmpdir'
require 'fileutils'

ROOT=File.expand_path('../..',__dir__)
BASE=URI(ENV.fetch('BLIT_LIVE','http://192.168.10.54:8080'))
TOKEN=ENV.fetch('BLIT_AUTH_TOKEN')
def get(path, raw:false)
  q=Net::HTTP::Get.new(BASE+path);q['Authorization']="Bearer #{TOKEN}"
  r=Net::HTTP.start(BASE.host,BASE.port,open_timeout:3,read_timeout:20){|h|h.request(q)}
  abort("POINTER_HOST_FAIL #{path}:HTTP#{r.code}") unless r.code=='200'
  raw ? r.body : JSON.parse(r.body)
end
def skip(reason)
  puts "POINTER_HOST_SKIP #{reason}";exit 77
end
before=get('/v1/diagnostics/observation')
skip('requires_direct_kms') unless before.dig('server','display_backend','name')=='drm-kms'
pointer=before.dig('diagnostics','render','pointer')
appearance=before.dig('settings','effective','pointer')
skip('pointer_or_appearance_unavailable') unless pointer && pointer['valid'] && appearance
skip('pointer_not_yet_composed') if pointer['dirty']
output=before.dig('diagnostics','render','outputs',pointer['output'])
display=before.dig('displays','displays').find{|d|d['id']==output['display_id']}
skip('requires_native_size') unless display &&
  output['render_width']==display.dig('bounds','width') &&
  output['render_height']==display.dig('bounds','height')
shape=%w[arrow hand precision].index(appearance['shape'])
palette=%w[pearl obsidian neon amber].index(appearance['palette'])
abort('POINTER_HOST_FAIL unknown_art') unless shape && palette

Dir.mktmpdir('blit-host-pointer') do |root|
  helper=File.join(root,'art');stamp=File.join(root,'stamp')
  abort('POINTER_HOST_FAIL compiler') unless system('tcc','-O2',File.join(ROOT,'tools/tests/pointer_art_gate.c'),'-o',helper)
  abort('POINTER_HOST_FAIL stamp') unless system(helper,'--stamp',shape.to_s,palette.to_s,appearance['size_px'].to_s,stamp)
  raw=File.binread(stamp);w,h,hx,hy=raw.unpack('L<4');rgba=raw.byteslice(16..)
  bmp=get("/v1/diagnostics/screenshots/#{display['id']}?cached=1",raw:true)
  after=get('/v1/diagnostics/observation')
  p2=after.dig('diagnostics','render','pointer')
  skip('pointer_or_configuration_changed_during_capture') unless
    p2 && %w[valid x y output].all?{|k|p2[k]==pointer[k]} &&
    appearance==after.dig('settings','effective','pointer') &&
    before['version']==after['version']
  fw,fh=bmp.byteslice(18,8).unpack('l<2');bpp=bmp.byteslice(28,2).unpack1('S<')/8
  abort('POINTER_HOST_FAIL unsupported_capture') unless bmp.start_with?('BM') && [3,4].include?(bpp) && fw==output['render_width'] && fh==output['render_height']
  stride=(fw*bpp+3)&~3;offset=bmp.byteslice(10,4).unpack1('L<');count=0
  h.times do |y|;w.times do |x|
    i=(y*w+x)*4;next unless rgba.getbyte(i+3)==255
    px=pointer['x'].to_i-display.dig('bounds','x')-hx+x
    py=pointer['y'].to_i-display.dig('bounds','y')-hy+y
    next unless px.between?(0,fw-1) && py.between?(0,fh-1)
    j=offset+(fh-1-py)*stride+px*bpp
    actual=[bmp.getbyte(j+2),bmp.getbyte(j+1),bmp.getbyte(j)]
    expected=3.times.map{|c|rgba.getbyte(i+c)}
    abort("POINTER_HOST_FAIL pixel=#{px},#{py} actual=#{actual} expected=#{expected}") unless actual==expected
    count+=1
  end;end
  skip('insufficient_visible_opaque_pixels') if count<4
  verdict={result:'pass',version:before['version'],display:display['id'],appearance:appearance,
    opaque_pixels:count,hotspot:'canonical',mutations:0,
    boundary:'API framebuffer, not glass or physical input-to-photon latency'}
  if (directory=ENV['BLIT_EVIDENCE_DIR'])
    FileUtils.mkdir_p(directory)
    File.binwrite(File.join(directory,'pointer-frame.bmp'),bmp)
    File.write(File.join(directory,'pointer-frame.json'),JSON.pretty_generate(verdict))
  end
  puts "POINTER_HOST_PASS #{JSON.generate(verdict)}"
end
