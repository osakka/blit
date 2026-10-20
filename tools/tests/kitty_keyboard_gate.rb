#!/usr/bin/env ruby
# A real raw PTY child records exact bytes delivered by production key dispatch.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require 'base64'
require_relative 'gate_port'
require_relative 'sdl_test_profile'
base=URI("http://127.0.0.1:#{gate_port(19573)}")
req=lambda do |method,path,body=nil,headers={}|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete,'PATCH'=>Net::HTTP::Patch}.fetch(method).new(base+path)
  q['Content-Type']='application/json';q.body=JSON.generate(body) if body
  headers.each{|k,v|q[k]=v}
  r=Net::HTTP.start(base.host,base.port,read_timeout:5){|h|h.request(q)}
  raise "#{method} #{path}:#{r.code}:#{r.body}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
Dir.mktmpdir('blit-kitty-keyboard') do |work|
  Dir.mkdir(File.join(work,'input'))
  File.write(File.join(work,'input/event0'),'')
  File.write(File.join(work,'input/event0.identity'),"bustype=3\nvendor=1234\nproduct=5678\nversion=1\nphys=usb-kitty-test/input0\nname=Kitty fixture\nkinds=keyboard\ncapabilities=keys\n")
  pid=spawn(SdlTestProfile.environment.merge('BLIT_INPUT_ROOT'=>File.join(work,'input')),
    SdlTestProfile.binary,base.port.to_s,out:File.join(work,'server.log'),err:[:child,:out])
  begin
    Timeout.timeout(8){loop{begin;req.call('GET','/v1/version');break;rescue Errno::ECONNREFUSED;sleep 0.02;end}}
    code=<<~'CHILD'
      require 'io/console';require 'json';require 'base64';STDIN.raw!;STDOUT.sync=true
      STDOUT.write("\e]2;ready\a")
      STDIN.each_line do |line|
        item=JSON.parse(line);STDOUT.write(item['out']);STDOUT.write("\e]2;armed-#{item['id']}\a")
        data='';deadline=Process.clock_gettime(1)+0.4
        while (left=deadline-Process.clock_gettime(1))>0
          next unless IO.select([STDIN],nil,nil,left)
          data<<STDIN.readpartial(4096)
        end
        STDOUT.write("\e]2;result-#{item['id']}:#{Base64.strict_encode64(data)}\a")
      end
    CHILD
    # Each API argv is bounded to255 bytes; evaluate joined source chunks.
    args=['-e','eval(ARGV.join)']+code.scan(/.{1,200}/m)
    session=req.call('POST','/v1/terminal-sessions',{command:RbConfig.ruby,arguments:args,surface:{width:40,height:8}})
    path="/v1/terminal-sessions/#{session['id']}"
    await_title=lambda do |prefix|
      Timeout.timeout(4){loop{v=req.call('GET',path)['title'].to_s;break v if v.start_with?(prefix);sleep 0.01}}
    end
    await_title.call('ready')
    key=lambda do |code,value,device=nil|
      body={kind:'key',code:code,value:value};body[:device_id]=device if device
      req.call('POST','/v1/input/inject',body)
    end
    # Default fallback keyboard starts with NumLock on; make the baseline
    # deterministic, then test lock bits explicitly below.
    key.call(69,1);key.call(69,0)
    serial=0
    check=lambda do |name,output,events,expected|
      serial+=1
      req.call('POST',path+'/input',{mode:'raw',data:JSON.generate({id:serial,out:output})+"\n"})
      await_title.call("armed-#{serial}")
      events.each{|c,v,d|key.call(c,v,d)}
      actual=Base64.strict_decode64(await_title.call("result-#{serial}:").split(':',2)[1]).unpack1('H*')
      raise "#{name}: expected #{expected.unpack1('H*')}, got #{actual}" unless actual==expected.unpack1('H*')
    end
    esc="\e["
    check.call('legacy Super silent','',[[125,1],[125,0]],'')
    check.call('legacy text','',[[30,1],[30,0]],'a')
    check.call('initial query',esc+'?u',[],esc+'?0u')
    check.call('masked flags',esc+'>31u'+esc+'?u',[],esc+'?11u')
    check.call('pop restores zero',esc+'<u'+esc+'?u',[],esc+'?0u')
    check.call('lone left Super',esc+'>9u',[[125,1]],esc+'57444;9u')
    check.call('Super+a','',[[30,1],[30,0],[125,0]],esc+'97;9u')
    check.call('right Super','',[[126,1],[126,0]],esc+'57450;9u')
    check.call('repeat/release',esc+'=11u',[[30,1],[30,2],[30,0]],esc+'97;1u'+esc+'97;1:2u'+esc+'97;1:3u')
    check.call('Super release state','',[[125,1],[125,0]],esc+'57444;9u'+esc+'57444;1:3u')
    check.call('two Super keys remain independent','',[[125,1],[126,1],[125,0],[126,0]],
      esc+'57444;9u'+esc+'57450;9u'+esc+'57444;9:3u'+esc+'57450;1:3u')
    check.call('Ctrl+Shift identity','',[[29,1],[42,1],[30,1],[30,0],[42,0],[29,0]],
      esc+'57442;5u'+esc+'57441;6u'+esc+'97;6u'+esc+'97;6:3u'+esc+'57441;5:3u'+esc+'57442;1:3u')
    check.call('nested push/pop',esc+'>1u'+esc+'?u'+esc+'>8u'+esc+'<u'+esc+'?u',[],esc+'?1u'+esc+'?1u')
    check.call('deep pop',esc+'<99u'+esc+'?u',[],esc+'?0u')
    check.call('set/or/clear',esc+'=1u'+esc+'=8;2u'+esc+'=1;3u'+esc+'?u',[],esc+'?8u')
    check.call('buffer stacks independent',esc+'?1049h'+esc+'?u'+esc+'>3u'+esc+'?1049l'+esc+'?u',[],esc+'?0u'+esc+'?8u')
    check.call('alternate restores its state',esc+'?1049h'+esc+'?u'+esc+'<u'+esc+'?1049l',[],esc+'?3u')
    check.call('bounded stack', (esc+'>9u')*20+esc+'?u',[],esc+'?9u')
    raise 'stack not bounded' unless req.call('GET',path).dig('terminal_capabilities','keyboard','stack_depth')==16
    check.call('reset clears both stacks',"\ec"+esc+'?u',[],esc+'?0u')
    check.call('disambiguated Ctrl+c',esc+'>1u',[[29,1],[46,1],[46,0],[29,0]],esc+'99;5u')
    check.call('disambiguation keeps text/recovery','',[[42,1],[30,1],[30,0],[42,0],[28,1],[28,0]],"A\r")
    check.call('keypad navigation distinct','',[[72,1],[72,0]],esc+'57419;1u')
    check.call('legacy restored',esc+'<u',[[30,1],[30,0]],'a')
    check.call('NumLock events and numeric keypad',esc+'>8u',[[69,1],[69,0],[72,1],[72,0],[69,1],[69,0]],
      esc+'57360;129u'+esc+'57407;129u'+esc+'57360;1u')
    check.call('CapsLock state','',[[58,1],[58,0],[30,1],[30,0],[58,1],[58,0]],
      esc+'57358;65u'+esc+'97;65u'+esc+'57358;1u')
    check.call('event types without all-key mode',esc+'=2u',[[103,1],[103,2],[103,0]],
      esc+'A'+esc+'1;1:2A'+esc+'1;1:3A')
    check.call('parameterised replies do not loop',esc+'=9u'+esc+'?9u',[],'')
    fixture=nil
    Timeout.timeout(5){loop{fixture=req.call('GET','/v1/input/devices')['devices'].find{|d|d['name']=='Kitty fixture'};break if fixture;sleep 0.05}}
    source=fixture['id']
    check.call('fixture NumLock baseline','',[[69,1,source],[69,0,source]],esc+'57360;1u')
    check.call('modifier state isolated per keyboard','',[[125,1],[30,1,source],[30,0,source],[125,0]],
      esc+'57444;9u'+esc+'97;1u')
    revision=req.call('GET',"/v1/input/devices/#{source}")['revision']
    req.call('PATCH',"/v1/input/devices/#{source}",{configuration:{modifiers:{'58'=>'meta','125'=>'none'}}},{'If-Match'=>revision.to_s})
    check.call('remapped Super and disabled physical Meta','',[[58,1,source],[30,1,source],[30,0,source],[58,0,source],
      [125,1,source],[30,1,source],[30,0,source],[125,0,source]],
      esc+'57444;9u'+esc+'97;9u'+esc+'97;1u')
    revision=req.call('GET',"/v1/input/devices/#{source}")['revision']
    req.call('PATCH',"/v1/input/devices/#{source}",{configuration:{modifiers:{'30'=>'none','31'=>'control'}}},{'If-Match'=>revision.to_s})
    check.call('legacy printable modifier remaps stay silent',esc+'=0u',
      [[30,1,source],[30,0,source],[31,1,source],[31,0,source]],'')
    check.call('restore negotiated state',esc+'=9u'+esc+'?u',[],esc+'?9u')
    other=req.call('POST','/v1/terminal-sessions',{command:'/bin/cat',surface:{width:1,height:1}})
    raise 'keyboard state leaked between sessions' unless other.dig('terminal_capabilities','keyboard','active_flags')==0 &&
      req.call('GET',path).dig('terminal_capabilities','keyboard','active_flags')==9
    req.call('DELETE',"/v1/terminal-sessions/#{other['id']}")
    puts "KITTY_KEYBOARD_PASS cases=#{serial} bytes=exact query_stack=independent_bounded modifiers=super_ctrl_shift legacy=preserved"
  ensure
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
