#!/usr/bin/env ruby
# frozen_string_literal: true

# Test the actual operator probe's measurement semantics against a TCP fixture.
# Server observations span exactly 2s irrespective of client request timing.
require 'json'
require 'socket'
require 'open3'
require 'tmpdir'
require 'fileutils'

[[true,:normal],[false,:normal],[true,:restart],[true,:removed],[true,:refresh],[true,:zero_window]].each do |native,scenario|
  server = TCPServer.new('127.0.0.1',0)
  port = server.addr[1]
  observations = 0
  errors = Queue.new
  worker = Thread.new do
    loop do
      socket = server.accept
      begin
        method,path, = socket.gets.split
        headers = {}
        while (line = socket.gets) && line != "\r\n"
          key,value = line.split(':',2)
          headers[key.downcase] = value.strip
        end
        payload = socket.read(Integer(headers.fetch('content-length','0')))
        raise 'missing fixture auth' unless headers['authorization'] == 'Bearer fixture'
        if method == 'GET' && path == '/v1/diagnostics/observation'
          later = observations.positive?
          observations += 1
          perf = %w[present_calls loop_iterations input_events evdev_mouse_events terminal_bytes].to_h { |k| [k,later ? 200 : 100] }
          perf['uptime_ms'] = later ? 3000 : 1000
          perf['uptime_ms'] = 500 if later && scenario == :restart
          perf['uptime_ms'] = 1000 if scenario == :zero_window
          perf['process_cpu_ms'] = later ? 20 : 10
          output = {'display_id'=>'display-1','refresh_hz'=>60,'target_interval_us'=>16667,
                    'present_calls'=>later ? 200 : 100,'missed_deadlines'=>0,
                    'kms_scanout'=>{'available'=>native,'flip_completions'=>later ? 170 : 80}}
          output['refresh_hz'] = 75 if later && scenario == :refresh
          body = {'version'=>{'server_version'=>'fixture'},'captured_monotonic_us'=>later ? 3_000_000 : 1_000_000,
                  'runtime'=>{'performance'=>perf},'login'=>{},
                  'displays'=>{'count'=>1,'displays'=>[{'id'=>'display-1','bounds'=>{'x'=>0,'y'=>0,'width'=>1024,'height'=>768}}]},
                  'diagnostics'=>{'render'=>{'direct_kms'=>native,'pointer'=>{'valid'=>true,'x'=>100,'y'=>100,'output'=>0},
                    'outputs'=>[output],'hotplug'=>{}}}}
          body['captured_monotonic_us'] = 1_000_000 if scenario == :zero_window
          body['diagnostics']['render']['outputs'] = [] if later && scenario == :removed
        elsif method == 'POST' && path == '/v1/input/inject'
          raise 'not pointer motion' unless JSON.parse(payload)['kind'] == 'pointer_motion'
          body = {'accepted'=>true}
        elsif method == 'GET' && ['/v1/diagnostics/trace?kind=pointer_motion&limit=128',
                                 '/v1/diagnostics/trace?kind=render_path&limit=128'].include?(path)
          body = {'revision'=>1,'events'=>[]}
        else
          raise "unexpected request #{method} #{path}"
        end
        encoded = JSON.generate(body)
        socket.write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: #{encoded.bytesize}\r\nConnection: close\r\n\r\n#{encoded}")
      rescue => error
        errors << error
      ensure
        socket.close
      end
    end
  end
  evidence = Dir.mktmpdir('blit-cadence-observation-')
  begin
    command = ['ruby',File.expand_path('../probes/host_pointer_cadence.rb',__dir__)]
    if ENV['BLIT_CADENCE_GATE_BASELINE'] == '1'
      source,status = Open3.capture2('git','show','HEAD:tools/probes/host_pointer_cadence.rb')
      raise 'baseline source unavailable' unless status.success?
      command = ['ruby','-e',source]
    end
    output,error,status = Open3.capture3(
      {'BLIT_LIVE'=>"http://127.0.0.1:#{port}",'BLIT_AUTH_TOKEN'=>'fixture',
       'BLIT_POINTER_HZ'=>'240','BLIT_POINTER_SECONDS'=>'0.25','BLIT_POINTER_ASYNC'=>'1',
       'BLIT_EVIDENCE_DIR'=>evidence},*command)
    raise errors.pop unless errors.empty?
    if scenario != :normal
      expected = {restart:'server restarted',removed:'output inventory changed',
                  refresh:'output topology or refresh changed',zero_window:'invalid server observation interval'}.fetch(scenario)
      raise "invalid sample accepted: #{scenario}: #{error}" if status.success? || !error.include?(expected)
      next
    end
    raise "probe failed: #{error}" unless status.success?
    result = JSON.parse(output)
    raise 'server measurement interval not used' unless result['server_observation_window_s'] == 2.0
    measured = result.fetch('output_submissions').first
    raise 'submission rate uses client timing' unless measured['submission_hz'] == 50.0
    expected = native ? 45.0 : nil
    raise 'completion availability/rate fabricated' unless measured['confirmed_flip_hz'] == expected
    raise 'ambiguous presentation label survived' if measured.key?('presentation_hz')
    raise 'restoration contamination not labelled' unless result['restoration_motion_included'] == true
    %w[before after pointer_motion-tail render_path-tail].each do |artifact|
      raise "missing observation/trace artifact #{artifact}" unless
        File.file?(File.join(evidence,"pointer-cadence-#{artifact}.json"))
    end
  ensure
    worker.kill
    worker.join
    server.close
    FileUtils.remove_entry(evidence)
  end
end
puts 'POINTER_CADENCE_OBSERVATION_PASS server_interval=exact submissions=separate native_completions=45 SDL_completions=unavailable invalid_samples=4_rejected'
