#!/usr/bin/env ruby
# A program built as Blit's are -- tcc compiles with -g, the system linker
# links through tools/tcc-link.sh -- and Blit's crash handler
# (src/blit_crash.h): its stack is declared not executable (PT_GNU_STACK
# read-write) with RELRO; it runs normally; a fault ends it by the signal
# with a backtrace on standard error, and addr2line (where binutils is
# installed) names the static function that faulted and its caller, with
# the fixture's file and line from tcc's debug information.
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
Dir.mktmpdir('blit-fault-trace-') do |work|
  object = File.join(work, 'fixture.o')
  executable = File.join(work, 'fixture')
  raise 'fixture compile failed' unless system('tcc', '-g', '-std=c99', '-c',
                                               File.join(root, 'tools/tests/tcc_fault_trace_fixture.c'), '-o', object)
  raise 'fixture link failed' unless system(File.join(root, 'tools/tcc-link.sh'), 'exe', executable, object, '--', '-ldl')
  headers, = Open3.capture2('readelf', '-lW', executable)
  raise "no read-write PT_GNU_STACK:\n#{headers}" unless headers =~ /GNU_STACK(\s+0x\h+){5}\s+RW\s/
  raise "no PT_GNU_RELRO:\n#{headers}" unless headers.include?('GNU_RELRO')
  _, error, status = Open3.capture3(executable)
  raise "normal execution changed: #{error}" unless status.success?
  _, error, status = Open3.capture3(executable, '--fault')
  raise "the fault did not end it by SIGSEGV (#{status.inspect}): #{error}" unless status.signaled? && status.termsig == 11
  raise "no backtrace:\n#{error}" unless error.include?('blit: fatal signal 11 (SIGSEGV)') && error =~ /^  #1 0x\h+/
  lines = if File.executable?('/usr/bin/addr2line')
            raise "addr2line did not name the faulting function and its caller with file and line:\n#{error}" unless
              error.include?('deliberate_fault') && error.include?('caller_of_the_fault') &&
              error =~ /tcc_fault_trace_fixture\.c:\d+/
            'function+file:line(addr2line)'
          else
            'addresses(no binutils)'
          end
  puts "TCC_FAULT_TRACE_PASS link=tcc-link.sh stack=RW relro normal=0 fault=SIGSEGV backtrace=frames #{lines}"
end
