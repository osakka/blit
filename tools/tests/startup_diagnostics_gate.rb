#!/usr/bin/env ruby
require 'tmpdir'
require 'open3'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-startup-diagnostics-') do |work|
  exe=File.join(work,'fixture')
  raise 'compile failed' unless system('tcc','-std=c99',
    File.join(root,'tools/tests/startup_diagnostics_fixture.c'),'-o',exe)
  [nil,'invalid'].each do |id|
    out,err,status=Open3.capture3({'BLIT_PRESENTATION_TRIAL_ID'=>id},exe)
    raise 'normal/invalid marker leaked diagnostics' unless status.success? && out.empty? && err.empty?
  end
  out,err,status=Open3.capture3({'BLIT_PRESENTATION_TRIAL_ID'=>'1'*32},exe)
  raise 'trial diagnostics missing' unless status.success? && out.empty? &&
    err.include?('stage=fixture display=57') &&
    err.match?(/map=[0-9a-f]+-[0-9a-f]+ r-xp [0-9a-f]+.*libc\.so\.6/) && err.include?(exe)
  puts 'STARTUP_DIAGNOSTICS_PASS normal=silent invalid=silent trial=stages_and_executable_maps signal_handler_work=none'
end
