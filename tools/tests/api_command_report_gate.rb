#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'open3'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'test_profile'
base=URI("http://127.0.0.1:#{gate_port(19572)}")
Dir.mktmpdir('blit-api-report-gate') do |work|
  pid=spawn(TestProfile.environment,TestProfile.binary,base.port.to_s,
    out:File.join(work,'server.log'),err:[:child,:out])
  begin
    Timeout.timeout(8){loop{begin;break if Net::HTTP.get_response(base+'/v1/version').code=='200';rescue Errno::ECONNREFUSED;end;sleep 0.02}}
    out,err,status=Open3.capture3({'BLIT_BASE_URL'=>base.to_s,'BLIT_AUTH_TOKEN'=>''},
      '/usr/bin/ruby',File.join(TestProfile::ROOT,'tools/probes/api_command_report.rb'),
      %q{printf 'λ%.0s' {1..1000}; printf 'err%.0s' {1..50} >&2; exit 7})
    raise "report transport failed: #{err}" unless status.success?
    report=JSON.parse(out)
    raise 'chunked report truncated/corrupted' unless report=={'stdout'=>'λ'*1000,'stderr'=>'err'*50,'exit_code'=>7}
    sessions=JSON.parse(Net::HTTP.get(base+'/v1/terminal-sessions')).fetch('sessions')
    raise 'diagnostic terminal leaked' unless sessions.empty?
    puts 'API_COMMAND_REPORT_PASS chunks=lossless unicode=preserved stderr=separate exit=exact session=removed'
  ensure
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
