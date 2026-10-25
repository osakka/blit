#!/usr/bin/env ruby
# ADR0128: actual credential boundary and Hibr workers; synthetic native owner
# inventory isolates controller behavior. Actual native CLI is gated separately.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'fileutils'
require_relative 'gate_port'
require_relative 'gate_server'
ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
PORT = gate_port(ENV.fetch('BLIT_AUXILIARY_GATE_PORT', '19493'))
BASE = URI("http://127.0.0.1:#{PORT}")
abort('root fixture unavailable') unless system('sudo', '-n', 'true', out: File::NULL, err: File::NULL)
def check(ok, message) = (raise(message) unless ok)
def request(method, path, body = nil, headers = {}, expected = nil)
  q = { 'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post, 'PUT'=>Net::HTTP::Put, 'DELETE'=>Net::HTTP::Delete }.fetch(method).new(BASE + path)
  q['Authorization'] = 'Bearer auxiliary-admin'
  q['Content-Type'] = 'application/json'
  headers.each { |k,v| q[k] = v }
  q.body = body.is_a?(String) ? body : JSON.generate(body) if body
  r = Net::HTTP.start(BASE.host, BASE.port, read_timeout: 4) { |h| h.request(q) }
  check(expected ? r.code.to_i == expected : r.code.start_with?('2'), "HTTP #{method} #{path} #{r.code}:#{r.body}")
  r.body.to_s.empty? ? nil : JSON.parse(r.body)
end
def await(message = 'condition')
  Timeout.timeout(12, RuntimeError, "timeout: #{message}") do
    loop do
      begin
        value = yield
        return value if value
      rescue Errno::ECONNREFUSED, EOFError
      end
      sleep 0.04
    end
  end
end
def status = request('GET','/v1/auxiliary-displays')
def stop
  return unless $pid
  request('POST','/v1/control/shutdown') rescue nil
  Timeout.timeout(12) { Process.wait($pid) }
  $pid = nil
end
at_exit { stop }
Dir.mktmpdir('blit-auxiliary') do |root|
  File.chmod(0755, root)
  Dir.mkdir(File.join(root,'input'))
  result = File.join(root,'result'); owner = File.join(root,'owner')
  proof = File.join(root,'proof'); position = File.join(root,'position'); moves = File.join(root,'moves')
  [result,owner,proof,position,moves].each { |p| File.write(p,''); File.chmod(0666,p) }
  hook = File.join(root,'hook.hibr')
  FileUtils.cp(File.join(ROOT,'hibr/blit-login-session.hibr'),hook)
  target = File.join(root,'target.hibr')
  FileUtils.cp(File.join(ROOT,'tools/tests/fixtures/login_identity_target.hibr'),target)
  File.chmod(0755,target)
  launcher = File.join(root,'blit-session.hibr')
  File.write(launcher, <<~HIBR)
    strict
    [ "$EUID" = 65534 ] || exit 4
    [ "${BLIT_API_TOKEN-absent}" = absent ] || exit 5
    [ "${BLIT_AUTH_TOKEN-absent}" = absent ] || exit 6
    [ "$1" = --session ] && [ "$2" = desktop ] && [ "$3" = --standby ] && [ "$4" = --name ] || exit 7
    printf '%s\\n' "$5" > '#{proof}'
    exec /bin/sleep 90
  HIBR
  observer = File.join(root,'observer.hibr')
  File.write(observer, <<~HIBR)
    strict
    pid := /bin/cat '#{owner}'
    [ -n "$pid" ] || { printf 'waiting owner_missing\\n'; exit 0; }
    name := /bin/cat '#{proof}'
    pos := /bin/cat '#{position}'
    count=1
    [ -z "$name" ] || [ "$pos" = absent ] || count=2
    printf 'ready %s 24 128 0 0 24 128 1\\nclients %s\\nclient %s 0 0 24 128 1\\n' "$pid" "$count" "$2"
    if [ "$count" = 2 ]; then
        [ -n "$pos" ] || pos='0 128'
        printf 'client %s %s 24 128 0\\n' "$name" "$pos"
    fi
  HIBR
  placer = File.join(root,'placer.hibr')
  File.write(placer, <<~HIBR)
    strict
    [ "$EUID" = 65534 ] && [ "${BLIT_AUTH_TOKEN-absent}" = absent ] || exit 3
    printf '%s %s\\n' "$6" "$7" > '#{position}'
    printf 'move\\n' >> '#{moves}'
  HIBR
  log = File.join(root,'server.log')
  settings = File.join(root,'settings.json')
  env = {**gate_display_env,
    'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),'BLIT_INPUT_ROOT'=>File.join(root,'input'),
    'BLIT_SETTINGS_PATH'=>settings,'BLIT_AUTH_TOKEN'=>'auxiliary-admin','BLIT_PANE_LAYOUT'=>'1x2',
    'BLIT_LOGIN_PANE_ID'=>'pane-display-1-2','BLIT_LOGIN_AUTOSTART'=>'0',
    'BLIT_LOGIN_COMMAND'=>File.join(ROOT,'tools/tests/fixtures/login_identity.rb'),'BLIT_LOGIN_SCRIPT'=>'',
    'BLIT_GATE_IDENTITY_MODE'=>'desktop','BLIT_GATE_HOOK'=>hook,'BLIT_GATE_TARGET'=>target,
    'BLIT_GATE_RESULT'=>result,'BLIT_GATE_OWNER_FILE'=>owner,
    'BLIT_HIBR_OBSERVER'=>observer,'BLIT_HIBR_PLACER'=>placer,'BLIT_AUXILIARY_LAUNCHER'=>launcher}
  begin
    $pid = spawn('sudo','-n','env',*env.map { |k,v| "#{k}=#{v}" },BINARY,PORT.to_s,out:log,err:[:child,:out])
    await('server') { request('GET','/v1/version') }
    display = request('GET','/v1/displays')['displays'].first
    output = display.dig('state','connector') || display['name']
    binding = {'output'=>output,'pane_ordinal'=>1}
    # Startup migration is a separate durable operation, not an auxiliary
    # controller write. Establish the baseline after that receipt settles.
    saved = await('startup migration durable') do
      q=request('GET','/v1/settings'); q if q.dig('storage','state')=='clean'
    end
    applied = request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[binding]})
    check(applied.dig('bindings',0,'state') == 'waiting_authentication','unauthed auxiliary launched')
    check(request('GET','/v1/settings') == saved,'Apply changed settings')
    same = request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[binding]})
    check(same['revision']==applied['revision'],'identical Apply was not idempotent')
    ['{}','{"bindings":null}','{"bindings":[],"unknown":1}',
     '{"bindings":[]','{"bindings":[],}','{"bindings":[]}trash','{"bindings":[] "retry_stopped":true}',
     '{"bindings":[{"output":"DP-1" "pane_ordinal":1}]}',
     '{"bindings":[{"output":"DP-1","pane_ordinal":1,}]}',
     JSON.generate('bindings'=>[binding,binding]), JSON.generate('bindings'=>[binding.merge('pane_ordinal'=>0)])].each do |bad|
      request('POST','/v1/auxiliary-displays/apply',bad,{},400)
      check(status['revision']==applied['revision'],'invalid Apply changed live intent')
    end
    request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[]},{'If-Match'=>'999'},409)
    request('POST','/v1/login')
    joined = await('native join and placement') { q=status; q if q.dig('bindings',0,'state')=='joined' }
    check(joined.dig('bindings',0,'initial_placement','row_offset')==-24,'upper pane relative offset incorrect')
    check(File.readlines(moves).length==1,'initial placement was not one move')
    check(request('GET','/v1/settings')['revision']==saved['revision'],'join progress wrote settings')
    # A subsequent native user move is authoritative.
    File.write(position,'11 -7')
    sleep 1.4
    check(status.dig('bindings',0,'state')=='joined' && File.readlines(moves).length==1,'user move was reasserted')
    File.write(position,'absent')
    await('native absence') { status.dig('bindings',0,'reason')=='native_client_absent_release_unknown' }
    check(File.readlines(moves).length==1,'released client was forced back')
    # Explicit terminal deletion is an observable stop, not a respawn signal.
    id=joined.dig('bindings',0,'session_id')
    request('DELETE',"/v1/terminal-sessions/#{id}")
    await('stopped') { status.dig('bindings',0,'state')=='stopped' }
    sleep 0.3
    check(status.dig('bindings',0,'session_id')==id,'stopped client respawned')
    File.write(proof,''); File.write(position,'')
    request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[binding],'retry_stopped'=>true})
    await('explicit retry') { q=status; q if q.dig('bindings',0,'state')=='joined' && q.dig('bindings',0,'session_id')!=id }
    # Durable save leaves the active policy unchanged.
    active=status
    document=request('GET','/v1/settings')['document']
    document['auxiliary_displays']=[{'output'=>'disconnected-connector','pane_ordinal'=>1}]
    request('PUT','/v1/settings',{'document'=>document})
    check(status['bindings'].map { |b| b.slice('output','pane_ordinal') }==[binding],'Save replaced live intent')
    missing=request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[{'output'=>'disconnected-connector','pane_ordinal'=>1}]})
    check(missing.dig('bindings',0,'state')=='waiting_output','missing connector did not wait')
    unavailable=request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[binding.merge('pane_ordinal'=>4)]})
    check(unavailable.dig('bindings',0,'state')=='waiting_pane','missing pane did not wait')
    occupied=request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[binding.merge('pane_ordinal'=>2)]})
    check(occupied.dig('bindings',0,'reason')=='pane_occupied','primary pane was not protected')
    request('POST','/v1/auxiliary-displays/apply',{'bindings'=>[]})
    check(status['bindings'].empty?,'clear did not replace live intent')
    stop
    $pid=spawn('sudo','-n','env',*env.map { |k,v| "#{k}=#{v}" },BINARY,PORT.to_s,out:log,err:[:child,:out])
    restored=await('restart defaults') { q=status; q if q['source']=='startup' }
    check(restored.dig('bindings',0,'output')=='disconnected-connector','restart did not load saved defaults')
    stop
    puts 'AUXILIARY_POLICY_PASS apply=volatile save=independent join=observed placement=once user_move=preserved stop=explicit_retry restart=saved_default'
  rescue Exception
    warn JSON.pretty_generate(status) rescue nil
    warn JSON.pretty_generate(request('GET','/v1/login')) rescue nil
    warn "fixture proof=#{File.read(proof).inspect} position=#{File.read(position).inspect} moves=#{File.read(moves).inspect}"
    warn File.read(log) if File.exist?(log)
    raise
  end
end
