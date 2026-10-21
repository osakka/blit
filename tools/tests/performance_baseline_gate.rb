#!/usr/bin/env ruby
require 'socket'
require 'json'
require 'open3'
root = File.expand_path('../..', __dir__)
%w[valid reset changed_backend].each do |mode|
  server = TCPServer.new('127.0.0.1', 0)
  port = server.addr[1]
  runtimes = 0
  observations = 0
  requests = []
  worker = Thread.new do
    loop do
      client = server.accept
      begin
        method, path = client.gets.split
        requests << [method, path]
        while (line = client.gets) && line != "\r\n"; end
        body = case path
        when '/v1/version'
          { server_version: 'fixture', api_version: '0.1' }
        when '/v1/diagnostics'
          observations += 1
          { render: { direct_kms: true,
              video_driver: mode == 'changed_backend' && observations > 1 ? 'other' : 'direct-kms',
              renderer: '', font_scale: 1,
              outputs: [{ display_id: 'fixture', render_width: 800, render_height: 600,
                          refresh_hz: 60, kms_phases: { draw_ms: 2 } }] } }
        when '/v1/diagnostics/runtime'
          runtimes += 1
          offset = runtimes == 1 ? 0 : 1
          { performance: { uptime_ms: 1000 + offset * 2000,
              process_cpu_ms: mode == 'reset' && offset == 1 ? 0 : 100 + offset * 20,
              terminal_bytes: 500 + offset * 200, present_calls: 10 + offset * 3,
              missed_deadlines: offset } }
        else
          raise "unexpected route #{path}"
        end
        json = JSON.generate(body)
        client.write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: #{json.bytesize}\r\nConnection: close\r\n\r\n#{json}")
      ensure
        client.close
      end
    end
  rescue IOError, Errno::EBADF
    # Fixture server closed by the test owner.
  end
  begin
    out, err, status = Open3.capture3({ 'BLIT_LIVE' => "http://127.0.0.1:#{port}",
      'BLIT_AUTH_TOKEN' => 'fixture-secret' }, 'ruby',
      File.join(root, 'tools/probes/performance_baseline.rb'), '2')
    raise 'non-read-only probe' unless requests.all? { |m, _| m == 'GET' }
    raise 'credential leaked' if (out + err).include?('fixture-secret')
    if mode == 'valid'
      raise err unless status.success?
      r = JSON.parse(out)
      raise 'counter units incorrect' unless r.fetch('server_cpu_percent_one_core') == 1 &&
        r.fetch('terminal_bytes_per_second') == 100 && r.fetch('present_calls_delta') == 3
    else
      raise "accepted #{mode}" if status.success?
      raise "wrong rejection #{err}" unless err.include?('discard comparison')
    end
  ensure
    server.close
    worker.join
  end
end
puts 'PERFORMANCE_BASELINE_PASS units=exact reset=reject backend_change=reject methods=GET credentials=absent'
