#!/usr/bin/ruby
# Root-controlled auth-boundary fixture; never an actual PAM login.
require 'shellwords'
require 'socket'
command = Shellwords.split(ENV.fetch('LG_SESSIONCMD'))
command[1] = ENV.fetch('BLIT_GATE_HOOK') if command[1] == '/usr/share/blit/hibr/blit-login-session.hibr'
mode = ENV.fetch('BLIT_GATE_IDENTITY_MODE', 'desktop')
fd_index = command.index('--blit-handoff-fd')
handoff = fd_index && command[fd_index + 1].to_i
if mode == 'waiting' || !handoff
  sleep 30
  exit
end
sender_inode = File.readlink("/proc/self/fd/#{handoff}")
child = fork do
  unless mode == 'root'
    Process.groups = []
    Process::GID.change_privilege(65534)
    Process::UID.change_privilege(65534)
  end
  if mode == 'foreign'
    fork do
      # Leave the login ancestry while keeping its process group for cleanup.
      sleep 0.2
      socket = Socket.for_fd(handoff)
      socket.send('desktop', 0)
      socket.recv(32) rescue nil
      sleep 30
    end
    exit!
  elsif %w[root invalid oversized rights].include?(mode)
    socket = Socket.for_fd(handoff)
    payload = mode == 'invalid' ? 'unrecognised' : (mode == 'oversized' ? 'x' * 200 : 'desktop')
    if mode == 'rights'
      file = File.open('/dev/null')
      socket.sendmsg(payload, 0, nil, Socket::AncillaryData.unix_rights(file))
    else
      socket.send(payload, 0)
    end
    socket.recv(32) rescue nil
    sleep 30
  else
    # Mirrors auth.c's credential drop and environment replacement, but does
    # not test passwords/PAM. Real Hibr must carry the inherited descriptor.
    exec('/usr/bin/env', '-i', 'TERM=xterm-kitty',
         "BLIT_HIBR_SESSION_LAUNCHER=#{ENV.fetch('BLIT_GATE_TARGET')}",
         "SHELL=#{ENV.fetch('BLIT_GATE_TARGET')}",
         "BLIT_GATE_RESULT=#{ENV.fetch('BLIT_GATE_RESULT')}",
         "BLIT_GATE_SENDER_INODE=#{sender_inode}",
         "BLIT_GATE_OWNER_FILE=#{ENV['BLIT_GATE_OWNER_FILE']}",
         *command, mode, close_others: false)
  end
end
Process.wait(child)
sleep 30 if mode == 'foreign'
