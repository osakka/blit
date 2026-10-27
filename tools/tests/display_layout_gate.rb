#!/usr/bin/env ruby
# Actual gateway, saved document, PTY resize and mock DRM inventory boundary.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
ROOT=File.expand_path('../..',__dir__)
BINARY=ENV.fetch('BLIT_TEST_SERVER',File.join(ROOT,'build/blit/blit-server'))
PORT=Integer(ENV.fetch('BLIT_LAYOUT_GATE_PORT','19498'))
BASE=URI("http://127.0.0.1:#{PORT}")
def req(method,path,body=nil,revision=nil)
  q={'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post,'PUT'=>Net::HTTP::Put,'DELETE'=>Net::HTTP::Delete}.fetch(method).new(BASE+path)
  q['Content-Type']='application/json'; q['If-Match']=revision.to_s if revision
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:8){|h|h.request(q)}
  [r.code.to_i,r.body && !r.body.empty? ? JSON.parse(r.body) : nil]
end
def get(path)
  code,value=req('GET',path); raise "GET #{path}:#{code}" unless code==200; value
end
def check(ok,why)
  raise why unless ok
end
Dir.mktmpdir('blit-layout') do |root|
  mock=File.join(root,'drm.so'); state=File.join(root,'state'); device=File.join(root,'device')
  check(system('tcc','-shared','-fPIC','-I'+File.join(ROOT,'build/deps/prefix/include'),
    '-I'+File.join(ROOT,'build/deps/prefix/include/libdrm'),File.join(ROOT,'tools/tests/fixtures/drm_hotplug_mock.c'),'-o',mock),'mock compile')
  File.write(state,"3 0 0 0\n93 640 384\n94 640 384\n95 640 384\n")
  File.write(device,''); Dir.mkdir(File.join(root,'input'))
  env={'BLIT_DISPLAY_BACKEND'=>'kms','BLIT_DRM_DEVICE'=>device,'BLIT_DRM_MOCK_STATE'=>state,
    'LD_PRELOAD'=>mock,'LD_LIBRARY_PATH'=>File.join(ROOT,'build/deps/prefix/lib/x86_64-linux-gnu'),
    'BLIT_LOGIN_AUTOSTART'=>'0','BLIT_PANE_LAYOUT'=>'1x2','BLIT_AUTH_TOKEN'=>'',
    'BLIT_SETTINGS_PATH'=>File.join(root,'settings.json'),'BLIT_INPUT_ROOT'=>File.join(root,'input'),
    'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Terminus16.psf')}
  pid=nil
  start=-> do
    pid=spawn(env,BINARY,PORT.to_s,out:File.join(root,'log'),err:[:child,:out])
    Timeout.timeout(10){loop{begin get('/v1/version');break;rescue Errno::ECONNREFUSED;sleep 0.02;end}}
  end
  stop=-> {Process.kill('TERM',pid);Process.wait(pid);pid=nil}
  save=->(doc){s=get('/v1/settings');req('PUT','/v1/settings',{document:doc},s['revision'])}
  begin
    start.call
    settings=get('/v1/settings')
    check(settings.dig('document','display_layouts','DP-1')=={'columns'=>1,'rows'=>2},'legacy grid not migrated')
    Timeout.timeout(3,RuntimeError,'legacy startup migration was not persisted') do
      loop do
        settings=get('/v1/settings')
        path=File.join(root,'settings.json')
        break if settings.dig('storage','state')=='clean' && File.exist?(path) &&
          JSON.parse(File.read(path)).dig('display_layouts','DP-1')=={'columns'=>1,'rows'=>2}
        sleep 0.05
      end
    end
    code,session=req('POST','/v1/terminal-sessions',{command:'/bin/cat',pane_id:'pane-display-93-2'})
    check(code==201,'primary fixture failed')
    doc=settings['document'];doc['display_layouts']={'DP-1'=>{'columns'=>1,'rows'=>2},'DP-2'=>{'columns'=>2,'rows'=>3}}
    code,reply=save.call(doc);check(code==200,"independent save:#{reply}")
    topology=get('/v1/topology')
    check(topology['panes'].group_by{|p|p['display_id']}.transform_values(&:size)=={'display-93'=>2,'display-94'=>6,'display-95'=>1},'independent/default grids wrong')
    topology['outputs'].each do |output|
      check(output['layout']==(doc['display_layouts'][get('/v1/displays')['displays'].find{|d|d['id']==output['display_id']}.dig('state','connector')] || {'columns'=>1,'rows'=>1}),'effective per-output layout missing')
    end
    untouched=get('/v1/settings')
    performance=get('/v1/diagnostics/runtime')['performance']
    check(performance['process_cpu_ms'].is_a?(Numeric) && performance['process_cpu_ms']>=0 &&
          performance['desktop_cpu_ms'].nil? && performance['system_load_1m'].is_a?(Numeric),'CPU observability missing or invented native owner')
    doc['display_layouts']['DP-1']['rows']=1
    code,reply=save.call(doc);check(code==409 && reply['code']=='topology_in_use','occupied pane silently removed')
    check(get('/v1/settings')==untouched,'rejected layout changed settings')
    doc['display_layouts']['DP-1']['rows']=3
    code,reply=save.call(doc);check(code==200,'surviving pane resize refused')
    check(get("/v1/terminal-sessions/#{session['id']}")['process_id']==session['process_id'],'layout restarted PTY')
    summary=get("/v1/surfaces/#{session['surface_id']}/summary")
    check(summary['height']==8 || summary.dig('size','height')==8,"PTY grid did not resize:#{summary}")
    req('DELETE',"/v1/terminal-sessions/#{session['id']}")
    # All combinations through 8x8 when the live aggregate is within 64.
    1.upto(8) do |columns|
      1.upto(8) do |rows|
        doc['display_layouts']={'DP-1'=>{'columns'=>columns,'rows'=>rows}}
        code,reply=save.call(doc)
        check(code==(columns*rows+2>64 ? 400 : 200),"grid #{columns}x#{rows}:#{code}:#{reply}")
        next unless code==200
        panes=get('/v1/topology')['panes'].select{|p|p['display_id']=='display-93'}
        check(panes.size==columns*rows,'grid pane count wrong')
        check(panes.all?{|p|g=p['cell_grid'];r=p['pixel_region'];g['origin_x']>=r['x'] && g['origin_y']>=r['y'] && g['origin_x']+g['pixel_width']<=r['x']+r['width'] && g['origin_y']+g['pixel_height']<=r['y']+r['height']},'pane crossed its pixel partition')
      end
    end
    [0,9,1.5,'2',nil,true].each do |invalid|
      doc['display_layouts']={'DP-1'=>{'columns'=>invalid,'rows'=>1}}
      code,_=save.call(doc);check(code==400,"invalid column accepted:#{invalid.inspect}")
    end
    doc['display_layouts']={'DP-1'=>{'columns'=>3,'rows'=>2},'HDMI-A-1'=>{'columns'=>2,'rows'=>3}}
    full_doc=Marshal.load(Marshal.dump(doc))
    full_doc['display_layouts']=16.times.to_h{|i|["offline-#{i}",{'columns'=>1,'rows'=>1}]}
    code,_=save.call(full_doc);check(code==200,'valid offline-capacity document refused')
    unchanged=get('/v1/settings')
    code,reply=req('PUT','/v1/topology',{layout:{columns:1,rows:2}})
    check(code==400 && reply['code']=='display_layout_capacity_exceeded','legacy bulk exceeded saved layout capacity')
    check(get('/v1/settings')==unchanged,'failed bulk layout changed saved state')
    code,_=save.call(doc);check(code==200,'offline layout refused')
    stop.call;start.call
    check(get('/v1/settings').dig('document','display_layouts')==doc['display_layouts'],'per-output layouts lost at restart')
    puts 'DISPLAY_LAYOUT_PASS grids=1..8 independent=yes defaults=1x1 occupied=409 resize=same_pty bounds=complete_cells restart=preserved'
  ensure
    stop.call if pid
  end
end
