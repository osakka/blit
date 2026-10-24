#!/usr/bin/env ruby
# ADR 0125: private inherited capability, real credential drop and Hibr hook.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'fileutils'
require 'socket'
require 'fcntl'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(19487)
BASE = URI("http://127.0.0.1:#{PORT}")
unless Process.uid.zero? || system('sudo', '-n', 'true', out:File::NULL, err:File::NULL)
  puts 'LOGIN_IDENTITY_SKIP root_fixture=required'; exit 77
end
def request(method, path, body=nil)
  q = {'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE+path)
  q['Authorization']='Bearer identity-gate-admin';q['Content-Type']='application/json'
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:3){|h|h.request(q)}
  raise "HTTP #{r.code}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
def check(value, message) = (raise(message) unless value)
def until_ready
  Timeout.timeout(12) do
    loop do
      begin; return yield; rescue Errno::ECONNREFUSED, IOError; end
      sleep 0.05
    end
  end
end
def stop
  return unless $pid
  system('sudo','-n','kill','-TERM',$pid.to_s,out:File::NULL,err:File::NULL)
  Timeout.timeout(10){Process.wait($pid)} rescue nil
  $pid=nil
end
at_exit { stop }
Dir.mktmpdir('blit-login-identity') do |root|
  File.chmod(0755, root); Dir.mkdir(File.join(root,'input'))
  fixture=File.join(ROOT,'tools/tests/fixtures/login_identity.rb')
  target=File.join(root,'target.hibr')
  hook=File.join(root,'blit-login-session.hibr')
  FileUtils.cp(File.join(ROOT,'tools/tests/fixtures/login_identity_target.hibr'),target)
  FileUtils.cp(File.join(ROOT,'hibr/blit-login-session.hibr'),hook)
  File.chmod(0755,target);File.chmod(0755,hook)
  modes=%w[desktop shell root invalid oversized rights foreign waiting custom]
  modes.each do |mode|
    result=File.join(root,"#{mode}.result")
    File.write(result,'');File.chmod(0666,result)
    env={**gate_display_env,'BLIT_RENDERER'=>'software',
      'LD_LIBRARY_PATH'=>File.join(ROOT,'build/sdl3-kms'),
      'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
      'BLIT_INPUT_ROOT'=>File.join(root,'input'),'BLIT_SETTINGS'=>'ignore',
      'BLIT_LOGIN_AUTOSTART'=>'1','BLIT_LOGIN_COMMAND'=>fixture,
      'BLIT_LOGIN_SCRIPT'=>'','BLIT_AUTH_TOKEN'=>'identity-gate-admin',
      'BLIT_GATE_IDENTITY_MODE'=>mode,'BLIT_GATE_TARGET'=>target,'BLIT_GATE_RESULT'=>result}
    env['BLIT_GATE_HOOK']=hook
    env['BLIT_LOGIN_SESSION_COMMAND']='/bin/false' if mode=='custom'
    log=File.join(root,"#{mode}.log")
    $pid=spawn('sudo','-n','env',*env.map{|k,v|"#{k}=#{v}"},BINARY,PORT.to_s,out:log,err:log)
    login=until_ready{request('GET','/v1/login')}
    check(login['identity'].is_a?(Hash),'login identity API absent')
    check(login['desktop'].is_a?(Hash),'native login association API absent')
    expected=%w[desktop shell].include?(mode) ? 'verified' : (%w[waiting custom].include?(mode) ? (mode=='waiting' ? 'waiting' : 'unavailable') : 'rejected')
    Timeout.timeout(8) do
      loop do
        login=request('GET','/v1/login')
        break if login.dig('identity','state')==expected
        sleep 0.03
      end
    end
    identity=login.fetch('identity')
    if expected=='verified'
      check(identity['uid']==65534 && identity['gid']==65534,'identity not the real dropped credentials')
      check(identity['session_kind']==mode && identity['sender_pid']>1,'selected kind or sender PID missing')
      check(login.dig('session','hibr_desktop','role')=='none','identity incorrectly claims desktop ownership')
      Timeout.timeout(8){sleep 0.03 until !File.read(result).empty?}
      check(File.read(result).strip=='0:absent:65534',"capability/admin-token leak: #{File.read(result).strip}")
    else
      check(%w[uid gid sender_pid session_kind].all?{|k|identity[k].nil?},'unverified metadata exposed as trusted')
      reasons={'root'=>'root_sender','invalid'=>'invalid_message',
               'oversized'=>'truncated_message','rights'=>'invalid_credentials',
               'foreign'=>'foreign_sender','custom'=>'custom_session_command',
               'waiting'=>'awaiting_authenticated_hook'}
      check(identity['reason']==reasons.fetch(mode),'wrong rejection/unavailable reason')
    end
    if mode=='waiting'
      # Public PTY output/input is not an identity-registration channel.
      ordinary=request('POST','/v1/terminal-sessions',{
        command:'/bin/cat',surface:{width:20,height:4}})
      request('POST',"/v1/terminal-sessions/#{ordinary['id']}/input",{data:"desktop\n"})
      Timeout.timeout(3) do
        loop do
          cells=request('GET',"/v1/surfaces/#{ordinary['surface_id']}/cells").fetch('cells')
          break if cells.select{|c|c['y']==0}.sort_by{|c|c['x']}.map{|c|c['text']}.join.include?('desktop')
          sleep 0.03
        end
      end
      check(request('GET','/v1/login')['identity']==identity,'ordinary PTY changed login identity')
      request('DELETE',"/v1/terminal-sessions/#{ordinary['id']}")
    end
    request('DELETE','/v1/login')
    check(request('GET','/v1/login').dig('identity','state')=='unavailable','identity survived login destruction')
    request('POST','/v1/login',{})
    fresh=request('GET','/v1/login')
    check(fresh.dig('session','id')!=login.dig('session','id'),'login replacement retained old terminal')
    stop
  rescue StandardError => error
    warn "mode=#{mode} last_identity=#{login && login['identity']}"
    warn File.read(log) if log && File.file?(log)
    raise error
  end
  # A missing acknowledgement must not hang authenticated login forever.
  receiver,sender=Socket.pair(:UNIX,:DGRAM,0)
  # Ruby creates nonblocking sockets; the production C sender is blocking.
  sender.fcntl(Fcntl::F_SETFL,sender.fcntl(Fcntl::F_GETFL) & ~Fcntl::O_NONBLOCK)
  sender.setsockopt(Socket::SOL_SOCKET,Socket::SO_RCVTIMEO,[5,0].pack('l!l!'))
  inode=File.readlink("/proc/self/fd/#{sender.fileno}")
  result=File.join(root,'timeout.result')
  started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
  hook_pid=spawn('/usr/bin/env','-i',"BLIT_HIBR_SESSION_LAUNCHER=#{target}",
    "BLIT_GATE_RESULT=#{result}","BLIT_GATE_SENDER_INODE=#{inode}",
    '/usr/bin/hibr',hook,'--blit-handoff-fd',sender.fileno.to_s,'desktop',
    sender.fileno=>sender,out:File::NULL,err:File::NULL)
  sender.close
  check(Timeout.timeout(2){receiver.recv(32)}=='desktop','hook did not send exactly one datagram')
  Timeout.timeout(8){sleep 0.03 until File.exist?(result) && !File.read(result).empty?}
  check(Process.clock_gettime(Process::CLOCK_MONOTONIC)-started>=4.5,'receive deadline not applied')
  check(File.read(result).strip=="0:absent:#{Process.uid}",'timeout leaked channel or prevented session start')
ensure
  receiver&.close;sender&.close unless sender&.closed?
  Process.kill('TERM',hook_pid) rescue nil
  Process.wait(hook_pid) rescue nil
end
puts 'LOGIN_IDENTITY_PASS kinds=2 credentials=dropped rejection=5 custom=unchanged lifecycle=revoked timeout=bounded'
