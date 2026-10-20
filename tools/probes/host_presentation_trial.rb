#!/usr/bin/env ruby
# API-only startup/recovery oracle, not a pixel or performance acceptance gate.
# Does not write canonical settings; normal completion exercises lease expiry.
require 'json'
require 'net/http'
require 'fileutils'
require 'tmpdir'
base = URI(ENV.fetch('BLIT_LIVE', 'http://192.168.10.54:8080'))
token = ENV.fetch('BLIT_AUTH_TOKEN')
renderer = ENV.fetch('BLIT_TRIAL_RENDERER', 'software')
raise 'renderer must be software/opengles2' unless %w[software opengles2].include?(renderer)
out = ENV.fetch('BLIT_EVIDENCE_DIR') { Dir.mktmpdir('blit-physical-trial-') }
FileUtils.mkdir_p(out)
request = lambda do |method, path, payload=nil, revision=nil|
  req = {'GET'=>Net::HTTP::Get, 'POST'=>Net::HTTP::Post}.fetch(method).new(base + path)
  req['Authorization'] = "Bearer #{token}"
  req['If-Match'] = revision.to_s if revision
  if payload
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(payload)
  end
  res = Net::HTTP.start(base.host, base.port, open_timeout: 1, read_timeout: 2) { |h| h.request(req) }
  raise "HTTP #{res.code}: #{res.body}" unless res.code.start_with?('2')
  JSON.parse(res.body)
end
save = lambda { |name, value| File.write(File.join(out, name+'.json'), JSON.pretty_generate(value)) }
before = request.call('GET', '/v1/settings')
baseline = request.call('GET', '/v1/diagnostics/observation')
raise 'requires clean native baseline' unless before.dig('storage','state')=='clean' &&
  baseline.dig('diagnostics','health','backend')=='drm-kms' && baseline.dig('diagnostics','health','ready')
save.call('baseline-settings', before); save.call('baseline-observation', baseline)
lease = request.call('POST', '/v1/control/presentation-trial',
  {renderer:renderer, timeout_seconds:30}, before.fetch('revision'))
save.call('lease', lease)
events=[]; startup=nil; restored=nil
deadline=Process.clock_gettime(Process::CLOCK_MONOTONIC)+80
begin
  loop do
    raise 'independent recovery deadline exceeded; no API-only recovery is assumed' if
      Process.clock_gettime(Process::CLOCK_MONOTONIC)>deadline
    begin
      state=request.call('GET','/v1/control/presentation-trial')
      obs=request.call('GET','/v1/diagnostics/observation')
      health=obs.dig('diagnostics','health')
      render=obs.dig('diagnostics','render')
      events << {at:Time.now.to_f, trial:state, health:health,
        driver:render['video_driver'], renderer:render['renderer'], outputs:render['outputs']}
      if state['id']==lease['id'] && state['active'] && startup.nil? &&
          obs.dig('login','enabled')==false
        startup=obs; save.call('trial-observation',obs)
        save.call('trial-settings',request.call('GET','/v1/settings'))
      end
      if state['id']==lease['id'] && state['state']=='restored' && !state['active'] &&
          health['backend']=='drm-kms' && health['ready']
        after=request.call('GET','/v1/settings')
        raise 'canonical settings changed across trial' unless after['document']==before['document'] &&
          after['revision']==before['revision'] && after.dig('storage','state')=='clean'
        raise 'output inventory changed across restoration' unless
          obs.dig('server','display_count')==baseline.dig('server','display_count')
        save.call('restored-settings',after); save.call('restored-observation',obs)
        restored=state; break
      end
    rescue SystemCallError, IOError, Net::ReadTimeout, Net::OpenTimeout => error
      events << {at:Time.now.to_f, unavailable:error.class.name}
    end
    sleep 0.5
  end
ensure
  save.call('events',events)
end
startup_ok=startup && startup.dig('diagnostics','health','backend')=='sdl3' &&
  startup.dig('diagnostics','health','ready') &&
  startup.dig('diagnostics','render','video_driver')=='kmsdrm' &&
  startup.dig('diagnostics','render','renderer')==renderer &&
  startup.dig('server','display_count')==baseline.dig('server','display_count')
result={recovery_pass:!!restored, sdl_connected_output_startup_pass:!!startup_ok,
  requested_renderer:renderer, evidence:out, acceptance:'startup_and_recovery_only_not_pixels_or_cadence'}
save.call('result',result)
puts JSON.generate(result)
