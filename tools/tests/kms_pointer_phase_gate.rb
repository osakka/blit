#!/usr/bin/env ruby
require 'tmpdir'
require 'open3'
require_relative 'build_flags'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-kms-pointer-phase') do |dir|
  binary=File.join(dir,'gate')
  args=['tcc','-O2','-std=c99',*BuildFlags.cflags('drm','zlib'),File.join(root,'tools/tests/kms_pointer_phase_gate.c'),
    File.join(root,'src/blit_font.c'),*BuildFlags.libs('drm','zlib'),'-o',binary]
  out,err,status=Open3.capture3(*args);abort(out+err) unless status.success?
  out,err,status=Open3.capture3(binary,dir)
  abort("pointer phase fixture failed #{status}:\n#{out}\n#{err}") unless status.success?
  puts out
end
