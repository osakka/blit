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
      when 'secondary' then 'printf "ready %s 96 128 48 0 48 128 0\\n" "$owner"'
      when 'missing' then 'printf "waiting client_missing\\n"'
      when 'owner_missing' then 'printf "waiting owner_missing\\n"'
      when 'geometry' then 'printf "ready %s 48 128 0 0 47 128 1\\n" "$owner"'
      when 'wrong_uid' then "printf 'ready #{Process.pid} 48 128 0 0 48 128 1\\n'"
      when 'overflow' then "printf 'ready 9999999999999999999999 48 128 0 0 48 128 1\\n'"
      when 'nul' then 'printf "ready %s 48 128 0 0 48 128 1\\n\\0extra" "$owner"'
      when 'oversized' then '/usr/bin/head -c 2048 /dev/zero'
      when 'timeout' then 'exec /bin/sleep 30'
      when 'exit_failure' then 'exit 7'
      when 'expires' then "if [ -s '#{count}' ]; then exec /bin/sleep 30; fi; printf x > '#{count}'; printf 'ready %s 48 128 0 0 48 128 1\\n' \"$owner\""
      else 'printf "ready %s 48 128 0 0 48 128 1\\n" "$owner"'
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
    d=login['desktop'];check(login.dig('session','hibr_desktop','role')=='none','native observation enabled legacy projection')
    check(File.read(startup)=='owner_missing','startup-ordering fixture was not exercised') if mode=='geometry'
    if expected=='ready'
      check(d.dig('owner','uid')==65534 && d.dig('owner','starttime_ticks').to_i>0,'owner credentials/lifetime absent')
      check(d.dig('owner','pid')==File.read(owner_file).to_i,'wrong native owner bound')
      check(d.dig('client','name')==d['client_name'],'named-client mismatch')
      check(d.dig('client','primary')==(mode!='secondary'),'observer forced primary selection')
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
      # Exercise the read-only physical acceptance client against this known
      # fixture, with quiet intervals longer than the association's TTL.
      out,err,status=Open3.capture3({'BLIT_LIVE'=>BASE.to_s,
        'BLIT_AUTH_TOKEN'=>'observer-admin','BLIT_EXPECT_UID'=>'65534',
        'BLIT_EVIDENCE_DIR'=>File.join(root,'read-only-probe')},
        'ruby',File.join(ROOT,'tools/probes/host_native_association.rb'))
      check(status.success? && out.include?('HOST_NATIVE_ASSOCIATION_PASS'),
        "read-only association acceptance client failed:#{err}")
      # Cancellation while the next worker is blocked must remain responsive.
      File.write(observer,"exec /bin/sleep 30\n")
      await{request('GET','/v1/login').dig('desktop','query_in_flight')}
    end
    started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
    request('DELETE','/v1/login')
    check(Process.clock_gettime(Process::CLOCK_MONOTONIC)-started<1,'logout blocked on observer')
    check(request('GET','/v1/login').dig('desktop','state')=='inactive','association survived logout')
    if mode=='ready'
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
