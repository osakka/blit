#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT,'build/blit/blit-server'))
PORT = gate_port(19564)
BASE = URI("http://127.0.0.1:#{PORT}")
def request(method, path, body=nil, revision=nil, status=200)
  q={'GET'=>Net::HTTP::Get,'PUT'=>Net::HTTP::Put,'POST'=>Net::HTTP::Post}.fetch(method).new(BASE+path)
  q['Content-Type']='application/json';q['If-Match']=revision.to_s if revision
  q.body=JSON.generate(body) if body
  r=Net::HTTP.start(BASE.host,BASE.port,open_timeout:2,read_timeout:5){|h|h.request(q)}
  raise "#{method} #{path}: #{r.code}:#{r.body}" unless r.code.to_i==status
  JSON.parse(r.body)
end

cases=[[nil,nil,'default',true],['sdl',nil,'environment',true],
       ['kms',nil,'environment',true],['kmsdrm',nil,'environment',false],
       ['ksmdrm',nil,'environment',false],['',nil,'environment',false],
       ['SDL',nil,'environment',false],['bogus"backend',nil,'environment',false],
       ['kmsdrm','sdl','saved-settings',true],['sdl','kms','saved-settings',true],
       ['kms','sdl','saved-settings',true]]
cases.each_with_index do |(environment,saved,source,valid),index|
  Dir.mktmpdir('blit-config-source') do |work|
    path=File.join(work,'settings.json');log=File.join(work,'server.log')
    File.write(path,JSON.generate(version:1,revision:1,input_devices:{},server:{display_backend:saved})) if saved
    # SDL path only (reports SDL renderer identity); retired at the GLES cutover.
    env={**gate_display_env(backend: 'sdl'),'BLIT_RENDERER'=>index==0 ? nil : 'software',
         'BLIT_DISPLAY_BACKEND'=>environment,'BLIT_DRM_DEVICE'=>File.join(work,'absent-drm'),
         'BLIT_SETTINGS_PATH'=>path,'BLIT_SETTINGS'=>nil,'BLIT_AUTH_TOKEN'=>'',
         'BLIT_LOGIN_AUTOSTART'=>'0','BLIT_EXPECT_DESKTOP'=>'0','BLIT_BIND_ADDRESS'=>'127.0.0.1',
         'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
         'LD_LIBRARY_PATH'=>File.join(ROOT,'build/sdl3-kms')}
    pid=spawn(env,BINARY,PORT.to_s,out:log,err:[:child,:out])
    begin
      Timeout.timeout(10){loop{begin;request('GET','/v1/version');break;rescue Errno::ECONNREFUSED;sleep 0.02;end}}
      state=request('GET','/v1/settings');p=state.dig('effective','presentation_configuration')
      raise "provenance missing: case#{index}" unless p
      raise "wrong source: #{p}" unless p['backend_source']==source && p['backend_environment']==environment
      env_valid=environment.nil? || %w[kms sdl].include?(environment)
      raise 'validity mismatch' unless p['backend_environment_valid']==env_valid && p['configuration_valid']==valid
      raise 'override mismatch' unless p['backend_environment_overridden']==(!saved.nil? && !environment.nil?)
      raise 'renderer provenance missing' unless p['renderer_source']==(index==0 ? 'default' : 'environment')
      diag=request('GET','/v1/diagnostics')
      selected=saved || environment || 'sdl'
      if !valid
        raise 'invalid default initialized a display backend' unless !diag.dig('health','ready') && p['video_driver_active']=='unavailable'
        raise 'validation error not reported' unless diag.dig('health','error').to_s.include?('BLIT_DISPLAY_BACKEND')
        raise 'startup warning absent' unless File.read(log).include?('BLIT_DISPLAY_BACKEND accepts only kms or sdl')
        # Repair through the sole writable owner, without console/env editing.
        doc=state.fetch('document');doc['server']||={};doc['server']['display_backend']='sdl'
        repaired=request('PUT','/v1/settings',{document:doc},state.fetch('revision'))
        raise 'repair did not become authoritative' unless repaired.dig('effective','presentation_configuration','configuration_valid')
        request('POST','/v1/control/restart',{},nil,202)
        Timeout.timeout(10){loop{begin;break if request('GET','/v1/diagnostics').dig('health','ready');rescue Errno::ECONNREFUSED,Errno::ECONNRESET,EOFError;end;sleep 0.02}}
        p=request('GET','/v1/settings').dig('effective','presentation_configuration')
        raise 'repaired source incorrect' unless p['backend_source']=='saved-settings' && p['backend_environment_valid']==false
      elsif selected=='kms'
        raise 'native renderer source misleading' unless p['renderer_applicable']==false && p['video_driver_active']=='not-applicable'
      else
        raise 'SDL identity missing' unless p['renderer_applicable'] && p['video_driver_active']=='dummy' && diag.dig('health','ready')
      end
      puts "PRESENTATION_SOURCE_CASE_PASS case=#{index} source=#{source} valid=#{valid}"
    rescue Exception
      warn File.read(log);raise
    ensure
      Process.kill('TERM',pid) rescue nil
      Timeout.timeout(10){Process.wait(pid)} rescue nil
    end
  end
end
puts 'PRESENTATION_PROVENANCE_PASS cases=11 invalid=no_display API=reachable repair=saved_settings restart=verified'
