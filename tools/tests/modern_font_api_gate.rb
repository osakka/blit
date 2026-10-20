#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'fileutils'
require 'timeout'
require_relative 'gate_port'
require_relative 'sdl_test_profile'

root = File.expand_path('../..', __dir__)
port = gate_port(19562)
base = URI("http://127.0.0.1:#{port}")
request = lambda do |method, path, body = nil, status = 200|
  q = {'GET'=>Net::HTTP::Get, 'PUT'=>Net::HTTP::Put, 'POST'=>Net::HTTP::Post}.fetch(method).new(base + path)
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  r = Net::HTTP.start(base.host, base.port, read_timeout: 5) { |h| h.request(q) }
  raise "HTTP #{r.code}: #{r.body}" unless r.code.to_i == status
  JSON.parse(r.body)
end
Dir.mktmpdir('blit-modern-api') do |work|
  fonts = File.join(work, 'fonts'); Dir.mkdir(fonts)
  %w[Blit-Hibr-Terminus16.psf Blit-Hibr-Spleen16.psf BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf NotoColorEmoji.ttf].each do |asset|
    FileUtils.copy_file(File.join(root, 'assets/fonts', asset), File.join(fonts, asset))
  end
  env = SdlTestProfile.environment.merge('BLIT_SETTINGS'=>'load',
    'BLIT_SETTINGS_PATH'=>File.join(work, 'settings.json'), 'BLIT_FONT_ROOT'=>fonts)
  pid = nil
  launch = lambda do
    pid = spawn(env, SdlTestProfile.binary, port.to_s, out:File.join(work, 'server.log'), err:[:child,:out])
    Timeout.timeout(10) do
      loop do
        begin
          request.call('GET', '/v1/version'); break
        rescue Errno::ECONNREFUSED
          sleep 0.02
        end
      end
    end
  end
  stop = lambda do
    Process.kill('TERM', pid)
    Timeout.timeout(10) { Process.wait(pid) }
    pid = nil
  end
  begin
    launch.call
    result = request.call('PUT', '/v1/fonts', {font_id:'blit-modern', size_px:23})
    raise 'native face not selected' unless result.dig('active', 'rasterization') == 'native-antialiased'
    raise 'colour support absent' unless result.dig('active', 'colour_glyphs') == true
    %w[blit-retro blit-modern].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      (8..64).each do |size|
        active = request.call('PUT', '/v1/fonts', {size_px:size}).fetch('active')
        raise "incorrect #{profile} #{size}px metrics" unless active['size_px'] == size && active.dig('cell_pixels','height') == size && active['id'] == profile && active['rasterization'] == 'native-antialiased'
      end
    end
    request.call('PUT', '/v1/fonts', {size_px:23})
    [1.1, 1.7, 2.3, 3.0].each do |scale|
      active = request.call('PUT', '/v1/fonts', {scale:scale}).fetch('active')
      raise 'modern legacy scale policy failed' unless active['sizing'] == 'scale' && active['scale'] == scale && active['rasterization'] == 'native-antialiased'
    end
    request.call('PUT', '/v1/fonts', {size_px:23})
    active = request.call('PUT', '/v1/fonts', {font_id:'blit-hibr-spleen16'}).fetch('active')
    raise 'retro policy changed' unless active['size_px'] == 23 && active['rasterization'] == 'bitmap-nearest'
    active = request.call('PUT', '/v1/fonts', {font_id:'blit-modern'}).fetch('active')
    raise 'modern policy changed' unless active['size_px'] == 23 && active['rasterization'] == 'native-antialiased'
    colour = File.join(fonts, 'NotoColorEmoji.ttf')
    %w[blit-modern blit-retro].each do |profile|
      request.call('PUT', '/v1/fonts', {font_id:profile})
      File.rename(colour, colour + '.missing')
      begin
        request.call('PUT', '/v1/fonts', {size_px:24}, 422)
        active = request.call('GET', '/v1/fonts').fetch('active')
        raise 'failed change destroyed active font' unless active['size_px'] == 23 && active['id'] == profile
      ensure
        File.rename(colour + '.missing', colour)
      end
      Timeout.timeout(5) do
        loop do
          settings = request.call('GET', '/v1/settings')
          break if settings.dig('storage','state') == 'clean'
          sleep 0.05
        end
      end
      font = request.call('GET', '/v1/settings').dig('document','font')
      raise 'competing saved size policies' unless font['font_id'] == profile && font['size_px'] == 23 && !font.key?('scale')
      stop.call; launch.call
      active = request.call('GET', '/v1/fonts').fetch('active')
      raise 'restart lost native font' unless active['id'] == profile && active['size_px'] == 23 && active['rasterization'] == 'native-antialiased'
    end
    puts 'MODERN_FONT_API_PASS sizes=57_each profiles=bitmap+modern+smooth_retro sizing=preserved save_restore=canonical missing_colour=rollback'
  rescue Exception
    warn File.read(File.join(work,'server.log'))
    raise
  ensure
    stop.call if pid
  end
end
