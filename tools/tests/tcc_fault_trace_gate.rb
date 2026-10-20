#!/usr/bin/env ruby
require 'tmpdir'
require 'open3'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-fault-trace-') do |work|
  executable=File.join(work,'fixture')
  raise 'debug fixture compile failed' unless system('tcc','-g','-bt8','-std=c99',
    File.join(root,'tools/tests/tcc_fault_trace_fixture.c'),'-o',executable)
  _,error,ok=Open3.capture3(executable)
  raise "normal execution changed: #{error}" unless ok.success?
  _,error,status=Open3.capture3(executable,'--fault')
  raise 'fault did not fail' if status.success?
  raise "no function/source fault trace: #{error}" unless error.include?('deliberate_fault') &&
    error.include?('tcc_fault_trace_fixture.c:') && error.include?('main')
  puts 'TCC_FAULT_TRACE_PASS normal=0 fault=nonzero source=present function=present callers=present bounds_instrumentation=off'
end
