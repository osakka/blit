#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'timeout'
require 'tmpdir'
require 'fileutils'
require_relative 'gate_port'
require_relative 'test_profile'
base=URI("http://127.0.0.1:#{gate_port(19571)}")
req=lambda do |method,path,body=nil|
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(base+path)
  q['Content-Type']='application/json';q.body=JSON.generate(body) if body
  r=Net::HTTP.start(base.host,base.port,read_timeout:3){|h|h.request(q)}
  raise "#{r.code}: #{r.body}" unless r.code.start_with?('2')
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
Dir.mktmpdir('blit-session-log-gate') do |work|
  pid=spawn(TestProfile.environment.merge('DT_LOG'=>nil),TestProfile.binary,base.port.to_s,
    out:File.join(work,'server.log'),err:[:child,:out])
  paths=[]
  begin
    Timeout.timeout(8){loop{begin;req.call('GET','/v1/version');break;rescue Errno::ECONNREFUSED;sleep 0.02;end}}
    [nil,nil,File.join(work,'chosen.log')].each do |chosen|
      created=req.call('POST','/v1/terminal-sessions',{
        command:'/usr/bin/hibr',arguments:['-c','printf "\033]2;%s\007" "$DT_LOG"; /bin/sleep 2'],
        surface:{width:1,height:1},environment:chosen ? {DT_LOG:chosen} : {}})
      begin
        title=nil
        Timeout.timeout(5) do
          loop do
            title=req.call('GET',"/v1/terminal-sessions/#{created['id']}")['title']
            break if title&.start_with?('/tmp/')
            sleep 0.02
          end
        end
        if chosen
          raise 'explicit log path overwritten' unless title==chosen
        else
          raise "unsafe default log path: #{title}" unless title.match?(%r{\A/tmp/blit-hibr-[A-Za-z0-9]+/desktop\.log\z})
          dir=File.dirname(title);paths<<dir
          st=File.lstat(dir)
          raise 'unsafe log directory owner/mode' unless st.directory? && st.uid==Process.uid && (st.mode&0777)==0700
        end
      ensure
        req.call('DELETE',"/v1/terminal-sessions/#{created['id']}")
      end
    end
    raise 'log directory reused across sessions' unless paths.uniq.size==2
    puts 'SESSION_LOG_PASS private=0700 owner=session_uid names=unique explicit_override=preserved'
  ensure
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
    paths.each{|dir|FileUtils.remove_entry_secure(dir) if File.directory?(dir)}
  end
end
