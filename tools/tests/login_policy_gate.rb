#!/usr/bin/env ruby
# ADR 0124: exercise the service API, its durable file, and boot selection.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(19486)
BASE = URI("http://127.0.0.1:#{PORT}")
ROOT_OK = Process.uid.zero? || system('sudo', '-n', 'true', out: File::NULL, err: File::NULL)
def request(method, path, body=nil, revision=nil)
  q = {'GET'=>Net::HTTP::Get,'PUT'=>Net::HTTP::Put,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE+path)
  q['Content-Type']='application/json'; q.body=JSON.pretty_generate(body) if body
  q['If-Match']=revision.to_s if revision
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:5){|h|h.request(q)}
  [r.code.to_i, JSON.parse(r.body)]
end
def get(path)
  status, body=request('GET',path); raise "GET #{path}:#{status}" unless status==200; body
end
def check(value, message)
  raise message unless value
end
def durable_bytes(path)
  return File.binread(path) if File.readable?(path)
  IO.popen(['sudo','-n','cat',path], &:read)
end
def start(root, extra={})
  env={**gate_display_env,
       'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
       'BLIT_INPUT_ROOT'=>File.join(root,'input'), 'BLIT_SETTINGS_PATH'=>File.join(root,'settings.json'),
       'BLIT_PANE_LAYOUT'=>'1x2','BLIT_LOGIN_AUTOSTART'=>'0',
       'BLIT_LOGIN_PANE_ID'=>'pane-display-1-1',
       'BLIT_LOGIN_COMMAND'=>'/bin/cat','BLIT_LOGIN_SCRIPT'=>''}.merge(extra)
  command=ROOT_OK && !Process.uid.zero? ? ['sudo','-n','env',*env.map{|k,v|"#{k}=#{v}"},BINARY,PORT.to_s] : [env,BINARY,PORT.to_s]
  $pid=spawn(*command,out:File::NULL,err:File::NULL)
  Timeout.timeout(15) do
    loop do
      begin; break if request('GET','/v1/version').first==200; rescue SystemCallError, IOError; end
      sleep 0.05
    end
  end
end
def stop
  return unless $pid
  if ROOT_OK && !Process.uid.zero?
    system('sudo','-n','kill','-TERM',$pid.to_s,out:File::NULL,err:File::NULL)
  else
    Process.kill('TERM',$pid) rescue nil
  end
  Process.wait($pid) rescue nil
  $pid=nil
end
at_exit { stop }
Dir.mktmpdir('blit-login-policy') do |root|
  Dir.mkdir(File.join(root,'input'))
  path=File.join(root,'settings.json')
  start(root)
  state=get('/v1/settings')
  document=state.fetch('document')
  document['future_extension']={'preserved'=>true,'enabled'=>false}
  document['login']={'enabled'=>true,'preferred_pane_id'=>'pane-display-1-2','fallback'=>'wait'}
  status, saved=request('PUT','/v1/settings',{document:document},state['revision'])
  check(status==200, 'valid login policy refused')
  check(get('/v1/login')['enabled']==false, 'save unexpectedly started login')
  before=durable_bytes(path)
  state=get('/v1/settings')
  Dir.mkdir(path+'.new') # Fail the real atomic writer without a test-only seam.
  changed=Marshal.load(Marshal.dump(document));changed['login']['enabled']=false
  status, failed=request('PUT','/v1/settings',{document:changed},state['revision'])
  check(status==503 && failed['code']=='settings_write_failed','disk failure reported as successful save')
  check(durable_bytes(path)==before,'failed replacement destroyed previous durable document')
  check(get('/v1/settings').dig('storage','state')=='error','disk failure not observable')
  check(saved.dig('storage','state')=='clean','successful save has no clean durability receipt')
  check(saved.dig('document','future_extension','preserved')==true,'opaque future settings lost on save')
  check(saved.dig('effective','login_restart_required')==true,
        'saved startup policy missing restart flag')
  check(get('/v1/settings').dig('effective','login','enabled')==false,
        'failed save changed the running login policy')
  stop
  Dir.rmdir(path+'.new')
  # The old durable preference must win over the conflicting boot default.
  start(root)
  if ROOT_OK
    Timeout.timeout(5){sleep 0.05 until get('/v1/login')['session']}
    login=get('/v1/login')
    check(login['pane_id']=='pane-display-1-2','restart lost lower preference')
    check(login['preferred_pane_id']=='pane-display-1-2','preference not observable')
  end
  check(!get('/v1/settings').dig('effective','login_restart_required'),'restored policy still pending restart')
  state=get('/v1/settings');document=state['document'];before=durable_bytes(path)
  [false, {'enabled'=>'yes'}, {'preferred_pane_id'=>''},
   {'preferred_pane_id'=>'bad/path'}, {'preferred_pane_id'=>'x'*128},
   {'fallback'=>'guess'}, {'password'=>'secret'}].each do |invalid|
    bad=Marshal.load(Marshal.dump(document));bad['login']=invalid
    status,_=request('PUT','/v1/settings',{document:bad},state['revision'])
    check(status==400, "invalid login accepted:#{invalid.inspect}")
    check(durable_bytes(path)==before && get('/v1/settings')['revision']==state['revision'], 'invalid policy changed state')
  end
  status, problem=request('PUT','/v1/settings',{document:document},state['revision']-1)
  check(status==409,'stale configuration revision accepted')
  # Missing preferred target stays configured rather than being discarded.
  state=get('/v1/settings');document=state['document']
  document['login']['preferred_pane_id']='pane-unavailable'
  status,_=request('PUT','/v1/settings',{document:document},state['revision']);check(status==200,'absent target refused')
  stop;start(root)
  login=get('/v1/login')
  check(login['session'].nil?,'wait policy started a fallback')
  check(login['preferred_pane_id']=='pane-unavailable','missing preference discarded')
  state=get('/v1/settings');document=state['document'];document['login']['fallback']='first_available'
  status,_=request('PUT','/v1/settings',{document:document},state['revision']);check(status==200,'fallback save failed')
  stop;start(root)
  if ROOT_OK
    # A preferred pane that is not there yet is waited for a moment -- its
    # monitor may still be waking -- before the first available takes its
    # place (BLIT_LOGIN_PREFERRED_GRACE_US, 10 s): a primary chosen by
    # whichever output was discovered first moved from one boot to the next.
    sleep 3
    check(get('/v1/login')['session'].nil?,'fallback did not wait for the preferred pane')
    Timeout.timeout(20){sleep 0.1 until get('/v1/login')['session']}
    login=get('/v1/login')
    check(login['pane_id']=='pane-display-1-1','fallback did not use available pane')
    check(login['preferred_pane_id']=='pane-unavailable','fallback overwrote intent')
  end
  stop;start(root,{'BLIT_SETTINGS'=>'ignore'})
  check(get('/v1/settings').dig('storage','state')=='disabled','ignore storage status incorrect')
  check(!get('/v1/login')['enabled'],'ignore did not restore boot default')
  stop
  start(root)
  state=get('/v1/settings')
  # Reset does not stop an active login; the removal is explicit and durable.
  status, reset=request('DELETE','/v1/settings',nil,state['revision'])
  check(status==200 && !File.exist?(path),'reset failed to remove saved configuration')
  check(reset.dig('storage','state')=='clean','reset durability not reported')
  stop;start(root)
  check(!get('/v1/login')['enabled'],'reset did not restore boot login default')
  stop
end
puts "LOGIN_POLICY_PASS save_error=honest old_file=retained restart=preserved fallback=explicit,after_grace root_assignment=#{ROOT_OK ? 'tested' : 'not-tested'}"
