#!/usr/bin/env ruby
# Installed native Hibr hold, isolated namespace; no Hibr source changes.
# Proves the boundary Blit must preserve: two local PTYs, one 2-D rewrite.
require 'open3'
require 'pty'
require 'io/console'
require 'tmpdir'
require 'timeout'
require 'shellwords'
LOAD='mod load pty; mod load term; mod load hold; '
def check(ok,message) = (raise(message) unless ok)
def await
  Timeout.timeout(6){loop{v=yield;return v if v;sleep 0.03}}
end
Dir.mktmpdir('blit-native-join') do |root|
  env={'TMPDIR'=>root};name="blit-pty-#{Process.pid}";clients=[];threads=[]
  input=File.join(root,'input')
  cli=->(command) do
    out,err,status=Open3.capture3(env,'/usr/bin/hibr','-c',LOAD+command)
    check(status.success?,"native CLI failed:#{command}:#{err}");out
  end
  begin
    command="stty raw -echo; printf '\033[?1003h\033[?1006h'; exec cat > #{Shellwords.escape(input)}"
    cli.call("hold new -d #{name} /bin/sh -c #{Shellwords.escape(command)}")
    attach=->(client,row=nil,col=nil) do
      args="hold attach -m -n #{client} #{name}#{row ? " #{row} #{col}" : ''}"
      if row
        # Exercise Blit's actual changed launcher, not only direct hold CLI.
        launcher=File.expand_path('../../hibr/blit-session.hibr',__dir__)
        r,w,pid=PTY.spawn(env.merge('BLIT_HIBR_PANE_ROW'=>row.to_s,'BLIT_HIBR_PANE_COL'=>col.to_s),
          '/usr/bin/hibr',launcher,'--session',name,'--join','--name',client)
      else
        r,w,pid=PTY.spawn(env,'/usr/bin/hibr','-c',LOAD+args)
      end
      w.winsize=[67,240];clients<<[r,w,pid]
      threads<<Thread.new do
        begin
          loop{r.readpartial(4096)}
        rescue Errno::EIO,IOError
        end
      end
      await{cli.call("hold clients #{name}").lines.any?{|l|l.start_with?("#{client} ") && l.split[3,2]==%w[67 240]}}
      w
    end
    lower=attach.call('lower')
    upper=attach.call('upper',-67,0)
    geometry=cli.call("hold clients #{name}").lines.map(&:split).to_h{|v|[v[0],v[1..].map(&:to_i)]}
    check(geometry=={'lower'=>[67,0,67,240,1],'upper'=>[0,0,67,240,0]},"wrong native 2-D union:#{geometry}")
    # Hold preserves modifiers/button/release/wheel, translating only position.
    lower.write((0..2).map{|button|"\e[<#{button};3;4M\e[<#{button};3;4m"}.join)
    upper_reports="\e[<8;7;9M\e[<8;7;9m\e[<42;7;9M\e[<64;7;9M\e[<65;7;9MKEY"
    upper.write(upper_reports)
    expected=(0..2).flat_map{|button|["\e[<#{button};3;71M","\e[<#{button};3;71m"]}
    expected<<upper_reports
    await{File.file?(input) && expected.all?{|s|File.binread(input).include?(s)}}
    check(!File.binread(input).include?("\e[<8;247;9M"),'upper click was transformed twice')
    puts 'NATIVE_JOIN_PTY_PASS cli=installed launcher=actual union=134x240 local_input=rewritten_once buttons=3_press_release modifiers=preserved drag=preserved wheel=2 keyboard=preserved'
  ensure
    cli.call("hold kill #{name}") rescue nil
    clients.each do |r,w,pid|
      Process.kill('TERM',pid) rescue nil
      Process.wait(pid) rescue nil
      r.close rescue nil;w.close rescue nil
    end
    threads.each{|t|t.join(1)}
  end
end
