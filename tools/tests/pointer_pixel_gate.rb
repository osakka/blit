#!/usr/bin/env ruby
# Shared vector source vs actual SDL framebuffer, including rotated hotspots.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'open3'
require_relative 'gate_port'
require_relative 'gate_server'
ROOT=File.expand_path('../..',__dir__)
PORT=gate_port(Integer(ENV.fetch('BLIT_GATE_PORT','19522')))
BASE=URI("http://127.0.0.1:#{PORT}")
def req(method,path,body=nil,revision=nil)
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,'PATCH'=>Net::HTTP::Patch}.fetch(method).new(BASE+path)
  q['Content-Type']='application/json';q.body=JSON.generate(body) if body;q['If-Match']=revision.to_s if revision
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:5){|h|h.request(q)}
  raise "HTTP #{r.code}:#{r.body[0,100]}" unless r.code.start_with?('2');r.body
end
def data(*args) = JSON.parse(req(*args))
Dir.mktmpdir('blit-pointer-pixels') do |root|
  helper=File.join(root,'art');stamp=File.join(root,'stamp')
  abort('cannot compile actual pointer source') unless system('tcc','-O2',File.join(ROOT,'tools/tests/pointer_art_gate.c'),'-o',helper)
  # SDL path only (checks SDL rotating the pointer with its output); retired at the GLES cutover.
  pid=spawn({**gate_display_env(backend: 'sdl'),'BLIT_RENDERER'=>'software','BLIT_LOGIN_AUTOSTART'=>'0',
    'BLIT_SETTINGS_PATH'=>File.join(root,'settings.json'),'BLIT_INPUT_ROOT'=>root,
    'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
    'LD_LIBRARY_PATH'=>File.join(ROOT,'build/sdl3-kms')},
    ENV.fetch('BLIT_GATE_BINARY',File.join(ROOT,'build/blit/blit-server')),PORT.to_s,out:File.join(root,'server.log'),err:[:child,:out])
  begin
    Timeout.timeout(10){loop{begin;break if data('GET','/v1/version');rescue Errno::ECONNREFUSED;end;sleep 0.02}}
    count=0
    [24,64].each do |size|
      %w[arrow hand precision].each_with_index do |shape,index|
        settings=data('GET','/v1/settings');doc=settings['document'].merge('pointer'=>{shape:shape,palette:'neon',size_px:size,shadow:true})
        data('PUT','/v1/settings',{document:doc},settings['revision'])
        system(helper,'--stamp',index.to_s,'2',size.to_s,stamp) || raise('stamp failed')
        raw=File.binread(stamp);w,h,hx,hy=raw.unpack('L<4');rgba=raw.byteslice(16..)
        [0,90,180,270].each do |rotation|
          display=data('GET','/v1/displays/display-1')
          data('PATCH','/v1/displays/display-1',{configuration:{rotation:rotation}},display['revision'])
          data('POST','/v1/input/inject',{kind:'pointer_motion',x:100,y:100})
          bmp=req('GET','/v1/diagnostics/screenshots/display-1')
          fw,fh=bmp.byteslice(18,8).unpack('l<2');bpp=bmp.byteslice(28,2).unpack1('S<')/8
          stride=(fw*bpp+3)&~3;offset=bmp.byteslice(10,4).unpack1('L<')
          # Compare every non-transparent stamp pixel with SDL's straight-alpha
          # over black. One-channel integer blend rounding may differ by1.
          h.times do |y|;w.times do |x|
            p=(y*w+x)*4;alpha=rgba.getbyte(p+3);next if alpha==0
            lx=100-hx+x;ly=100-hy+y
            px,py=case rotation;when 90;[fw-1-ly,lx];when 180;[fw-1-lx,fh-1-ly];when 270;[ly,fh-1-lx];else;[lx,ly];end
            pos=offset+(fh-1-py)*stride+px*bpp
            got=[bmp.getbyte(pos+2),bmp.getbyte(pos+1),bmp.getbyte(pos)]
            want=3.times.map{|c|rgba.getbyte(p+c)*alpha/255}
            raise "POINTER_PIXEL_FAIL shape=#{shape} size=#{size} rotation=#{rotation} pixel=#{x},#{y} got=#{got} expected=#{want}" unless got.zip(want).all?{|a,b|(a-b).abs<=1}
            count+=1
          end;end
        end
      end
    end
    puts "POINTER_PIXEL_PASS shapes=3 sizes=2 rotations=4 rgba=shared hotspot=exact pixels=#{count}"
  ensure
    Process.kill('TERM',pid) rescue nil;Process.wait(pid) rescue nil
  end
end
