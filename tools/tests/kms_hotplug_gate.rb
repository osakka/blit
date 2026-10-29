#!/usr/bin/env ruby
# Actual gateway and direct-KMS lifecycle with only the libdrm boundary mocked.
require 'json'
require 'net/http'
require 'tmpdir'
require 'fileutils'
require 'timeout'
require_relative 'gate_port'
ROOT=File.expand_path('../..',__dir__)
BINARY=ENV.fetch('BLIT_GATE_BINARY',File.join(ROOT,'build/blit/blit-server'))
PORT=gate_port(ENV.fetch('BLIT_KMS_HOTPLUG_PORT','19497'))
BASE=URI("http://127.0.0.1:#{PORT}")
def check(ok,why) = (raise(why) unless ok)
def req(method,path,body=nil)
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put}.fetch(method).new(BASE+path)
  q['Authorization']='Bearer hotplug-test'; q['Content-Type']='application/json';q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:3){|h|h.request(q)}
  check(r.code.start_with?('2'),"#{method} #{path} #{r.code}:#{r.body}")
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
def await(why)
  Timeout.timeout(13,RuntimeError,"timeout:#{why}") do
    loop do
      begin
        v=yield; return v if v
      rescue Errno::ECONNREFUSED
      end
      sleep 0.05
    end
  end
end
Dir.mktmpdir('blit-kms-hotplug') do |root|
  mock=File.join(root,'drm.so');state=File.join(root,'state');trace=File.join(root,'trace')
  check(system('tcc','-shared','-fPIC','-I'+File.join(ROOT,'build/deps/prefix/include'),
    '-I'+File.join(ROOT,'build/deps/prefix/include/libdrm'),File.join(ROOT,'tools/tests/fixtures/drm_hotplug_mock.c'),'-o',mock),'mock compile failed')
  write=->(ids,fail_add=0,fail_remove=0,delay=0,width=320) do
    File.write(state+'.new',"#{ids.length} #{fail_add} #{fail_remove} #{delay}\n"+ids.map{|id|"#{id} #{width} 192\n"}.join)
    File.rename(state+'.new',state)
  end
  ids=-> {req('GET','/v1/displays')['displays'].map{|d|d['id']}}
  write.call([93]);device=File.join(root,'drm');File.write(device,'');Dir.mkdir(File.join(root,'input'))
  env={'BLIT_DISPLAY_BACKEND'=>'kms','BLIT_KMS_COMPOSITOR'=>'cpu','BLIT_DRM_DEVICE'=>device,'BLIT_DRM_MOCK_STATE'=>state,'BLIT_DRM_MOCK_TRACE'=>trace,
    'LD_PRELOAD'=>mock,'LD_LIBRARY_PATH'=>File.join(ROOT,'build/deps/prefix/lib/x86_64-linux-gnu'),
    'BLIT_AUTH_TOKEN'=>'hotplug-test','BLIT_LOGIN_AUTOSTART'=>'0','BLIT_PANE_LAYOUT'=>'1x2',
    'BLIT_SETTINGS_PATH'=>File.join(root,'settings.json'),'BLIT_INPUT_ROOT'=>File.join(root,'input'),
    'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Terminus16.psf')}
  log=File.join(root,'server.log');pid=spawn(env,BINARY,PORT.to_s,out:log,err:[:child,:out])
  begin
    await('startup'){req('GET','/v1/version')}
    await('startup migration durable'){req('GET','/v1/settings').dig('storage','state')=='clean'}
    check(ids.call==['display-93'],'initial inventory wrong')
    primary=req('POST','/v1/terminal-sessions',{command:'/bin/cat',pane_id:'pane-display-93-2'})
    scanout_for_primary=-> do
      req('GET','/v1/diagnostics')['render']['outputs'].find do |output|
        output['display_id']=='display-93'
      end&.fetch('kms_scanout',nil)
    end
    await('direct-KMS scanout completion is observable') do
      scanout=scanout_for_primary.call
      scanout if scanout && scanout['available'] && scanout['flip_completions']>0
    end
    first_scanout=scanout_for_primary.call
    check(first_scanout['last_vblank_timestamp_us']>0 &&
          first_scanout['vblank_sequence']>0,
          'first completed flip omitted its vblank identity')
    await('direct-KMS vblank rate sample is observable') do
      scanout=scanout_for_primary.call
      scanout if scanout && scanout['vblank_span_count']>0 &&
        scanout['vblank_span_us']>0 && scanout['observed_refresh_hz']>0
    end
    pointer_before=scanout_for_primary.call
    position_changes_before=req('GET','/v1/diagnostics/runtime')
      .dig('performance','pointer_position_changes')
    req('POST','/v1/input/inject',{kind:'pointer_motion',x:100,y:100})
    pointer_after=await('pointer position reaches KMS output submission') do
      scanout=scanout_for_primary.call
      position_changes=req('GET','/v1/diagnostics/runtime')
        .dig('performance','pointer_position_changes')
      if scanout && position_changes>position_changes_before &&
        scanout['pointer_change_requests']>pointer_before['pointer_change_requests'] &&
        scanout['pointer_frame_submissions']>pointer_before['pointer_frame_submissions']
        {scanout:scanout,position_changes:position_changes}
      end
    end
    check(pointer_after[:scanout]['pointer_frame_deferred']>=
          pointer_before['pointer_frame_deferred'],
          'pointer deferral counter moved backwards')
    policy=req('POST','/v1/auxiliary-displays/apply',{bindings:[{output:'DP-2',pane_ordinal:1}]})
    check(policy.dig('bindings',0,'state')=='waiting_output','absent binding not waiting')
    before=req('GET','/v1/topology');settings=req('GET','/v1/settings')
    device_revision=req('GET','/v1/displays')['revision']
    primary_grid=before['panes'].find{|p|p['id']=='pane-display-93-2'}['cell_grid']
    write.call([95,93,94])
    await('new second/third outputs'){ids.call.sort==%w[display-93 display-94 display-95]}
    discovered_panes=req('GET','/v1/topology')['panes']
    check(discovered_panes.count{|p|p['display_id']=='display-94'}==1 &&
          discovered_panes.count{|p|p['display_id']=='display-95'}==1,
          'new outputs inherited another monitor split instead of defaulting to 1x1')
    check(req('GET',"/v1/terminal-sessions/#{primary['id']}")['process_id']==primary['process_id'],'primary replaced')
    check(req('GET','/v1/topology')['revision']>before['revision'],'topology revision unchanged')
    check(req('GET','/v1/displays')['revision']>device_revision,'display inventory revision unchanged')
    check(req('GET','/v1/topology')['panes'].find{|p|p['id']=='pane-display-93-2'}['cell_grid']==primary_grid,'unrelated addition changed primary geometry')
    await('binding resolves discovered output'){req('GET','/v1/auxiliary-displays').dig('bindings',0,'state')=='waiting_authentication'}
    check(req('GET','/v1/settings')==settings,'hotplug wrote settings')
    check(!File.read(trace).lines.any?{|l|l.match?(/\Afree [12] /)},'unrelated addition freed primary buffers')
    worker=req('GET','/v1/diagnostics').dig('render','hotplug')
    check(worker && worker['supported'] && worker['scans']>0,'observer diagnostics missing')
    revision=req('GET','/v1/topology')['revision']
    write.call([94,95,93])
    sleep 5.4
    check(req('GET','/v1/topology')['revision']==revision,'enumeration reorder changed topology')
    write.call([93,95])
    await('remove secondary'){ids.call.sort==%w[display-93 display-95]}
    await('binding retains absent intent'){req('GET','/v1/auxiliary-displays').dig('bindings',0,'state')=='waiting_output'}
    write.call([93,94,95],94)
    sleep 5.4
    check(ids.call.sort==%w[display-93 display-95],'failed add harmed existing outputs')
    check(req('GET','/v1/diagnostics').dig('render','hotplug','error').include?('activation_failed'),'failed add not diagnosed')
    write.call([93,94,95])
    await('reconnect after failed activation'){ids.call.length==3}
    write.call([93,95],0,101)
    await('failed teardown removes unavailable inventory'){ids.call.sort==%w[display-93 display-95]}
    check(req('GET','/v1/diagnostics').dig('render','hotplug','error').include?('disable_failed'),'teardown failure not diagnosed')
    write.call([93,94,95])
    await('quarantined resource recovers'){ids.call.length==3}
    write.call([],0,0)
    await('zero outputs keeps API alive'){ids.call.empty?}
    check(req('GET',"/v1/terminal-sessions/#{primary['id']}")['process_id']==primary['process_id'],'primary killed by last-output loss')
    write.call([93])
    await('primary connector returns'){ids.call==['display-93']}
    check(req('GET',"/v1/terminal-sessions/#{primary['id']}")['pane_id']=='pane-display-93-2','primary pane changed')
    write.call([93],0,0,0,384)
    await('returned mode resizes existing PTY'){req('GET','/v1/topology')['panes'].find{|p|p['id']=='pane-display-93-2'}&.dig('cell_grid','columns')==48}
    check(req('GET',"/v1/terminal-sessions/#{primary['id']}")['process_id']==primary['process_id'],'mode change restarted primary')
    File.write(state+'.new',"-1 0 0 0\n");File.rename(state+'.new',state)
    await('failed snapshot is diagnosed'){req('GET','/v1/diagnostics').dig('render','hotplug','error').include?('snapshot_unavailable')}
    check(ids.call==['display-93'],'failed probe invented output removal')
    write.call([93],-1093)
    await('unknown connection status is diagnosed'){req('GET','/v1/diagnostics').dig('render','hotplug','error').include?('state_unknown')}
    check(ids.call==['display-93'],'unknown state invented output removal')
    write.call([93],0,0,300)
    await('slow worker probe starts'){File.read(trace).include?('probe-start 300 ')}
    overlap=0
    latencies=30.times.map do
      t=Process.clock_gettime(Process::CLOCK_MONOTONIC);req('GET','/v1/version');elapsed=Process.clock_gettime(Process::CLOCK_MONOTONIC)-t
      overlap+=1 unless File.read(trace).include?('probe-end 300 ')
      elapsed
    end
    check(overlap>0,'API samples did not overlap slow connector probe')
    check(latencies.max<0.2,"connector probing blocked API:#{latencies.max}")
    write.call([93])
    req('POST','/v1/control/shutdown');Timeout.timeout(10){Process.wait(pid)};pid=nil
    write.call([])
    pid=spawn(env,BINARY,PORT.to_s,out:log,err:[:child,:out])
    await('initial zero-output startup'){req('GET','/v1/version')}
    check(ids.call.empty?,'headless boot invented output')
    write.call([93]);await('first monitor after headless boot'){ids.call==['display-93']}
    req('POST','/v1/control/restart')
    await('direct KMS restart retains API and output') do
      begin
        req('GET','/v1/version') && ids.call==['display-93'] &&
          req('GET','/v1/diagnostics').dig('render','hotplug','supported')
      rescue Errno::ECONNRESET, EOFError
        nil
      end
    end
    req('POST','/v1/control/shutdown');Timeout.timeout(10){Process.wait(pid)};pid=nil
    births=File.read(trace).lines.count{|l|l.start_with?('allocate ')}
    frees=File.read(trace).lines.count{|l|l.start_with?('free ')}
    check(births==frees,"scanout leak allocations=#{births} frees=#{frees}")
    puts "KMS_HOTPLUG_PASS add=2 remove=reconnect reorder=stable primary=preserved zero_outputs=api_alive restart=api_ready failure=isolated buffers=#{births}/#{frees} max_api_ms=#{(latencies.max*1000).round(2)} source=mocked_drm_not_physical"
  rescue Exception
    warn File.read(log);raise
  ensure
    if pid
      Process.kill('TERM',pid) rescue nil
      Timeout.timeout(10){Process.wait(pid)} rescue nil
    end
  end
end
