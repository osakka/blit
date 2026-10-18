#!/usr/bin/env ruby
# Actual installed Hibr standby + native hold + Blit Apply over HTTP, using
# an isolated native socket/state namespace. Login authentication is a fixture.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'fileutils'
require 'open3'
require_relative 'gate_port'
ROOT=File.expand_path('../..',__dir__)
BINARY=ENV.fetch('BLIT_GATE_BINARY',File.join(ROOT,'build/blit/blit-server'))
PORT=gate_port(ENV.fetch('BLIT_AUXILIARY_NATIVE_PORT','19494'))
BASE=URI("http://127.0.0.1:#{PORT}")
LOAD='mod load pty; mod load term; mod load hold; '
ACTUAL_DESKTOP=ENV['BLIT_NATIVE_AUXILIARY_DESKTOP']=='1'
def check(ok,msg) = (raise(msg) unless ok)
def request(method,path,body=nil)
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,'PATCH'=>Net::HTTP::Patch,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE+path)
  q['Authorization']='Bearer native-auxiliary-admin';q['Content-Type']='application/json'
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:5){|h|h.request(q)}
  check(r.code.start_with?('2'),"HTTP #{r.code}:#{r.body}")
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
def await(message)
  Timeout.timeout(15,RuntimeError,"timeout:#{message}") do
    loop do
      begin
        v=yield;return v if v
      rescue Errno::ECONNREFUSED
      end
      sleep 0.05
    end
  end
end
def native_process_tree(pid)
  ticks=File.read("/proc/#{pid}/stat").split(') ',2).last.split[19]
  children=File.read("/proc/#{pid}/task/#{pid}/children").split.map(&:to_i)
  [[pid,ticks]] + children.flat_map { |child| native_process_tree(child) }
rescue Errno::ENOENT,Errno::ESRCH
  []
end
unless system('sudo','-n','true',out:File::NULL,err:File::NULL)
  puts 'AUXILIARY_NATIVE_SKIP root_fixture=required';exit 77
end
Dir.mktmpdir('blit-native-auxiliary') do |root|
  pid=nil
  Dir.mkdir(File.join(root,'input'))
  env={'TMPDIR'=>root}
  target=File.join(root,'target.hibr');hook=File.join(root,'hook.hibr')
  result=File.join(root,'result');File.write(result,'')
  FileUtils.cp(File.join(ROOT,'hibr/blit-login-session.hibr'),hook)
  File.write(target,<<~HIBR)
    strict
    export TMPDIR='#{root}'
    mod load pty; mod load term; mod load hold
    name=
    for arg in "$@"; do case "$arg" in --name=*) name=${arg#--name=} ;; esac; done
    [ -n "$name" ] || exit 2
    present=0
    sessions := hold list || exit 1
    while read -r base status pid size extra; do
      [ "$base" != desktop ] || present=1
    done <<< "$sessions"
    [ "$present" = 1 ] || hold new -d desktop /bin/sleep 90 || exit 1
    hold attach -n "$name" desktop
  HIBR
  if ACTUAL_DESKTOP
    File.write(target,<<~HIBR)
      strict
      export TMPDIR='#{root}'
      export XDG_STATE_HOME='#{root}/state'
      export XDG_CONFIG_HOME='#{root}/config'
      export XDG_DATA_HOME='#{root}/data'
      export DT_LOG='#{root}/desktop.log'
      exec "$HIBR" '#{ROOT}/hibr/blit-session.hibr' "$@"
    HIBR
  end
  File.chmod(0755,target)
  launcher=File.join(root,'blit-session.hibr')
  File.write(launcher,<<~HIBR)
    strict
    export TMPDIR='#{root}'
    export XDG_STATE_HOME='#{root}/state'
    export XDG_CONFIG_HOME='#{root}/config'
    export XDG_DATA_HOME='#{root}/data'
    export DT_LOG='#{root}/auxiliary.log'
    exec "$HIBR" '#{ROOT}/hibr/blit-session.hibr' "$@"
  HIBR
  observer=File.join(root,'observer.hibr');placer=File.join(root,'placer.hibr')
  {observer=>'blit-desktop-observe.hibr',placer=>'blit-desktop-place.hibr'}.each do |file,script|
    File.write(file,"strict\nexport TMPDIR='#{root}'\nexec \"$HIBR\" '#{ROOT}/hibr/#{script}' \"$@\"\n")
  end
  server_env={'SDL_VIDEODRIVER'=>'dummy','BLIT_RENDERER'=>'software','LD_LIBRARY_PATH'=>File.join(ROOT,'build/sdl3-kms'),
    'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),'BLIT_INPUT_ROOT'=>File.join(root,'input'),
    'BLIT_SETTINGS'=>'ignore','BLIT_AUTH_TOKEN'=>'native-auxiliary-admin','BLIT_PANE_LAYOUT'=>'1x2',
    'BLIT_LOGIN_PANE_ID'=>'pane-display-1-2','BLIT_LOGIN_AUTOSTART'=>'1',
    'BLIT_LOGIN_COMMAND'=>File.join(ROOT,'tools/tests/fixtures/login_identity.rb'),'BLIT_LOGIN_SCRIPT'=>'',
    'BLIT_GATE_IDENTITY_MODE'=>'desktop','BLIT_GATE_HOOK'=>hook,'BLIT_GATE_TARGET'=>target,
    'BLIT_GATE_RESULT'=>result,'BLIT_GATE_UID'=>Process.uid.to_s,'BLIT_GATE_GID'=>Process.gid.to_s,
    'BLIT_HIBR_OBSERVER'=>observer,'BLIT_HIBR_PLACER'=>placer,'BLIT_AUXILIARY_LAUNCHER'=>launcher}
  log=File.join(root,'server.log')
  begin
    pid=spawn('sudo','-n','env',*server_env.map{|k,v|"#{k}=#{v}"},BINARY,PORT.to_s,out:log,err:[:child,:out])
    login=await('actual native login association'){q=request('GET','/v1/login');q if q.dig('desktop','state')=='ready'}
    output=request('GET','/v1/displays')['displays'].first['name']
    check(login.dig('desktop','client_name')=="blit-#{output}-p2",'primary native name is not stable output/ordinal identity')
    request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[{'output'=>output,'pane_ordinal'=>1}]})
    b=await('actual standby started'){q=request('GET','/v1/auxiliary-displays')['bindings'][0];q if q['session_id']}
    request('PUT','/v1/fonts',{'scale'=>1.1}) if ENV['BLIT_NATIVE_AUXILIARY_RESIZE']=='1'
    # The held test workload is sleep, so there is no desktop auto-invite loop.
    # Enter is Hibr's documented standby join action, delivered through the API.
    unless ACTUAL_DESKTOP
      sleep 0.5
      request('POST',"/v1/terminal-sessions/#{b['session_id']}/input",{'data'=>"\r"})
    end
    joined=await('actual standby join and native placement'){q=request('GET','/v1/auxiliary-displays')['bindings'][0];q if q['state']=='joined'}
    check(joined['client_name']=="blit-#{output}-p1",'auxiliary native name is not stable output/ordinal identity')
    native=request('GET','/v1/login')['desktop']
    a=native['clients'].find{|c|c['name']==joined['client_name']}
    check(a && a['row']-native['client']['row']==-a['rows'],'actual native placement differs from upper/lower pane grid')
    check(native['owner']['pid']==login['desktop']['owner']['pid'],'Apply replaced desktop owner')
    # Restart Blit itself, retaining the isolated held owner. Both fresh PTY
    # clients must reuse their names, not create #2 aliases or new preferences.
    unless ACTUAL_DESKTOP
      request('POST','/v1/control/restart')
      sleep 0.5
      login_after=await('native owner association after server restart') do
        q=request('GET','/v1/login')
        q if q.dig('desktop','state')=='ready' &&
          q.dig('session','process_id')!=login.dig('session','process_id') &&
          q.dig('desktop','client_name')==login.dig('desktop','client_name')
      end
      check(login_after.dig('desktop','owner','pid')==native['owner']['pid'],'restart replaced held owner')
      request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[{'output'=>output,'pane_ordinal'=>1}]})
      b=await('standby after restart'){q=request('GET','/v1/auxiliary-displays')['bindings'][0];q if q['session_id']}
      sleep 0.5
      request('POST',"/v1/terminal-sessions/#{b['session_id']}/input",{'data'=>"\r"})
      after=await('stable native rejoin'){q=request('GET','/v1/auxiliary-displays')['bindings'][0];q if q['state']=='joined'}
      check(after['client_name']==joined['client_name'],'restart changed auxiliary identity')
      names=request('GET','/v1/login').fetch('desktop').fetch('clients').map{|c|c['name']}
      check(names.sort==[login.dig('desktop','client_name'),joined['client_name']].sort,'restart accumulated duplicate native names')
      joined=after
    end
    request('PATCH',"/v1/terminal-sessions/#{b['session_id']}",{'pane_id'=>'pane-display-1-2'})
    await('explicit Blit pane reassignment conflict'){request('GET','/v1/auxiliary-displays')['bindings'][0]['reason']=='managed_pane_assignment_changed'}
    request('PATCH',"/v1/terminal-sessions/#{b['session_id']}",{'pane_id'=>'pane-display-1-1'})
    await('explicit pane restore'){request('GET','/v1/auxiliary-displays')['bindings'][0]['state']=='joined'}
    _,err,st=Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold move desktop #{joined['client_name']} 7 11")
    check(st.success?,"native user move failed:#{err}")
    sleep 1.4
    check(request('GET','/v1/auxiliary-displays')['bindings'][0]['state']=='joined','user move invalidated confirmed join')
    _,err,st=Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold drop desktop #{joined['client_name']}")
    check(st.success?,"native release failed:#{err}")
    await('actual released standby remains alive'){request('GET','/v1/auxiliary-displays')['bindings'][0]['reason']=='native_client_absent_release_unknown'}
    request('POST',"/v1/terminal-sessions/#{b['session_id']}/input",{'data'=>'q'})
    await('standby quit without respawn'){request('GET','/v1/auxiliary-displays')['bindings'][0]['state']=='stopped'}
    request('DELETE','/v1/login')
    await('owner lifecycle cleanup'){request('GET','/v1/terminal-sessions')['sessions'].none?{|s|s['id']==b['session_id']}}
    puts "AUXILIARY_NATIVE_PASS standby=installed_hibr desktop=#{ACTUAL_DESKTOP ? 'installed' : 'sleep_fixture'} invitation=#{ACTUAL_DESKTOP ? 'automatic' : 'enter'} placement=confirmed stable_names=yes restart=#{ACTUAL_DESKTOP ? 'separate' : 'same_owner_no_aliases'} user_move=preserved release=alive quit=stopped logout=cleaned"
  rescue Exception
    warn JSON.pretty_generate(request('GET','/v1/auxiliary-displays')) rescue nil
    warn JSON.pretty_generate(request('GET','/v1/login')) rescue nil
    warn File.read(log) if File.exist?(log)
    warn File.read(File.join(root,'desktop.log')) if File.exist?(File.join(root,'desktop.log'))
    raise
  ensure
    owned=defined?(login) && login ? native_process_tree(login['desktop']['owner']['pid']) : []
    request('POST','/v1/control/shutdown') rescue nil
    Timeout.timeout(12){Process.wait(pid)} if pid
    Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold kill desktop")
    Timeout.timeout(5) do
      loop do
        alive=owned.select do |native_pid,ticks|
          begin
            fields=File.read("/proc/#{native_pid}/stat").split(') ',2).last.split
            fields[19]==ticks && fields[0]!='Z'
          rescue Errno::ENOENT,Errno::ESRCH
            false
          end
        end
        break if alive.empty?
        sleep 0.05
      end
    end
  end
end
