#!/usr/bin/env ruby
# Fixed-count local comparison using the existing timing gate, not a retry
# until green. Never contacts or changes the physical desktop.
require 'json'
require 'net/http'
require 'open3'
require 'tmpdir'
require 'fileutils'
require 'timeout'
require_relative '../tests/gate_port'

raise 'usage: local_frame_rate_comparison.rb BASELINE_BINARY CANDIDATE_BINARY' unless ARGV.size == 2
root = File.expand_path('../..', __dir__)
binaries = ARGV.map { |path| File.expand_path(path) }
raise 'both binaries must exist' unless binaries.all? { |path| File.executable?(path) }
out = ENV.fetch('BLIT_EVIDENCE_DIR') { Dir.mktmpdir('blit-local-timing-') }
FileUtils.mkdir_p(out)
results = []
3.times do |round|
  binaries.each_with_index do |binary, index|
    name = "#{index.zero? ? 'baseline' : 'candidate'}-#{round + 1}"
    port = gate_port(19668)
    base = URI("http://127.0.0.1:#{port}")
    env = {'SDL_VIDEODRIVER'=>'dummy', 'BLIT_RENDERER'=>'software',
      'BLIT_DISPLAY_BACKEND'=>'sdl', 'BLIT_SETTINGS'=>'ignore',
      'BLIT_LOGIN_AUTOSTART'=>'0', 'BLIT_EXPECT_DESKTOP'=>'0',
      'BLIT_BIND_ADDRESS'=>'127.0.0.1', 'BLIT_AUTH_TOKEN'=>'',
      'BLIT_FONT_SCALE'=>'1', 'BLIT_PSF_FONT'=>File.join(root,'assets/fonts/Blit-Hibr-Terminus16.psf'),
      'LD_LIBRARY_PATH'=>File.join(root,'build/sdl3-kms')}
    pid = spawn(env, binary, port.to_s, out:File.join(out,"#{name}-server.log"), err:[:child,:out])
    begin
      get = lambda do |path|
        response = Net::HTTP.start(base.host,base.port,open_timeout:1,read_timeout:3) { |h| h.get(path) }
        raise "HTTP#{response.code}" unless response.code == '200'
        JSON.parse(response.body)
      end
      version = nil
      Timeout.timeout(10) do
        loop do
          begin
            version = get.call('/v1/version'); break
          rescue SystemCallError
            sleep 0.02
          end
        end
      end
      diagnostic = get.call('/v1/diagnostics')
      raise 'wrong comparison presentation path' unless diagnostic.dig('health','backend') == 'sdl3' &&
        diagnostic.dig('render','video_driver') == 'dummy' && diagnostic.dig('render','renderer') == 'software'
      stdout, stderr, status = Open3.capture3({'BLIT_GATE_SERVER'=>base.to_s,'BLIT_GATE_AUTH_TOKEN'=>'',
        'BLIT_GATE_MAX_RENDER_MS'=>'20.000','BLIT_GATE_MIN_HZ'=>'30',
        'BLIT_GATE_SURFACE_WIDTH'=>'80','BLIT_GATE_SURFACE_HEIGHT'=>'24'},
        'ruby',File.join(root,'tools/tests/frame_rate_gate.rb'))
      File.write(File.join(out,"#{name}-gate.log"),stdout+stderr)
      entry = {run:name,version:version,passed:status.success?,exit_status:status.exitstatus,
        result:(stdout+stderr).strip,diagnostic:diagnostic}
      results << entry
      File.write(File.join(out,'results.json'),JSON.pretty_generate(results))
      puts "#{name}: #{status.success? ? 'PASS' : 'FAIL'} #{(stdout+stderr).lines.first.to_s.strip}"
      $stdout.flush
    ensure
      Process.kill('TERM',pid) rescue nil
      begin
        Timeout.timeout(10) { Process.wait(pid) }
      rescue Timeout::Error
        Process.kill('KILL',pid) rescue nil
        Process.wait(pid) rescue nil
        raise
      end
    end
  end
end
puts "LOCAL_FRAME_RATE_COMPARISON_COMPLETE runs=#{results.size} failures=#{results.count { |r| !r[:passed] }} evidence=#{out}"
exit(results.all? { |r| r[:passed] } ? 0 : 1)
