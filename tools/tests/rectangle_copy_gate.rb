#!/usr/bin/env ruby
require 'tmpdir'
require_relative 'build_flags'
root=File.expand_path('../..',__dir__)
Dir.chdir(root) do
  Dir.mktmpdir('blit-rectangle-copy') do |work|
    binary=File.join(work,'gate')
    raise 'compile failed' unless system('tcc','-O2','-std=c99',
      *BuildFlags.cflags('server'),
      'tools/tests/rectangle_copy_gate.c',*BuildFlags.server_sources,
      *BuildFlags.libs('server'),'-o',binary)
    command=[binary]
    command=['valgrind','--error-exitcode=99','--leak-check=full','--errors-for-leak-kinds=definite,indirect',*command] if ENV['BLIT_RECTANGLE_VALGRIND']=='1'
    raise 'rectangle copy failed' unless system(*command)
  end
end
