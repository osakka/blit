#!/usr/bin/env ruby
require 'tmpdir'
require 'open3'
root=File.expand_path('../..',__dir__)
Dir.mktmpdir('blit-kms-pointer-phase') do |dir|
  binary=File.join(dir,'gate')
  args=['tcc','-O2','-std=c99','-I'+File.join(root,'build/deps/prefix/include'),
    '-I'+File.join(root,'build/deps/prefix/include/libdrm'),File.join(root,'tools/tests/kms_pointer_phase_gate.c'),
    File.join(root,'src/blit_font.c'),'-L'+File.join(root,'build/deps/prefix/lib/x86_64-linux-gnu'),'-ldrm','-lz','-o',binary]
  out,err,status=Open3.capture3(*args);abort(out+err) unless status.success?
  out,err,status=Open3.capture3(binary,dir)
  abort("pointer phase fixture failed #{status}:\n#{out}\n#{err}") unless status.success?
  puts out
end
