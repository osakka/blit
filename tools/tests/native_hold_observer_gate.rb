#!/usr/bin/env ruby
# Actual installed Hibr hold CLI; isolated namespace, never Hibr source edits.
require 'open3'
require 'pty'
require 'io/console'
require 'tmpdir'
require 'timeout'
ROOT=File.expand_path('../..',__dir__)
HELPER=File.join(ROOT,'hibr/blit-desktop-observe.hibr')
LOAD='mod load pty; mod load term; mod load hold; '
def check(ok, message) = (raise(message) unless ok)
Dir.mktmpdir('blit-native-hold') do |root|
  env={'TMPDIR'=>root}
  name="blit-gate-#{Process.pid}"
  reader=writer=pid=aux_reader=aux_writer=aux_pid=nil
  begin
    out,err,status=Open3.capture3(env,'/usr/bin/hibr',HELPER,name,'login')
    check(status.success? && out=="waiting owner_missing\n",'absent owner inferred ready')
    _,err,status=Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold new -d #{name} /bin/sleep 60")
    check(status.success?,"native hold creation failed:#{err}")
    out,_,status=Open3.capture3(env,'/usr/bin/hibr',HELPER,name,'login')
    check(status.success? && out=="waiting client_missing\n",'missing named client inferred primary')
    reader,writer,pid=PTY.spawn(env,'/usr/bin/hibr','-c',"#{LOAD}hold attach -n login #{name}")
    writer.winsize=[67,240]
    observe=-> {
      out,err,status=Open3.capture3(env,'/usr/bin/hibr',HELPER,name,'login')
      check(status.success?,"native observer failed:#{err}")
      out
    }
    out=nil
    Timeout.timeout(5) do
      loop do
        out=observe.call
        break if out.match?(/\Aready \d+ 67 240 0 0 67 240 1\nclients 1\nclient login 0 0 67 240 1\n\z/)
        sleep 0.03
      end
    end
    owner=out.split[1]
    check(owner.to_i>1,'native owner PID missing')
    aux_reader,aux_writer,aux_pid=PTY.spawn(env,'/usr/bin/hibr','-c',"#{LOAD}hold attach -m -n auxiliary #{name} 0 67")
    aux_writer.winsize=[24,80]
    out=nil
    Timeout.timeout(5) do
      loop do
        out=observe.call
        break if out.match?(/\Aready #{owner} 67 240 0 0 67 240 1\nclients 2\n(?=.*client auxiliary -?\d+ -?\d+ 24 80 [01]\n)(?=.*client login 0 0 67 240 1\n)/m)
        sleep 0.03
      end
    end
    check(out.include?('client auxiliary ') && out.include?('client login 0 0 67 240 1'),
      'all native clients or their geometries were not observed')
    ticks=File.read("/proc/#{owner}/stat").split(') ',2).last.split[19]
    place=File.join(ROOT,'hibr/blit-desktop-place.hibr')
    _,err,status=Open3.capture3(env,'/usr/bin/hibr',place,name,'login','auxiliary',owner,ticks,'-24','0')
    check(status.success?,"guarded native placement failed:#{err}")
    placed=observe.call.lines.grep(/^client /).to_h { |l| f=l.split; [f[1],f[2,2].map(&:to_i)] }
    check(placed['auxiliary'][0]-placed['login'][0]==-24 && placed['auxiliary'][1]==placed['login'][1],
      'native helper did not preserve relative pane placement across union normalization')
    _,_,status=Open3.capture3(env,'/usr/bin/hibr',place,name,'login','auxiliary',owner,'1','11','7')
    check(!status.success?,'stale owner lifetime was allowed to move a native client')
    # A move changes native geometry, not the association's client identity.
    _,err,status=Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold move #{name} login 7 9")
    check(status.success?,"native fixture move failed:#{err}")
    check(observe.call.split[1]==owner,'observation changed held owner')
    # Another name is never guessed from a lone primary client.
    out,_,status=Open3.capture3(env,'/usr/bin/hibr',HELPER,name,'other')
    check(status.success? && out=="waiting client_missing\n",'wrong client matched a primary')
    puts 'NATIVE_HOLD_OBSERVER_PASS cli=installed owner=actual client=exact geometry=67x240 owner_continuity=yes'
  ensure
    Open3.capture3(env,'/usr/bin/hibr','-c',"#{LOAD}hold kill #{name}")
    Process.kill('TERM',aux_pid) rescue nil
    Process.wait(aux_pid) rescue nil
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
    reader&.close;writer&.close
  end
end
