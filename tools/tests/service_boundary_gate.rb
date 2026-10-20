#!/usr/bin/env ruby
# Exercise the shipped service policy using isolated local systemd units.
require 'open3'
require 'socket'
require 'timeout'
require 'securerandom'
root=File.expand_path('../..',__dir__)
unit=File.read(ARGV.find{|a|a.end_with?('.service')} || File.join(root,'packaging/blit.service'))
def value(unit,key)
  matches=unit.lines.grep(/^#{Regexp.escape(key)}=/)
  raise "ambiguous/missing #{key}" unless matches.size==1
  matches[0].split('=',2)[1].strip
end
raise 'root login profile drift' unless value(unit,'User')=='root' && value(unit,'NoNewPrivileges')=='false'
raise 'user session sockets hidden by private tmp' unless value(unit,'PrivateTmp')=='false'
raise 'restart policy drift' unless value(unit,'Restart')=='on-failure' && value(unit,'RestartSec')=='10s' &&
  value(unit,'StartLimitIntervalSec')=='60s' && value(unit,'StartLimitBurst')=='3'
raise 'headless API must not depend on a specific DRM node' if unit.include?('ConditionPathExists=/dev/dri/')
if ARGV.include?('--static')
  puts 'SERVICE_POLICY_PASS root=intentional tmp=shared restart=3_per_60s_10s headless=allowed'
  exit
end
unless system('sudo','-n','true',out:File::NULL,err:File::NULL)
  puts 'SERVICE_BOUNDARY_SKIP requires=local_systemd_sudo';exit 77
end
units=[]
begin
  # Negative control proves that this test detects the former namespace fault.
  %w[true false].each do |private_tmp|
    name="blit-boundary-#{SecureRandom.hex(6)}"
    path="/tmp/#{name}.sock"
    units<<name
    code='s=UNIXServer.new(ARGV[0]);puts "ready";STDOUT.flush;if ARGV[1]=="false";c=s.accept;c.write(c.read(4));c.close;else;sleep 0.2;end;s.close;File.unlink(ARGV[0])'
    Open3.popen3('sudo','-n','systemd-run','--quiet','--pipe','--wait','--collect',
      "--unit=#{name}","--property=User=#{Process.uid}","--property=PrivateTmp=#{private_tmp}",
      '/usr/bin/ruby','-rsocket','-e',code,path,private_tmp) do |input,output,error,wait|
      Timeout.timeout(8) do
        raise "fixture not ready: #{error.read}" unless output.gets&.strip=='ready'
        if private_tmp=='true'
          raise 'private tmp negative control leaked socket' if File.exist?(path)
        else
          UNIXSocket.open(path) do |socket|
            socket.write('ping');raise 'shared socket round trip failed' unless socket.read(4)=='ping'
          end
        end
        raise "systemd fixture failed: #{error.read}" unless wait.value.success?
      end
    end
  end
  name="blit-retry-#{SecureRandom.hex(6)}";units<<name
  args=%w[Restart RestartSec StartLimitIntervalSec StartLimitBurst].map{|key|"--property=#{key}=#{value(unit,key)}"}
  out,err,status=Open3.capture3('sudo','-n','systemd-run','--quiet',"--unit=#{name}",*args,'/bin/false')
  raise "retry fixture start failed: #{err}" unless status.success?
  Timeout.timeout(45) do
    loop do
      state,err,status=Open3.capture3('systemctl','show',name,'-p','ActiveState','-p','SubState','-p','NRestarts')
      raise "retry state unavailable: #{err}" unless status.success?
      if state.include?('ActiveState=failed') && state.include?('SubState=failed')
        raise "unexpected retry count: #{state}" unless state.include?('NRestarts=3')
        break
      end
      sleep 0.2
    end
  end
  puts 'SERVICE_BOUNDARY_PASS socket=shared_same_uid private_tmp_negative=hidden restart=failed_after_three_starts'
ensure
  units.each do |name|
    system('sudo','-n','systemctl','stop',name,out:File::NULL,err:File::NULL)
    system('sudo','-n','systemctl','reset-failed',name,out:File::NULL,err:File::NULL)
  end
end
