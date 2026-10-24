#!/usr/bin/env ruby
# frozen_string_literal: true

# A bounded burst must not wait one simulated display refresh per connection.
# Only SDL present duration is injected; HTTP, input and renderer code are real.
require 'json'
require 'net/http'
require 'socket'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'sdl_test_profile'
# SDL path only (preloads a simulated slow SDL present); retired at the GLES cutover.
SdlTestProfile.sdl_only!

port = gate_port(Integer(ENV.fetch('BLIT_HTTP_ACCEPT_BUDGET_PORT','19941')))
base = URI("http://127.0.0.1:#{port}")
def request(base,method,path,body=nil)
  q = {'GET'=>Net::HTTP::Get,'POST'=>Net::HTTP::Post}.fetch(method).new(base+path)
  q['Content-Type'] = 'application/json'
  q.body = JSON.generate(body) if body
  response = Net::HTTP.start(base.host,base.port,open_timeout:2,read_timeout:4) { |h| h.request(q) }
  raise "#{path}: HTTP #{response.code}" unless response.code.start_with?('2')
  JSON.parse(response.body)
end

Dir.mktmpdir('blit-http-accept-budget-') do |directory|
  library = File.join(directory,'present.so')
  root = SdlTestProfile::ROOT
  raise 'present fixture compile failed' unless system('tcc','-std=c99','-shared','-fPIC',
    '-I'+File.join(root,'vendor/SDL3/include'),
    File.join(root,'tools/tests/fixtures/sdl_present_delay.c'),'-ldl','-o',library)
  arm = File.join(directory,'arm'); entered = File.join(directory,'entered')
  environment = SdlTestProfile.environment.merge('LD_PRELOAD'=>library,
    'BLIT_TEST_PRESENT_ARM'=>arm,'BLIT_TEST_PRESENT_ENTERED'=>entered)
  pid = spawn(environment,SdlTestProfile.binary,port.to_s,
    out:File.join(directory,'server.log'),err:[:child,:out])
  clients = []; readers = []
  begin
    diagnostics = Timeout.timeout(10) do
      loop do
        begin
          break request(base,'GET','/v1/diagnostics')
        rescue SystemCallError,IOError
          sleep 0.02
        end
      end
    end
    SdlTestProfile.verify!(diagnostics)
    File.write(arm,'1')
    request(base,'POST','/v1/input/inject',{kind:'pointer_motion',x:400,y:400})
    Timeout.timeout(3) { sleep 0.001 until File.exist?(entered) }
    8.times do |i|
      socket = TCPSocket.new(base.host,port)
      payload = JSON.generate(kind:'pointer_motion',x:100+i,y:100)
      socket.write("POST /v1/input/inject HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: #{payload.bytesize}\r\nConnection: close\r\n\r\n#{payload}")
      clients << socket
    end
    started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    File.unlink(arm)
    readers = clients.map do |socket|
      Thread.new do
        response = socket.read
        raise "bad burst response: #{response[0,100]}" unless response.start_with?('HTTP/1.1 2')
        1000 * (Process.clock_gettime(Process::CLOCK_MONOTONIC)-started)
      end
    end
    times = Timeout.timeout(4) { readers.map(&:value) }
    raise "HTTP_ACCEPT_BUDGET_FAIL burst=8 delay_ms=16 responses_ms=#{times.map{|v|v.round(2)}} max_budget_ms=80" if times.max > 80
    request(base,'GET','/v1/version')
    puts "HTTP_ACCEPT_BUDGET_PASS burst=8 simulated_present_ms=16 max_response_ms=#{times.max.round(2)} budget_ms=80"
  ensure
    File.unlink(arm) if File.exist?(arm)
    clients.each { |socket| socket.close rescue nil }
    readers.each { |thread| thread.kill; thread.join }
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
