#!/usr/bin/env ruby
# A real raw PTY child records exact bytes delivered by production key dispatch.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require 'base64'
require_relative 'gate_port'
require_relative 'test_profile'
base=URI("http://127.0.0.1:#{gate_port(19573)}")
req=lambda do |method,path,body=nil,headers={}|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete,'PATCH'=>Net::HTTP::Patch,'PUT'=>Net::HTTP::Put}.fetch(method).new(base+path)
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
  # BLIT_TEST_VT_SIGNALS: SIGUSR1/SIGUSR2 drive the console release/acquire
  # path with no VT, so focus on input hand-over can be checked.
  pid=gate_spawn(TestProfile.environment.merge('BLIT_INPUT_ROOT'=>File.join(work,'input'),
    'BLIT_TEST_MODE'=>'1','BLIT_TEST_VT_SIGNALS'=>'1'),
    TestProfile.binary,base.port.to_s,out:File.join(work,'server.log'),err:[:child,:out])
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
      events.each{|c,v,d| c.respond_to?(:call) ? c.call : key.call(c,v,d)}
      actual=Base64.strict_decode64(await_title.call("result-#{serial}:").split(':',2)[1]).unpack1('H*')
      raise "#{name}: expected #{expected.unpack1('H*')}, got #{actual}" unless actual==expected.unpack1('H*')
    end
    esc="\e["
    check.call('legacy Super silent','',[[125,1],[125,0]],'')
    check.call('legacy text','',[[30,1],[30,0]],'a')
    check.call('initial query',esc+'?u',[],esc+'?0u')
    check.call('masked flags',esc+'>63u'+esc+'?u',[],esc+'?31u')
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
    # xkb's locks (ADR 0231): a lock goes on at its key's press and off at
    # the release of the press that unlocks it, as X and Wayland programs
    # see it, so that press still reports the lock on.
    check.call('NumLock events and numeric keypad',esc+'>8u',[[69,1],[69,0],[72,1],[72,0],[69,1],[69,0]],
      esc+'57360;129u'+esc+'57407;129u'+esc+'57360;129u')
    check.call('CapsLock state','',[[58,1],[58,0],[30,1],[30,0],[58,1],[58,0]],
      esc+'57358;65u'+esc+'97;65u'+esc+'57358;65u')
    check.call('event types without all-key mode',esc+'=2u',[[103,1],[103,2],[103,0]],
      esc+'A'+esc+'1;1:2A'+esc+'1;1:3A')
    # Flag 4, report alternate keys: key:shifted:base, the shifted key only
    # with Shift, each left out when it equals the key.
    check.call('flag 4 query',esc+'=12u'+esc+'?u',[],esc+'?12u')
    check.call('flag 4 US key, base omitted','',[[30,1],[30,0]],esc+'97;1u')
    check.call('flag 4 shifted letter','',[[42,1],[30,1],[30,0],[42,0]],esc+'57441;2u'+esc+'97:65;2u')
    check.call('flag 4 shifted digit','',[[42,1],[2,1],[2,0],[42,0]],esc+'57441;2u'+esc+'49:33;2u')
    check.call('flag 4 shifted space has no alternates','',[[42,1],[57,1],[57,0],[42,0]],esc+'57441;2u'+esc+'32;2u')
    check.call('flag 4 functional keys unchanged','',[[59,1],[59,0],[28,1],[28,0],[42,1],[103,1],[103,0],[42,0]],
      esc+'1;1P'+esc+'13;1u'+esc+'57441;2u'+esc+'1;2A')
    check.call('flag 4 with event types',esc+'=14u',[[42,1],[30,1],[30,0],[42,0]],
      esc+'57441;2u'+esc+'97:65;2u'+esc+'97:65;2:3u'+esc+'57441;1:3u')
    check.call('flag 4 with disambiguate, Ctrl+Shift',esc+'=5u',[[29,1],[42,1],[30,1],[30,0],[42,0],[29,0]],esc+'97:65;6u')
    check.call('flag 4 alone stays legacy',esc+'=4u',[[30,1],[30,0],[29,1],[30,1],[30,0],[29,0]],"a\x01")
    check.call('flag 4 push/pop',esc+'=0u'+esc+'>4u'+esc+'?u'+esc+'>13u'+esc+'?u'+esc+'<u'+esc+'?u'+esc+'<u'+esc+'?u',[],
      esc+'?4u'+esc+'?13u'+esc+'?4u'+esc+'?0u')
    # Flag 16, report associated text (with flag 8): a press or repeat that
    # types text carries it as a third field; a chord, a release and a
    # functional key carry none.
    check.call('flag 16 query',esc+'=27u'+esc+'?u',[],esc+'?27u')
    check.call('flag 16 text, repeat, release','',[[30,1],[30,2],[30,0]],esc+'97;1;97u'+esc+'97;1:2;97u'+esc+'97;1:3u')
    check.call('flag 16 shifted text','',[[42,1],[2,1],[2,0],[42,0]],
      esc+'57441;2u'+esc+'49;2;33u'+esc+'49;2:3u'+esc+'57441;1:3u')
    check.call('flag 16 chords and a Super tap type nothing','',[[29,1],[30,1],[30,0],[29,0],[125,1],[125,0]],
      esc+'57442;5u'+esc+'97;5u'+esc+'97;5:3u'+esc+'57442;1:3u'+esc+'57444;9u'+esc+'57444;1:3u')
    check.call('flag 16 space, keypad, functional','',[[57,1],[57,0],[78,1],[78,0],[72,1],[72,0],[28,1],[28,0]],
      esc+'32;1;32u'+esc+'32;1:3u'+esc+'57413;1;43u'+esc+'57413;1:3u'+esc+'57419;1u'+esc+'57419;1:3u'+esc+'13;1u'+esc+'13;1:3u')
    check.call('flag 16 CapsLock text','',[[58,1],[58,0],[30,1],[30,0],[58,1],[58,0]],
      esc+'57358;65u'+esc+'57358;65:3u'+esc+'97;65;65u'+esc+'97;65:3u'+esc+'57358;65u'+esc+'57358;1:3u')
    check.call('flag 16 with flag 4',esc+'=31u',[[42,1],[2,1],[2,0],[42,0]],
      esc+'57441;2u'+esc+'49:33;2;33u'+esc+'49:33;2:3u'+esc+'57441;1:3u')
    check.call('flag 16 without flag 8 stays text',esc+'=17u',[[30,1],[30,0]],'a')
    check.call('parameterised replies do not loop',esc+'=9u'+esc+'?9u',[],'')
    fixture=nil
    Timeout.timeout(5){loop{fixture=req.call('GET','/v1/input/devices')['devices'].find{|d|d['name']=='Kitty fixture'};break if fixture;sleep 0.05}}
    source=fixture['id']
    check.call('fixture NumLock baseline','',[[69,1,source],[69,0,source]],esc+'57360;129u')  # unlocking: on until its release (xkb)
    check.call('modifier state isolated per keyboard','',[[125,1],[30,1,source],[30,0,source],[125,0]],
      esc+'57444;9u'+esc+'97;1u')
    # Focus on input hand-over: the console release path reports focus out
    # (after its synthetic key releases), the acquire path focus in. The
    # releases themselves are not seen here: they walk the claimed evdev
    # keyboards, and a fixture without evdev ioctls is never claimed.
    req.call('PATCH',path,{focused:true},{'If-Match'=>req.call('GET',path)['revision'].to_s})
    raise 'gate session is not focused' unless req.call('GET',path)['focused']==true
    usr=lambda{|sig| lambda{Process.kill(sig,pid);sleep 0.1}}
    check.call('focus out on release',esc+'?1004h'+esc+'=10u',[[usr.call('USR1')]],esc+'O')
    check.call('focus in on acquire','',[[usr.call('USR2')]],esc+'I')
    check.call('no focus report without 1004',esc+'?1004l',[[usr.call('USR1')],[usr.call('USR2')]],'')
    check.call('restore flags 9',esc+'=9u',[],'')
    # The system's xkb `us` (ADR 0231): the key beside Left Shift on an ISO
    # keyboard types < and >, Right Alt is Alt, and the altgr role is the
    # keymap's ISO_Level3_Shift (kitty has no modifier bit for it). Other
    # layouts come with the keyboard list (ADR 0231 step 2).
    check.call('ISO key types < in us',esc+'=27u',[[86,1],[86,0],[42,1],[86,1],[86,0],[42,0]],
      esc+'60;1;60u'+esc+'60;1:3u'+esc+'57441;2u'+esc+'60;2;62u'+esc+'60;2:3u'+esc+'57441;1:3u')
    check.call('Right Alt is Alt in us',esc+'=9u',[[100,1],[30,1],[30,0],[100,0]],esc+'57449;3u'+esc+'97;3u')
    revision=req.call('GET',"/v1/input/devices/#{source}")['revision']
    req.call('PATCH',"/v1/input/devices/#{source}",{configuration:{modifiers:{'100'=>'altgr'}}},{'If-Match'=>revision.to_s})
    check.call('altgr role is ISO_Level3_Shift','',[[100,1,source],[30,1,source],[30,0,source],[100,0,source]],
      esc+'57453;1u'+esc+'97;1u')
    revision=req.call('GET',"/v1/input/devices/#{source}")['revision']
    req.call('PATCH',"/v1/input/devices/#{source}",{configuration:{modifiers:{'58'=>'meta','125'=>'none'}}},{'If-Match'=>revision.to_s})
    check.call('remapped Super and disabled physical Meta','',[[58,1,source],[30,1,source],[30,0,source],[58,0,source],
      [125,1,source],[30,1,source],[30,0,source],[125,0,source]],
      esc+'57444;9u'+esc+'97;9u'+esc+'97;1u')
    revision=req.call('GET',"/v1/input/devices/#{source}")['revision']
    req.call('PATCH',"/v1/input/devices/#{source}",{configuration:{modifiers:{'30'=>'none','31'=>'control'}}},{'If-Match'=>revision.to_s})
    check.call('legacy printable modifier remaps stay silent',esc+'=0u',
      [[30,1,source],[30,0,source],[31,1,source],[31,0,source]],'')
    # ADR 0231 step 2: a second keyboard, phonetic Arabic, made current. The
    # S key types seen, its kitty key the layout's own with the US base key
    # beside it, so Ctrl+C is the C key in any layout -- 0x03 in legacy.
    req.call('PUT','/v1/input/keyboards',{keyboards:[{layout:'us'},{layout:'ara',variant:'mac-phonetic'}],current:1})
    check.call('ara phonetic text and base key',esc+'=31u',[[31,1],[31,0]],esc+'1587::115;1;1587u'+esc+'1587::115;1:3u')
    check.call('ara Ctrl+C by its base key','',[[29,1],[46,1],[46,0],[29,0]],
      esc+'57442;5u'+esc+'1589::99;5u'+esc+'1589::99;5:3u'+esc+'57442;1:3u')
    check.call('ara legacy text and Ctrl+C',esc+'=0u',[[31,1],[31,0],[29,1],[46,1],[46,0],[29,0]],"\u0633\x03")
    req.call('PUT','/v1/input/keyboards',{current:0})
    check.call('us again','',[[31,1],[31,0]],'s')
    check.call('restore negotiated state',esc+'=9u'+esc+'?u',[],esc+'?9u')
    other=req.call('POST','/v1/terminal-sessions',{command:'/bin/cat',surface:{width:1,height:1}})
    raise 'keyboard state leaked between sessions' unless other.dig('terminal_capabilities','keyboard','active_flags')==0 &&
      req.call('GET',path).dig('terminal_capabilities','keyboard','active_flags')==9
    req.call('DELETE',"/v1/terminal-sessions/#{other['id']}")
    puts "KITTY_KEYBOARD_PASS cases=#{serial} bytes=exact query_stack=independent_bounded modifiers=super_ctrl_shift alternates=flag4 text=flag16 xkb=us,ara-phonetic focus_handover=out_in legacy=preserved"
  ensure
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
