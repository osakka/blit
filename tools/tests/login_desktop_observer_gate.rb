#!/usr/bin/env ruby
# Synthetic native-query payloads test the controller; native CLI is covered
# separately by native_hold_observer_gate.rb. Actual credential drop + Hibr hook.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'fileutils'
require 'open3'
require_relative 'gate_port'
ROOT=File.expand_path('../..',__dir__)
BINARY=ENV.fetch('BLIT_GATE_BINARY',File.join(ROOT,'build/blit/blit-server'))
PORT=gate_port(ENV.fetch('BLIT_OBSERVER_GATE_PORT','19489'))
BASE=URI("http://127.0.0.1:#{PORT}")
unless system('sudo','-n','true',out:File::NULL,err:File::NULL)
  puts 'LOGIN_DESKTOP_OBSERVER_SKIP root_fixture=required';exit 77
end
def request(method,path,body=nil)
  started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE+path)
  q['Authorization']='Bearer observer-admin';q['Content-Type']='application/json'
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:3){|h|h.request(q)}
  elapsed=Process.clock_gettime(Process::CLOCK_MONOTONIC)-started
  $max_http=[($max_http || 0),elapsed].max
  raise "HTTP #{r.code}:#{r.body}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
def check(ok,message) = (raise(message) unless ok)
def await
  Timeout.timeout(10) do
    loop do
      begin
        result=yield;return result if result
      rescue Errno::ECONNREFUSED,IOError
      end
      sleep 0.03
    end
  end
end
def stop
  return unless $pid
  system('sudo','-n','kill','-TERM',$pid.to_s,out:File::NULL,err:File::NULL)
  Timeout.timeout(10){Process.wait($pid)}
  $pid=nil
end
at_exit{stop}
Dir.mktmpdir('blit-login-observe') do |root|
  File.chmod(0755,root);Dir.mkdir(File.join(root,'input'))
  hook=File.join(root,'hook.hibr');target=File.join(root,'target.hibr')
  FileUtils.cp(File.join(ROOT,'hibr/blit-login-session.hibr'),hook)
  FileUtils.cp(File.join(ROOT,'tools/tests/fixtures/login_identity_target.hibr'),target)
  File.chmod(0755,target)
  %w[ready secondary missing owner_missing geometry wrong_uid overflow nul oversized timeout exit_failure expires shell waiting].each do |mode|
    result=File.join(root,"#{mode}.result");owner_file=result+'.pid';count=result+'.count';startup=result+'.startup'
    [result,owner_file,count,startup].each{|p|File.write(p,'');File.chmod(0666,p)}
    observer=File.join(root,'observer.hibr')
    action=case mode
      when 'ready' then 'printf "ready %s 24 128 0 0 24 128 1\\nclients 2\\nclient %s 0 0 24 128 1\\nclient auxiliary -24 -8 12 64 0\\n" "$owner" "$2"'
      when 'secondary' then 'printf "ready %s 96 128 48 0 48 128 0\\nclients 1\\nclient %s 48 0 48 128 0\\n" "$owner" "$2"'
      when 'missing' then 'printf "waiting client_missing\\n"'
      when 'owner_missing' then 'printf "waiting owner_missing\\n"'
      when 'geometry' then 'printf "ready %s 48 128 0 0 47 128 1\\nclients 1\\nclient %s 0 0 47 128 1\\n" "$owner" "$2"'
      when 'wrong_uid' then "printf 'ready #{Process.pid} 48 128 0 0 48 128 1\\nclients 1\\nclient %s 0 0 48 128 1\\n' \"$2\""
      when 'overflow' then "printf 'ready 9999999999999999999999 48 128 0 0 48 128 1\\nclients 1\\nclient %s 0 0 48 128 1\\n' \"$2\""
      when 'nul' then 'printf "ready %s 48 128 0 0 48 128 1\\nclients 1\\nclient %s 0 0 48 128 1\\n\\0extra" "$owner" "$2"'
      when 'oversized' then '/usr/bin/head -c 8192 /dev/zero'
      when 'timeout' then 'exec /bin/sleep 30'
      when 'exit_failure' then 'exit 7'
      when 'expires' then "if [ -s '#{count}' ]; then exec /bin/sleep 30; fi; printf x > '#{count}'; printf 'ready %s 48 128 0 0 48 128 1\\nclients 1\\nclient %s 0 0 48 128 1\\n' \"$owner\" \"$2\""
      else 'printf "ready %s 48 128 0 0 48 128 1\\nclients 1\\nclient %s 0 0 48 128 1\\n" "$owner" "$2"'
    end
    source=<<~HIBR
      strict
      [ "$EUID" = 65534 ] || exit 3
      [ "${BLIT_API_TOKEN-absent}" = absent ] || exit 4
      [ "$1" = desktop ] || exit 5
      case "$2" in blit-login-*) ;; *) exit 6 ;; esac
      for path in /proc/self/fd/*; do
        link=$(/usr/bin/readlink "$path") || continue
        case "$link" in socket:*|/dev/input/*|/dev/dri/*|/dev/pts/*) exit 8 ;; esac
      done
      owner=$(/bin/cat '#{owner_file}') || exit 1
      if [ -z "$owner" ]; then
        printf 'owner_missing' > '#{startup}'
        printf 'waiting owner_missing\n'
        exit 0
      fi
      #{action}
    HIBR
    File.write(observer,source)
    env={'SDL_VIDEODRIVER'=>'dummy','BLIT_RENDERER'=>'software',
      'LD_LIBRARY_PATH'=>File.join(ROOT,'build/sdl3-kms'),
      'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
      'BLIT_INPUT_ROOT'=>File.join(root,'input'),'BLIT_SETTINGS'=>'ignore',
      'BLIT_LOGIN_AUTOSTART'=>'1','BLIT_AUTH_TOKEN'=>'observer-admin',
      'BLIT_LOGIN_COMMAND'=>File.join(ROOT,'tools/tests/fixtures/login_identity.rb'),
      'BLIT_LOGIN_SCRIPT'=>'','BLIT_HIBR_OBSERVER'=>observer,
      'BLIT_GATE_IDENTITY_MODE'=>%w[shell waiting].include?(mode) ? mode : 'desktop',
      'BLIT_GATE_HOOK'=>hook,'BLIT_GATE_TARGET'=>target,
      'BLIT_GATE_RESULT'=>result,'BLIT_GATE_OWNER_FILE'=>owner_file}
    env['BLIT_GATE_OWNER_DELAY']='0.5' if mode=='geometry'
    if mode=='ready'
      env['BLIT_PANE_LAYOUT']='1x2'
      env['BLIT_LOGIN_PANE_ID']='pane-display-1-2'
    end
    log=File.join(root,'server.log')
    $pid=spawn('sudo','-n','env',*env.map{|k,v|"#{k}=#{v}"},BINARY,PORT.to_s,out:log,err:log)
    login=await{q=request('GET','/v1/login');q['desktop'] && q}
    expected=%w[ready secondary expires].include?(mode) ? 'ready' : %w[missing owner_missing].include?(mode) ? 'waiting' : %w[shell waiting].include?(mode) ? 'inactive' : 'error'
    login=await do
      q=request('GET','/v1/login');d=q['desktop']
      q if d['state']==expected && (expected!='waiting' || d['reason']!='awaiting_native_query') &&
        (mode!='missing' || d['reason']=='client_missing') &&
        (mode!='shell' || d['reason']=='shell_selected')
    end
    d=login['desktop'];check(login.dig('session','hibr_desktop','role')==(expected=='ready' ? 'primary' : 'none'),
      'native role projection differs from fresh verified association')
    check(File.read(startup)=='owner_missing','startup-ordering fixture was not exercised') if mode=='geometry'
    if expected=='ready'
      check(d.dig('owner','uid')==65534 && d.dig('owner','starttime_ticks').to_i>0,'owner credentials/lifetime absent')
      check(d.dig('owner','pid')==File.read(owner_file).to_i,'wrong native owner bound')
      check(d.dig('client','name')==d['client_name'],'named-client mismatch')
      check(d.dig('client','primary')==(mode!='secondary'),'observer forced primary selection')
      expected_clients=mode=='ready' ? 2 : 1
      check(d['clients'].length==expected_clients && d.dig('clients',0,'name')==d['client_name'],
        'native named-client collection missing exact login client or peer')
      check(d.dig('clients',0,'row')==(mode=='secondary' ? 48 : 0),
        'native named-client collection lost signed native geometry')
      check(d.dig('clients',1,'row')==-24 && d.dig('clients',1,'column')==-8,
        'native named-client collection lost a negative origin') if mode=='ready'
    elsif expected!='inactive'
      reasons={'missing'=>'client_missing','owner_missing'=>'owner_missing','geometry'=>'client_geometry_mismatch',
        'wrong_uid'=>'owner_identity_mismatch','overflow'=>'invalid_native_observation','nul'=>'invalid_native_observation',
        'oversized'=>'native_output_limit','timeout'=>'native_query_timeout','exit_failure'=>'native_query_failed'}
      check(d['reason']==reasons.fetch(mode),"wrong reason #{mode}:#{d}")
      check(d['owner'].nil? && d['client'].nil?,'unverified owner/client exposed')
    end
    if mode=='expires'
      await do
        q=request('GET','/v1/login')['desktop']
        q['state']!='ready'
      end
    end
    if mode=='ready'
      # Native login-owned API joins derive credentials, never root's namespace.
      # This child is a raw PTY oracle, not an actual Hibr desktop.
      child=File.join(root,'desktop');proof=File.join(root,'join-proof');input=File.join(root,'join-input')
      [proof,input].each{|p|File.write(p,'');File.chmod(0666,p)}
      File.write(child,<<~CHILD)
        #!/usr/bin/ruby
        require 'io/console';require 'json'
        STDIN.raw!
        File.write(#{proof.inspect},JSON.generate(uid:Process.uid,gid:Process.gid,
          home:ENV['HOME'],token:ENV['BLIT_API_TOKEN'],row:ENV['BLIT_HIBR_PANE_ROW'],column:ENV['BLIT_HIBR_PANE_COL']))
        STDOUT.write("\\e[?1003h\\e[?1006h");STDOUT.flush
        loop{File.open(#{input.inspect},'ab'){|f|f.write(STDIN.readpartial(4096))}}
      CHILD
      File.chmod(0755,child)
      body={command:child,arguments:['--session','desktop','--join'],pane_id:'pane-display-1-1',
        hibr_join_position:{row:-24,column:0}}
      refuse=->(payload,code,reason) do
        q=Net::HTTP::Post.new(BASE+'/v1/terminal-sessions');q['Authorization']='Bearer observer-admin'
        q['Content-Type']='application/json';q.body=JSON.generate(payload)
        r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:3){|h|h.request(q)}
        check(r.code.to_i==code && JSON.parse(r.body)['code']==reason,"wrong native join refusal:#{r.code}:#{r.body}")
      end
      refuse.call(body.merge(user:'root'),409,'hibr_desktop_identity_mismatch')
      refuse.call(body.merge(pane_id:'pane-display-1-2'),409,'hibr_join_pane_unavailable')
      [{row:true,column:0},{row:-24},{row:0,column:1.5},{row:0,column:32768},{row:0,column:0,extra:1}].each do |bad|
        refuse.call(body.merge(hibr_join_position:bad),400,'invalid_hibr_desktop_arguments')
      end
      refuse.call(body.merge(arguments:['--session','desktop']),409,'hibr_desktop_exists')
      refuse.call(body.merge(arguments:['--session','desktop','--standby']),400,'invalid_hibr_desktop_arguments')
      join=request('POST','/v1/terminal-sessions',body)
      await{File.size(proof)>0}
      details=JSON.parse(File.read(proof))
      check(details['uid']==65534 && details['gid']==65534 && details['token'].nil?,
        "join credentials/token boundary failed:#{details}")
      check(details['row']=='-24' && details['column']=='0','explicit native coordinates lost')
      await{request('GET',"/v1/terminal-sessions/#{join['id']}").dig('mouse_reporting','sgr')}
      pane=request('GET','/v1/topology')['panes'].find{|p|p['id']=='pane-display-1-1'}
      x=pane.dig('cell_grid','origin_x')+1;y=pane.dig('cell_grid','origin_y')+1
      request('POST','/v1/input/inject',{kind:'pointer_motion',x:x,y:y})
      request('POST','/v1/input/inject',{kind:'pointer_button',x:x,y:y,button:0,pressed:true})
      request('POST','/v1/input/inject',{kind:'pointer_button',x:x,y:y,button:0,pressed:false})
      await{File.binread(input).include?("\e[<0;1;1M") && File.binread(input).include?("\e[<0;1;1m")}
      check(request('GET',"/v1/terminal-sessions/#{join['id']}")['focused'],'join click focused another PTY')
      # A temporary query failure is not proof of logout and must not destroy it.
      refuse.call(body,409,'hibr_join_pane_unavailable')
      # Exercise the read-only physical acceptance client against this known
      # fixture, with quiet intervals longer than the association's TTL.
      out,err,status=Open3.capture3({'BLIT_LIVE'=>BASE.to_s,
        'BLIT_AUTH_TOKEN'=>'observer-admin','BLIT_EXPECT_UID'=>'65534',
        'BLIT_EVIDENCE_DIR'=>File.join(root,'read-only-probe')},
        'ruby',File.join(ROOT,'tools/probes/host_native_association.rb'))
      check(status.success? && out.include?('HOST_NATIVE_ASSOCIATION_PASS'),
        "read-only association acceptance client failed:#{err}")
      # Fresh evidence of owner replacement retires the old generation-bound
      # client, without killing either the login or the reported native owner.
      replacement=request('POST','/v1/terminal-sessions',
        {command:'/bin/sleep',arguments:['30'],user:'nobody',surface:{width:1,height:1}})
      File.write(owner_file,replacement['process_id'].to_s)
      await{request('GET','/v1/login').dig('desktop','owner','pid')==replacement['process_id']}
      await{request('GET','/v1/terminal-sessions')['sessions'].none?{|s|s['id']==join['id']}}
      check(request('GET','/v1/login').dig('session','id')==login.dig('session','id'),'owner replacement destroyed login')
      join=request('POST','/v1/terminal-sessions',body)
      # Cancellation while the next worker is blocked must remain responsive.
      File.write(observer,"exec /bin/sleep 30\n")
      await{request('GET','/v1/login').dig('desktop','query_in_flight')}
      await{request('GET','/v1/login').dig('desktop','state')!='ready'}
      check(request('GET','/v1/terminal-sessions')['sessions'].any?{|s|s['id']==join['id']},
        'temporary observation failure destroyed existing client')
      refuse.call(body,409,'hibr_desktop_owner_missing')
    end
    started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
    request('DELETE','/v1/login')
    check(Process.clock_gettime(Process::CLOCK_MONOTONIC)-started<1,'logout blocked on observer')
    check(request('GET','/v1/login').dig('desktop','state')=='inactive','association survived logout')
    if mode=='ready'
      await{request('GET','/v1/terminal-sessions')['sessions'].none?{|s|s['id']==join['id']}}
      check(%w[starting running].include?(request('GET',"/v1/terminal-sessions/#{replacement['id']}")['state']),
        'logout signalled native owner instead of only its client')
      request('DELETE',"/v1/terminal-sessions/#{replacement['id']}")
      File.write(observer,source)
      request('POST','/v1/login',{})
      fresh=await do
        q=request('GET','/v1/login');q if q.dig('desktop','state')=='ready'
      end
      check(fresh.dig('session','id')!=login.dig('session','id'),'login generation reused')
      check(fresh.dig('desktop','client_name')!=d['client_name'],'native client name reused')
      check(fresh.dig('desktop','owner','pid')!=d.dig('owner','pid'),'cancelled old query rebound a new login')
      request('DELETE','/v1/login')
    end
    stop
  rescue StandardError
    warn "mode=#{mode} last=#{login && login['desktop']}";warn File.read(log) if File.file?(log);raise
  end
end
check($max_http<0.5,"native worker stalled API:#{$max_http}s")
puts "LOGIN_DESKTOP_OBSERVER_PASS native_payloads=synthetic quiet_refresh=verified logout=nonblocking max_api_ms=#{($max_http*1000).round(2)}"
