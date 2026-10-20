#!/usr/bin/env ruby
# Operator-only read-only diagnostics via a temporary unbound API PTY.
# Chunked OSC titles avoid truncating reports or drawing onto desktop panes.
require 'json'
require 'net/http'
require 'timeout'
require 'base64'
base=URI(ENV.fetch('BLIT_BASE_URL','http://192.168.10.54:8080'))
token=ENV.fetch('BLIT_AUTH_TOKEN')
command=ARGV.fetch(0){abort 'usage: api_command_report.rb BASH_DIAGNOSTIC'}
request=lambda do |method,path,body=nil|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base+path)
  q['Authorization']="Bearer #{token}";q['Content-Type']='application/json'
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(base.host,base.port,open_timeout:3,read_timeout:8){|h|h.request(q)}
  raise "HTTP #{r.code}: #{r.body}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
child=<<~'CODE'
  require 'io/console';require 'open3';require 'json';require 'base64'
  STDIN.raw!;STDOUT.sync=true
  out,err,status=Open3.capture3('/usr/bin/timeout','12','/bin/bash','-c',ARGV.join)
  raw=JSON.generate({stdout:out,stderr:err,exit_code:status.exitstatus})
  Base64.strict_encode64(raw).scan(/.{1,72}/).each_with_index do |part,i|
    STDOUT.write("\e]2;probe:#{i}:#{part}\a");break unless STDIN.gets
  end
  STDOUT.write("\e]2;probe:done\a")
CODE
session=nil;received='';sequence=0
parts=child.scan(/.{1,200}/m)
arguments=['-e','eval(ARGV.shift(ARGV.shift.to_i).join)',parts.size.to_s]+parts+command.scan(/.{1,200}/m)
abort 'diagnostic exceeds API argument limit' if arguments.size>32
begin
  session=request.call('POST','/v1/terminal-sessions',{command:'/usr/bin/ruby',
    arguments:arguments,surface:{width:1,height:1}})
  Timeout.timeout(35) do
    loop do
      title=request.call('GET',"/v1/terminal-sessions/#{session['id']}")['title'].to_s
      break if title=='probe:done'
      if title.start_with?("probe:#{sequence}:")
        received<<title.split(':',3)[2];sequence+=1
        request.call('POST',"/v1/terminal-sessions/#{session['id']}/input",{mode:'raw',data:"\n"})
      else
        sleep 0.025
      end
    end
  end
  puts JSON.pretty_generate(JSON.parse(Base64.strict_decode64(received)))
ensure
  request.call('DELETE',"/v1/terminal-sessions/#{session['id']}") if session
end
