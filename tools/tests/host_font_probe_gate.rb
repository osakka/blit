#!/usr/bin/env ruby
# Exercise the real operator probe against an isolated actual server.
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require 'open3'
require_relative 'gate_port'
require_relative 'sdl_test_profile'

root = File.expand_path('../..', __dir__)
port = gate_port(19566)
base = URI("http://127.0.0.1:#{port}")
get = lambda do |path|
  response = Net::HTTP.start(base.host, base.port, open_timeout:2, read_timeout:5) { |h| h.get(path) }
  raise "#{path}: HTTP #{response.code}" unless response.code=='200'
  JSON.parse(response.body)
end
identity = ->(font) { font.reject { |key,_| key=='raster_cache' } }
Dir.mktmpdir('blit-font-probe-gate') do |work|
  env = SdlTestProfile.environment.merge('BLIT_FONT_ROOT'=>File.join(root,'assets/fonts'),
    'BLIT_SETTINGS'=>'load', 'BLIT_SETTINGS_PATH'=>File.join(work,'settings.json'))
  pid = spawn(env, SdlTestProfile.binary, port.to_s, out:File.join(work,'server.log'), err:[:child,:out])
  begin
    Timeout.timeout(10) do
      loop do
        begin
          get.call('/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
    original = identity.call(get.call('/v1/fonts').fetch('active'))
    %w[blit-modern blit-hibr-spleen16 unavailable-font].each do |profile|
      probe_env = {'BLIT_LIVE'=>base.to_s, 'BLIT_AUTH_TOKEN'=>'', 'BLIT_FONT_ID'=>profile,
        'BLIT_FONT_SIZES'=>'17,23', 'BLIT_EVIDENCE_DIR'=>File.join(work,profile)}
      output,status = Open3.capture2e(probe_env,'ruby',File.join(root,'tools/probes/host_font_size.rb'))
      if profile=='unavailable-font'
        raise "unavailable profile was not refused: #{output}" if status.success? || !output.include?('requested font profile is unavailable')
      else
        raise "probe failed: #{output}" unless status.success? &&
          output.scan("FONT_SIZE_GEOMETRY_PASS font=#{profile}").length==2 &&
          output.include?('FONT_SIZE_BASELINE_RESTORED')
      end
      raise 'probe did not restore original profile and size policy' unless
        identity.call(get.call('/v1/fonts').fetch('active'))==original
    end
    puts 'HOST_FONT_PROBE_PASS profiles=modern+retro sizes=17,23 restore=original_profile_and_policy unavailable=no_mutation'
  rescue Exception
    warn File.read(File.join(work,'server.log')); raise
  ensure
    Process.kill('TERM',pid) rescue nil
    Timeout.timeout(10) { Process.wait(pid) } rescue nil
  end
end
