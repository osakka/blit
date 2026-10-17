#!/usr/bin/env ruby
# ADR 0124 physical-host configuration check. Explicitly disruptive when
# BLIT_ALLOW_RESTART=1; never starts a desktop or join and never uses SSH.
require 'json'
require 'net/http'
require 'fileutils'

BASE = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
TOKEN = ENV.fetch('BLIT_AUTH_TOKEN')
PANE = ENV.fetch('BLIT_LOGIN_PANE')
EVIDENCE = ENV.fetch('BLIT_EVIDENCE_DIR', 'diagnostics/policy-537/physical')
FileUtils.mkdir_p(EVIDENCE)
def request(method, path, body=nil, revision=nil)
  q={'GET'=>Net::HTTP::Get,'PUT'=>Net::HTTP::Put,'POST'=>Net::HTTP::Post,
     'PATCH'=>Net::HTTP::Patch}.fetch(method).new(BASE+path)
  q['Authorization']="Bearer #{TOKEN}";q['Content-Type']='application/json'
  q['If-Match']=revision.to_s if revision
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,open_timeout:2,read_timeout:5){|h|h.request(q)}
  raise "#{method} #{path}:HTTP#{r.code}" unless r.code.start_with?('2')
  JSON.parse(r.body)
end
def get(path) = request('GET',path)
def record(name, value)
  File.write(File.join(EVIDENCE,"#{name}.json"),JSON.pretty_generate(value))
end
def check(value, message)
  raise message unless value
end
def geometry(topology)
  {layout:topology['layout'],outputs:topology['outputs'],
   panes:topology['panes'].map{|p|p.reject{|k,_|k=='surface_id'}}}
end
before=get('/v1/settings')
check(before.dig('effective','login').is_a?(Hash),'server lacks the login policy contract; no changes made')
topology=get('/v1/topology')
check(topology['panes'].any?{|p|p['id']==PANE},'requested pane absent; no changes made')
render=get('/v1/diagnostics').fetch('render')
font=get('/v1/fonts').fetch('active')
before_login=get('/v1/login')
record('before-settings',before);record('before-topology',topology)
record('before-login',before_login)
document=before.fetch('document')
document['login']={enabled:true,preferred_pane_id:PANE,fallback:'first_available'}
saved=request('PUT','/v1/settings',{document:document},before['revision'])
record('save-receipt',saved)
check(saved.dig('storage','state')=='clean','save not confirmed clean')
check(saved.dig('storage','saved_revision')==saved['revision'],'saved revision differs from receipt')
saved_policy=get('/v1/settings').dig('document','login')
check(saved_policy=={'enabled'=>true,'preferred_pane_id'=>PANE,'fallback'=>'first_available'},'policy readback differs')
unless ENV['BLIT_ALLOW_RESTART']=='1'
  puts 'HOST_LOGIN_POLICY_SAVED restart=not-requested; use BLIT_ALLOW_RESTART=1 for disruptive restart verification'
  exit
end
record('restart-receipt',request('POST','/v1/control/restart',{}))
deadline=Process.clock_gettime(Process::CLOCK_MONOTONIC)+45
after=nil;login=nil
loop do
  begin
    after=get('/v1/settings');login=get('/v1/login')
    break if login.dig('session','pane_id')==PANE &&
      login.dig('session','process_id')!=before_login.dig('session','process_id') &&
      after.dig('document','login')==saved_policy
  rescue IOError, SystemCallError, Net::OpenTimeout, Net::ReadTimeout
    # The supervised restart briefly closes the listener. Never resend POST.
  end
  raise 'restart did not restore the configured login pane' if Process.clock_gettime(Process::CLOCK_MONOTONIC)>=deadline
  sleep 0.1
end
check(after.dig('storage','state')=='clean','restored configuration is not clean')
check(!after.dig('effective','login_restart_required'),'restored policy still needs restart')
check(login['preferred_pane_id']==PANE,'restored preference differs')
check(login['pane_id']==PANE,'assigned login pane differs')
sessions=get('/v1/terminal-sessions').fetch('sessions')
check(sessions.size==1 && sessions.first['login_manager'],'restart created unexpected terminal sessions')
request('PATCH',"/v1/terminal-sessions/#{login['session']['id']}",{focused:true})
after_topology=get('/v1/topology');after_render=get('/v1/diagnostics').fetch('render')
check(geometry(after_topology)==geometry(topology),'restart changed topology geometry')
%w[video_driver renderer presentation_sync font_scale surface_inset_px].each do |key|
  check(after_render[key]==render[key],"restart changed #{key}")
end
check(get('/v1/fonts')['active']==font,'restart changed font identity or scale')
record('after-settings',after);record('after-login',get('/v1/login'))
record('after-topology',after_topology);record('after-diagnostics',get('/v1/diagnostics'))
record('after-version',get('/v1/version'))
puts "HOST_LOGIN_POLICY_PASS pane=#{PANE} save=clean restart=verified geometry=unchanged renderer=#{after_render['renderer']}"
