#!/usr/bin/env ruby
# Read only Blit's service journal through a temporary API-created PTY.
# Collect scrollback AND visible cells; never replace an occupied pane.
require 'json'
require 'net/http'
require 'timeout'
base=URI(ENV.fetch('BLIT_LIVE','http://192.168.10.54:8080'))
token=ENV.fetch('BLIT_AUTH_TOKEN')
since=ENV['BLIT_JOURNAL_SINCE']
raise 'invalid journal bound' if since && (since.empty? || since.bytesize>200)
api=lambda do |method,path,body=nil|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PATCH'=>Net::HTTP::Patch,
     'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base+path)
  q['Authorization']="Bearer #{token}"
  q['Content-Type']='application/json'
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(base.host,base.port,open_timeout:2,read_timeout:5){|h|h.request(q)}
  raise "#{method} #{path}: HTTP #{r.code}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
pane=api.call('GET','/v1/topology').fetch('panes').find{|p|p['surface_id'].nil?}
raise 'requires a free pane' unless pane
original=api.call('GET','/v1/terminal-sessions').fetch('sessions').find{|s|s['focused']}
session=nil
begin
  session=api.call('POST','/v1/terminal-sessions',
    {command:'/usr/bin/journalctl',arguments:['--no-pager','--quiet','-o','cat',
      *(since ? ['--since',since] : ['--boot']),
      '-u','blit.service','-u','blit-update-helper.service','-n','300'],
      pane_id:pane.fetch('id')})
  api.call('PATCH',"/v1/terminal-sessions/#{original['id']}",{focused:true}) if original
  Timeout.timeout(10) do
    loop do
      state=api.call('GET',"/v1/terminal-sessions/#{session['id']}").fetch('state')
      raise 'journal read failed' if state=='failed'
      break if state=='stopped'
      sleep 0.1
    end
  end
  surface="/v1/surfaces/#{session.fetch('surface_id')}"
  # Child reaping and PTY draining are separate event-loop operations. Do not
  # interpret stopped as an atomic receipt that all terminal bytes were read.
  previous=nil; quiet_since=Process.clock_gettime(Process::CLOCK_MONOTONIC)
  Timeout.timeout(4) do
    loop do
      revision=api.call('GET',surface+'/summary').fetch('revision')
      now=Process.clock_gettime(Process::CLOCK_MONOTONIC)
      quiet_since=now if revision!=previous
      previous=revision
      break if now-quiet_since>=0.3
      sleep 0.05
    end
  end
  scroll=api.call('GET',surface+'/scrollback').fetch('lines').map{|l|l.fetch('text').rstrip}
  cells=api.call('GET',surface+'/cells').fetch('cells')
  lines=cells.group_by{|c|c.fetch('y')}.sort.map do |_,row|
    row.sort_by{|c|c.fetch('x')}.map{|c|c.fetch('text')}.join.rstrip
  end
  puts (scroll+lines).join("\n")
ensure
  api.call('DELETE',"/v1/terminal-sessions/#{session['id']}") if session
  api.call('PATCH',"/v1/terminal-sessions/#{original['id']}",{focused:true}) if original
end
