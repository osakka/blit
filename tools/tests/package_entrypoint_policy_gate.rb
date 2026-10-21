#!/usr/bin/env ruby
# Apply the exact packaging policy, not a second equivalent implementation.
require 'open3'
root=File.expand_path('../..',__dir__)
pattern=File.join(root,'tools/release/runtime-entrypoints.pattern')
bad=%w[./usr/bin/start-blit ./bin/run-blit ./usr/lib/blit/bin/hibr-launch
  ./usr/share/blit/hibr/attach-blit ./usr/share/blit/hibr/hibr
  ./usr/lib/blit/fonts/not-an-icon.sh]
good=%w[./usr/lib/blit/fonts/icons/hibr-pen.svg
  ./usr/share/doc/blit/docs/hibr-integration.md ./usr/bin/blit
  ./usr/lib/blit/bin/blit-server ./usr/share/blit/hibr/blit-login.hibr]
(bad+good).each do |path|
  _,err,status=Open3.capture3('grep','-E','-f',pattern,stdin_data:"-rw-r--r-- root/root 100 2026-10-21 #{path}\n")
  raise "grep failed: #{err}" unless [0,1].include?(status.exitstatus)
  raise "runtime boundary misclassified #{path}" unless status.success? == bad.include?(path)
end
puts "PACKAGE_ENTRYPOINT_POLICY_PASS rejected=#{bad.size} legitimate_assets=#{good.size} policy=shared"
