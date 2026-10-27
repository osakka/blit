#!/usr/bin/env ruby
# Real local PTY -> parser -> cells -> SDL presentation, no Hibr or live changes.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require 'fileutils'
require_relative 'gate_port'
require_relative 'test_profile'
port = gate_port(19573)
base = URI("http://127.0.0.1:#{port}")
request = lambda do |method, path, body = nil, expected = 200|
  q = {'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base+path)
  q['Content-Type']='application/json'; q.body=JSON.generate(body) if body
  r=Net::HTTP.start(base.host,base.port,read_timeout:5){|h|h.request(q)}
  raise "#{path}: #{r.code} #{r.body}" unless r.code.to_i==expected
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
Dir.mktmpdir('blit-custom-icon-api') do |work|
  fonts=File.join(work,'fonts')
  FileUtils.cp_r(File.join(TestProfile::ROOT,'assets/fonts'),fonts)
  env=TestProfile.environment.merge('BLIT_FONT_ROOT'=>fonts)
  pid=spawn(env,TestProfile.binary,port.to_s,out:File.join(work,'server.log'),err:[:child,:out])
  cases=0
  begin
    Timeout.timeout(8) do
      loop do
        begin
          request.call('GET','/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
    TestProfile.verify!(request.call('GET','/v1/diagnostics'))
    display=TestProfile.display_id!(request.call('GET','/v1/displays'))
    expected=JSON.parse(File.read(File.join(TestProfile::ROOT,'assets/fonts/icons/catalogue.json')))
    raise 'API catalogue differs from generator source' unless request.call('GET','/v1/fonts/icons')==expected
    catalogue=File.join(fonts,'icons/catalogue.json')
    File.rename(catalogue,catalogue+'.saved')
    begin
      raise 'missing catalogue not diagnosed' unless request.call('GET','/v1/fonts/icons',nil,503)['code']=='font_icon_catalogue_unavailable'
      File.write(catalogue,' '*8192)
      raise 'oversized catalogue not diagnosed' unless request.call('GET','/v1/fonts/icons',nil,503)['code']=='font_icon_catalogue_unavailable'
    ensure
      File.rename(catalogue+'.saved',catalogue)
    end
    capture=lambda do
      r=Net::HTTP.get_response(base+"/v1/diagnostics/screenshots/#{display}")
      raise 'capture failed' unless r.code=='200' && r.body.start_with?('BM')
      r.body
    end
    sequences={icons:((0x100000..0x100006).to_a+(0x100009..0x10001e).to_a).pack('U*')+"\u16d2\u21cc\u270e\u2615X",
               arabic:"ﺮﺒْﺣِ ٠١٢٣٤٥٦٧٨٩ ﻻX"}
    %w[hack ibm-3270].each do |profile|
      [16,17,22].each do |size|
        request.call('PUT','/v1/fonts',{font_id:profile,size_px:size})
        sequences.each do |kind, sequence|
        [false,true].each do |fragmented|
          code=fragmented ? 'STDOUT.binmode;STDOUT.sync=true;ARGV[0].bytes.each{|b|STDOUT.write(b.chr);sleep 0.001};sleep 10' : 'STDOUT.binmode;STDOUT.sync=true;STDOUT.write(ARGV[0]);sleep 10'
          session=request.call('POST','/v1/terminal-sessions',{
            command:RbConfig.ruby,arguments:['-e',code,"\e[?25l"+sequence],
            surface:{width:40,height:2},text_layout:'scalar'},201)
          path="/v1/surfaces/#{session.fetch('surface_id')}/cells"
          begin
            snapshot=Timeout.timeout(5) do
              loop do
                value=request.call('GET',path)
                break value if value.fetch('cells').any?{|c|c['text']=='X'}
                sleep 0.01
              end
            end
            cells=snapshot.fetch('cells').select{|c|c['y']==0 && c['width']!=0}.sort_by{|c|c['x']}
            raise 'UTF-8 glyphs lost/reordered' unless cells.map{|c|c['text']}.join.rstrip==sequence
            raise 'private glyphs changed terminal advance' if kind==:icons && !cells.first(29).all?{|c|c['width']==1}
            pty=capture.call
            request.call('PUT',path,{base_revision:snapshot.fetch('revision'),cells:[{x:0,y:0,text:' '*40},{x:0,y:0,text:sequence}]})
            raise 'PTY/API presented pixels differ' unless capture.call==pty
            cases+=1
          ensure
            request.call('DELETE',"/v1/terminal-sessions/#{session.fetch('id')}",nil,204)
          end
        end
        end
      end
    end
    puts "CUSTOM_ICON_API_PASS catalogue=canonical profiles=2 sizes=16+17+22 real_PTY=contiguous+bytewise specimens=icons+Arabic_marks_digits cases=#{cases} private_advances=1 presented_API_parity=exact scope=local_SDL_not_physical"
  rescue Exception
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    Process.kill('TERM',pid) rescue nil
    Timeout.timeout(8){Process.wait(pid)}
  end
end
