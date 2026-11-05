#!/usr/bin/env ruby
# Real local PTY -> parser -> cells -> SDL presentation, no Hibr or live changes.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require 'fileutils'
require 'digest'
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
    # ADR 0212: the catalogue's revision is the SHA-256 of the file as packaged,
    # first in the body and the ETag; a current If-None-Match is answered 304; a
    # changed catalogue changes both, so a long-lived client can tell.
    get_icons=lambda do |match=nil|
      q=Net::HTTP::Get.new(base+'/v1/fonts/icons'); q['If-None-Match']=match if match
      Net::HTTP.start(base.host,base.port,read_timeout:5){|h|h.request(q)}
    end
    file=File.binread(File.join(TestProfile::ROOT,'assets/fonts/icons/catalogue.json'))
    response=get_icons.call
    raise "catalogue: #{response.code}" unless response.code=='200'
    served=JSON.parse(response.body)
    revision=served.delete('revision')
    raise 'API catalogue differs from generator source' unless served==expected
    raise 'the revision is not the SHA-256 of the catalogue file' unless revision==Digest::SHA256.hexdigest(file)
    raise 'the revision is not the first member' unless response.body.start_with?('{"revision":"'+revision+'",')
    raise "ETag #{response['ETag'].inspect} is not the quoted revision" unless response['ETag']=='"'+revision+'"'
    raise "Cache-Control #{response['Cache-Control'].inspect}, not no-cache" unless response['Cache-Control']=='no-cache'
    raise 'the catalogue is over the 256 KiB the API allows' unless response.body.bytesize<262144
    ['"'+revision+'"','W/"'+revision+'"','*','"x", "'+revision+'"'].each do |match|
      not_modified=get_icons.call(match)
      raise "If-None-Match #{match} gave #{not_modified.code}, not 304" unless not_modified.code=='304'
      raise 'a 304 has a body or no ETag' unless not_modified.body.to_s.empty? && not_modified['ETag']=='"'+revision+'"'
    end
    stale=get_icons.call('"'+('0'*64)+'"')
    raise 'a stale If-None-Match was not answered with the catalogue' unless stale.code=='200' && JSON.parse(stale.body)['revision']==revision
    catalogue=File.join(fonts,'icons/catalogue.json')
    original=File.binread(catalogue)
    begin
      File.binwrite(catalogue,original+"\n")
      changed=get_icons.call('"'+revision+'"')
      raise 'a changed catalogue was answered 304' unless changed.code=='200'
      new_revision=JSON.parse(changed.body)['revision']
      raise 'a changed catalogue kept its revision' if new_revision==revision
      raise 'a changed catalogue has the wrong ETag' unless new_revision==Digest::SHA256.hexdigest(original+"\n") && changed['ETag']=='"'+new_revision+'"'
      # SHA-256 pads to a 64-byte block: a message ending 56 bytes or more into
      # one (residues 56-63) takes a second block, and 55 is the most that fits
      # in the first, and an exact multiple takes a whole one. Trailing spaces
      # keep the JSON valid.
      [0,1,55,56,60,63].each do |residue|
        padded=original+' '*((residue-original.bytesize)%64)
        File.binwrite(catalogue,padded)
        got=JSON.parse(get_icons.call.body)['revision']
        raise "revision of a #{padded.bytesize}-byte catalogue (residue #{residue}) is #{got}, not its SHA-256" unless got==Digest::SHA256.hexdigest(padded)
      end
    ensure
      File.binwrite(catalogue,original)
    end
    cases+=1
    File.rename(catalogue,catalogue+'.saved')
    begin
      raise 'missing catalogue not diagnosed' unless request.call('GET','/v1/fonts/icons',nil,503)['code']=='font_icon_catalogue_unavailable'
      File.write(catalogue,' '*262144)
      raise 'oversized catalogue not diagnosed' unless request.call('GET','/v1/fonts/icons',nil,503)['code']=='font_icon_catalogue_unavailable'
    ensure
      File.rename(catalogue+'.saved',catalogue)
    end
    capture=lambda do
      r=Net::HTTP.get_response(base+"/v1/diagnostics/screenshots/#{display}")
      raise 'capture failed' unless r.code=='200' && r.body.start_with?('BM')
      r.body
    end
    sequences={icons:((0x100000..0x100006).to_a+(0x100009..0x100022).to_a+[0x100033,0x100034,0x10022f,0x100230]).pack('U*')+"\u16d2\u21cc\u270e\u2615X",
               arabic:"ﺮﺒْﺣِ ٠١٢٣٤٥٦٧٨٩ ﻻX"}
    %w[hack ibm-3270].each do |profile|
      [16,17,22].each do |size|
        request.call('PUT','/v1/fonts',{font_id:profile,size_px:size})
        sequences.each do |kind, sequence|
        [false,true].each do |fragmented|
          code=fragmented ? 'STDOUT.binmode;STDOUT.sync=true;ARGV[0].bytes.each{|b|STDOUT.write(b.chr);sleep 0.001};sleep 10' : 'STDOUT.binmode;STDOUT.sync=true;STDOUT.write(ARGV[0]);sleep 10'
          session=request.call('POST','/v1/terminal-sessions',{
            command:RbConfig.ruby,arguments:['-e',code,"\e[?25l"+sequence],
            surface:{width:64,height:2},text_layout:'scalar'},201)
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
            raise 'private glyphs changed terminal advance' if kind==:icons && !cells.first(37).all?{|c|c['width']==1}
            pty=capture.call
            request.call('PUT',path,{base_revision:snapshot.fetch('revision'),cells:[{x:0,y:0,text:' '*64},{x:0,y:0,text:sequence}]})
            raise 'PTY/API presented pixels differ' unless capture.call==pty
            cases+=1
          ensure
            request.call('DELETE',"/v1/terminal-sessions/#{session.fetch('id')}",nil,204)
          end
        end
        end
      end
    end
    puts "CUSTOM_ICON_API_PASS revision=sha256+etag+304+change_detected catalogue=canonical profiles=2 sizes=16+17+22 real_PTY=contiguous+bytewise specimens=icons+Arabic_marks_digits cases=#{cases} private_advances=1 presented_API_parity=exact scope=local_SDL_not_physical"
  rescue Exception
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    Process.kill('TERM',pid) rescue nil
    Timeout.timeout(8){Process.wait(pid)}
  end
end
