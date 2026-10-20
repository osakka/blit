#!/usr/bin/env ruby
# Actual local PTY -> parser -> presented pixels, with an independent grid oracle.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require 'fileutils'
require_relative 'gate_port'
require_relative 'sdl_test_profile'
base=URI("http://127.0.0.1:#{gate_port(19568)}")
request=lambda do |method,path,body=nil,status=200,headers={}|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,
     'PATCH'=>Net::HTTP::Patch,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base+path)
  q['Content-Type']='application/json'
  headers.each { |k,v| q[k]=v }
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(base.host,base.port,open_timeout:2,read_timeout:8) { |h| h.request(q) }
  raise "#{method} #{path}: #{r.code} #{r.body}" unless r.code.to_i==status
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
Dir.mktmpdir('blit-emoji-transitions') do |work|
  env=SdlTestProfile.environment.merge('BLIT_FONT_ROOT'=>File.join(SdlTestProfile::ROOT,'assets/fonts'))
  pid=spawn(env,SdlTestProfile.binary,base.port.to_s,out:File.join(work,'server.log'),err:[:child,:out])
  context='startup'; cases=0; serial=0
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
    SdlTestProfile.verify!(request.call('GET','/v1/diagnostics'))
    display=SdlTestProfile.display_id!(request.call('GET','/v1/displays'))
    samples=["❤️","1️⃣","🇬🇧","👍🏽","👩‍💻","👨‍👩‍👧‍👦",
      "🏴\u{e0067 e0062 e0065 e006e e0067 e007f}","\u2764\ufe0e","\u{1f41f}\ufe0e"]
    child='require "io/console";require "json";STDIN.raw!;STDOUT.sync=true;STDOUT.write("\e[?25l\e]2;ready\a");STDIN.each_line{|l|STDOUT.write(JSON.parse(l))}'
    %w[blit-modern blit-retro].each do |profile|
      ENV.fetch('BLIT_TRANSITION_SIZES','15,16').split(',').map(&:to_i).each do |size|
        request.call('PUT','/v1/fonts',{font_id:profile,size_px:size})
        session=request.call('POST','/v1/terminal-sessions',{command:RbConfig.ruby,
          arguments:['-e',child],text_layout:'emoji',surface:{width:16,height:4}},201)
        path="/v1/terminal-sessions/#{session.fetch('id')}"
        cells_path="/v1/surfaces/#{session.fetch('surface_id')}/cells"
        wait_title=lambda do |title|
          Timeout.timeout(5) do
            loop do
              break if request.call('GET',path)['title']==title
              sleep 0.005
            end
          end
        end
        emit=lambda do |bytes|
          serial+=1; title="frame-#{serial}"
          request.call('POST',path+'/input',{mode:'raw',data:JSON.generate(bytes+"\e]2;#{title}\a")+"\n"},202)
          wait_title.call(title)
        end
        capture=lambda do |state|
          Timeout.timeout(5) do
            loop do
              shot=Net::HTTP.get_response(base+"/v1/diagnostics/screenshots/#{display}")
              raise "capture failed #{shot.code}" unless shot.code=='200'&&shot.body.start_with?('BM')
              revs=shot['X-Blit-Capture-Revisions'].to_s.split(',').map { |pair| pair.split('=',2) }.to_h
              break shot.body if shot['X-Blit-Capture-Presented']=='true'&&revs[session['surface_id']].to_i==state.fetch('revision')
            end
          end
        end
        verify=lambda do |expected|
          state=request.call('GET',cells_path)
          grid=state.fetch('cells').to_h { |c| [[c['x'],c['y']],c] }
          grid.each_value do |c|
            if c['width']==2
              raise 'wide owner crosses the grid or lacks its continuation' unless c['x']+1<state['width']&&grid.dig([c['x']+1,c['y']],'width')==0
            elsif c['width']==0
              raise 'orphan continuation' unless c['x']>0&&grid.dig([c['x']-1,c['y']],'width')==2
            end
          end
          observed=state.fetch('cells').filter_map do |c|
            [c['x'],c['y'],c['text'],c['width']] if c['width']!=0&&!['',"\0",' '].include?(c['text'])
          end.sort
          raise "grid mismatch expected=#{expected.sort.inspect} actual=#{observed.inspect}" unless observed==expected.sort
          actual=capture.call(state)
          clear=(0...state['height']).map { |y| {x:0,y:y,text:' '*state['width']} }
          request.call('PUT',cells_path,{base_revision:state['revision'],text_layout:'emoji',
            cells:clear+expected.map { |x,y,text,_| {x:x,y:y,text:text} }})
          reference=capture.call(request.call('GET',cells_path))
          unless actual==reference
            if (dir=ENV['BLIT_TRANSITION_ARTIFACT_DIR'])
              FileUtils.mkdir_p(dir)
              stem=File.join(dir,context.gsub(/[^a-zA-Z0-9_.-]/,'_'))
              File.binwrite(stem+'-actual.bmp',actual)
              File.binwrite(stem+'-reference.bmp',reference)
              File.write(stem+'-state.json',JSON.pretty_generate(context:context,actual:state,expected:expected))
            end
            raise 'presented pixels differ from independently positioned grid'
          end
          cases+=1
        end
        resize=lambda do |width|
          current=request.call('GET',path)
          request.call('PATCH',path,{surface:{width:width,height:4}},200,{'If-Match'=>current.fetch('revision').to_s})
        end
        begin
          wait_title.call('ready')
          samples.each_with_index do |sequence,index|
            width=index==7 ? 1 : 2
            original=[[0,0,sequence,width],[width,0,'X',1]]
            operations={
              move:["\e[H\e[2K\e[2;4H#{sequence}X",[[3,1,sequence,width],[3+width,1,'X',1]]],
              overwrite:["\e[1;2HZ",width==2 ? [[1,0,'Z',1],[2,0,'X',1]] : [[0,0,sequence,1],[1,0,'Z',1]]],
              erase:["\e[1;2H\e[X",width==2 ? [[2,0,'X',1]] : [[0,0,sequence,1]]],
              scroll_up:["\e[S",[]],
              scroll_down:["\e[T",[[0,1,sequence,width],[width,1,'X',1]]],
              alternate:["\e[?1049hJUNK\e[?1049l",original],
              clear:["\e[2J\e[H",[]]
            }
            operations.each do |name,(bytes,expected)|
              context="#{profile}/#{size}px/sample#{index}/#{name}"
              emit.call("\e[2J\e[H#{sequence}X")
              capture.call(request.call('GET',cells_path)) # Make the old image visible first.
              emit.call(bytes)
              verify.call(expected)
            end
            context="#{profile}/#{size}px/sample#{index}/resize-cut"
            emit.call("\e[2J\e[1;4H#{sequence}X")
            capture.call(request.call('GET',cells_path))
            resize.call(4)
            surviving=width==1 ? [[3,0,sequence,1]] : []
            verify.call(surviving)
            resize.call(16)
            verify.call(surviving)
          end
        ensure
          request.call('DELETE',path,nil,204)
        end
      end
    end
    puts "EMOJI_TRANSITION_PASS renderer=#{SdlTestProfile.renderer} profiles=2 sizes=#{ENV.fetch('BLIT_TRANSITION_SIZES','15,16')} sequences=9 transitions=move+overwrite+erase+scroll+alternate+clear+shrink+grow checks=#{cases} oracle=independent_grid presented_revision=exact pixels=API_parity scope=local_PTY_not_physical_or_Kitty_sixel"
  rescue Exception
    warn "EMOJI_TRANSITION_FAIL #{context} log=#{work}/server.log"
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    Process.kill('TERM',pid) rescue nil
    Timeout.timeout(8) { Process.wait(pid) }
  end
end
