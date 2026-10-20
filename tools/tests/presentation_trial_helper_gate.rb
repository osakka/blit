#!/usr/bin/env ruby
require 'socket'
require 'tmpdir'
require 'json'
require 'timeout'
require 'fileutils'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-trial-helper-') do |work|
  binary=File.join(work,'helper'); trial=File.join(work,'trial'); socket=File.join(work,'update.sock')
  defines={BLIT_TRIAL_ROOT:trial,BLIT_TRIAL_DROPIN_DIR:File.join(work,'dropins'),
    BLIT_TRIAL_HELPER:binary,UPDATE_SOCKET:socket,UPDATE_RUNTIME_DIR:work}
  raise 'fixture compile failed' unless system('tcc','-std=c99','-Wall',*defines.map { |k,v| "-D#{k}=\"#{v}\"" },
    File.join(root,'tools/tests/presentation_trial_helper_fixture.c'),'-o',binary)
  raise 'command deadline failed' unless system(binary,'--bounded-command-test')
  pid=nil; frontend=nil
  request=lambda do |text|
    Timeout.timeout(5) do
      UNIXSocket.open(socket) { |s| s.write(text); s.gets.to_s }
    end
  end
  start=lambda do
    pid=spawn(binary,out:File.join(work,'log'),err:[:child,:out])
    Timeout.timeout(5) do
      loop do
        begin
          break if request.call("TRIAL_STATUS\n").start_with?('OK ')
        rescue Errno::ECONNREFUSED,Errno::ENOENT
          sleep 0.02
        end
      end
    end
  end
  stop=lambda do
    Process.kill('TERM',pid)
    Timeout.timeout(5) { Process.wait(pid) }; pid=nil
  end
  status=lambda { JSON.parse(request.call("TRIAL_STATUS\n").sub(/^OK /,'')) }
  prepare=lambda do
    doc=JSON.generate(version:1,server:{display_backend:'sdl',renderer:'software'},login:{enabled:false})
    response=request.call("TRIAL_PREPARE 30 software #{doc.bytesize}\n#{doc}")
    raise response unless response.start_with?('OK ')
    JSON.parse(response.sub(/^OK /,''))
  end
  begin
    start.call
    raise 'not idle' unless status.call['state']=='idle'
    lease=prepare.call
    raise 'update accepted while active' unless request.call("UPDATE /nonexistent #{'0'*64}\n").start_with?('ERROR ')
    # No frontend/API exists in this fixture. The actual helper CLI must work
    # from another process and terminate a blocked userspace frontend stand-in.
    frontend=spawn('/usr/bin/sleep','60')
    File.write(File.join(trial,'frontend.pid'),frontend.to_s)
    raise 'external recovery failed' unless system(binary,'--recover-trial',lease['id'])
    Timeout.timeout(5) { Process.wait(frontend) }; frontend=nil
    restored=status.call
    raise 'recovery not recorded' unless restored['state']=='restored' && restored['active']==false
    raise 'normal restart not queued' unless File.exist?(File.join(trial,'normal-restart-queued'))
    newer=prepare.call
    raise 'stale restore acted' unless system(binary,'--recover-trial',lease['id']) && status.call['id']==newer['id'] && status.call['active']
    # Restart the real helper entry point: outstanding runtime state is recovered.
    stop.call; start.call
    raise 'helper restart did not recover' unless status.call['state']=='restored' && !status.call['active']
    before=status.call
    raise 'trailing id accepted' unless request.call("TRIAL_ACTIVATE #{newer['id']}junk\n").start_with?('ERROR ')
    raise 'invalid request changed state' unless status.call==before
    api=File.join(work,'api-gate')
    raise 'API gate compile failed' unless system('tcc','-O2','-std=c99',
      "-I#{root}/vendor/SDL3/include","-I#{root}/build/deps/prefix/include",
      "-I#{root}/build/deps/prefix/include/libdrm","-I#{root}/build/deps/prefix/include/freetype2",
      File.join(root,'tools/tests/presentation_trial_api_gate.c'),File.join(root,'src/blit_kms.c'),File.join(root,'src/blit_vt.c'),
      File.join(root,'src/blit_font.c'),File.join(root,'src/blit_raster.c'),
      '/lib/x86_64-linux-gnu/libfreetype.so.6',
      ENV.fetch('BLIT_HARFBUZZ_LIBRARY', '/lib/x86_64-linux-gnu/libharfbuzz.so.0'),"-L#{root}/build/sdl3-kms",'-lSDL3',
      "-L#{root}/build/deps/prefix/lib/x86_64-linux-gnu",'-ldrm','-lutil','-lz','-o',api)
    raise 'API dispatcher gate failed' unless system({'BLIT_UPDATE_SOCKET'=>socket,
      'LD_LIBRARY_PATH'=>"#{root}/build/sdl3-kms:#{root}/build/deps/prefix/lib/x86_64-linux-gnu"},
      api,File.join(work,'canonical-must-not-be-created.json'))
    Timeout.timeout(5) { sleep 0.02 until status.call['state']=='restored' }
    snapshot=JSON.parse(File.read(File.join(trial,'received-snapshot.json')))
    raise 'snapshot changed layout/font/login target' unless
      snapshot.dig('server','display_backend')=='sdl' && snapshot.dig('server','renderer')=='software' &&
      snapshot.dig('server','pane_layout')=='1x2' && snapshot.dig('font','size_px')==22 &&
      snapshot.dig('font','font_id')=='blit-modern' && snapshot.dig('login','enabled')==false &&
      snapshot.dig('login','preferred_pane_id')=='pane-display-93-2' &&
      snapshot.dig('input_devices','input-persistent','sensitivity_percent')==123 && snapshot['revision']==7
    puts 'PRESENTATION_TRIAL_HELPER_PASS protocol=actual CLI=independent blocked_frontend=killed helper_restart=restored stale=ignored updates=refused systemd=mock'
  ensure
    if frontend
      Process.kill('KILL',frontend) rescue nil
      Process.wait(frontend) rescue nil
    end
    stop.call if pid
  end
end
