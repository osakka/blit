#!/usr/bin/env ruby
# frozen_string_literal: true
# Control-plane negative fixture: the first comparison restart fails readiness.
# Execute the actual operator tool and require restoration through its API.
require 'json'
require 'socket'
require 'tmpdir'
require 'open3'

server = TCPServer.new('127.0.0.1', 0)
port = server.addr[1]
document = {'server'=>{'renderer'=>'opengles2'}, 'login'=>{'enabled'=>true},
            'display_layouts'=>{'DP-1'=>{'columns'=>1, 'rows'=>2}}}
revision = 1
restarts = 0
started = Process.clock_gettime(Process::CLOCK_MONOTONIC, :microsecond) - 1_000_000
errors = Queue.new
worker = Thread.new do
  loop do
    socket = server.accept
    begin
      method, path, = socket.gets.split
      headers = {}
      while (line = socket.gets) && line != "\r\n"
        key, value = line.split(':', 2)
        headers[key.downcase] = value.strip
      end
      payload = socket.read(Integer(headers.fetch('content-length', '0')))
      raise 'missing fixture auth' unless headers['authorization'] == 'Bearer fixture'
      response = case [method, path]
      when ['GET', '/v1/settings']
        {'revision'=>revision, 'document'=>document, 'storage'=>{'state'=>'clean'},
         'effective'=>{'display_backend_requested'=>document.dig('server', 'display_backend') || 'kms'}}
      when ['PUT', '/v1/settings']
        raise 'missing current revision' unless headers['if-match'] == revision.to_s
        document = JSON.parse(payload).fetch('document')
        revision += 1
        document['server']['concurrent_extension'] = 'preserve'
        {'revision'=>revision, 'storage'=>{'state'=>'clean'}}
      when ['POST', '/v1/control/restart']
        restarts += 1
        started = Process.clock_gettime(Process::CLOCK_MONOTONIC, :microsecond)
        {'state'=>'restarting'}
      when ['GET', '/v1/diagnostics/observation']
        now = Process.clock_gettime(Process::CLOCK_MONOTONIC, :microsecond)
        {'captured_monotonic_us'=>now, 'version'=>{'server_version'=>'fixture'},
         'runtime'=>{'performance'=>{'uptime_ms'=>(now-started)/1000}},
         'diagnostics'=>{'health'=>{'backend'=>'drm-kms', 'ready'=>restarts != 1},
                        'render'=>{'direct_kms'=>true, 'renderer'=>'', 'video_driver'=>'direct-kms'}},
         'login'=>{'enabled'=>document.dig('login', 'enabled')}}
      else
        raise "unexpected request #{method} #{path}"
      end
      body = JSON.generate(response)
      socket.write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: #{body.bytesize}\r\nConnection: close\r\n\r\n#{body}")
    rescue => e
      errors << e
    ensure
      socket.close
    end
  end
end
begin
  Dir.mktmpdir('blit-comparison-cleanup') do |out|
    stdout, stderr, status = Open3.capture3(
      {'BLIT_LIVE'=>"http://127.0.0.1:#{port}", 'BLIT_AUTH_TOKEN'=>'fixture',
       'BLIT_EVIDENCE_DIR'=>out, 'BLIT_COMPARISON_RESTART_TIMEOUT'=>'1'},
      'ruby', File.expand_path('../probes/host_backend_comparison.rb', __dir__))
    raise errors.pop unless errors.empty?
    raise 'failed comparison reported success' if status.success?
    raise "baseline not restored: #{stdout} #{stderr}" unless stdout.include?('BACKEND_BASELINE_RESTORED') && restarts == 2
    raise 'backend preference was not restored' if document['server'].key?('display_backend')
    raise 'renderer/login preference not restored' unless document.dig('server', 'renderer') == 'opengles2' && document.dig('login', 'enabled') == true
    raise 'unrelated configuration lost' unless document.dig('server', 'concurrent_extension') == 'preserve' && document.dig('display_layouts', 'DP-1', 'rows') == 2
    raise 'restoration observation missing' unless File.file?(File.join(out, 'restored-observation.json'))
    puts 'BACKEND_COMPARISON_CLEANUP_PASS failed_trial=reported baseline=restored unrelated_settings=preserved'
  end
ensure
  worker.kill
  worker.join
  server.close
end
