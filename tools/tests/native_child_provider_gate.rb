#!/usr/bin/env ruby
# Execute the actual Hibr RC and launcher with installed Hibr; no core edits.
require 'open3'
require 'tmpdir'
require 'json'
root=File.expand_path('../..',__dir__)
rc=ENV.fetch('BLIT_GATE_RUNTIME_RC',File.join(root,'hibr/blit-runtime-console.hibr'))
launcher=File.join(root,'hibr/blit-session.hibr')
adapter=ENV.fetch('BLIT_GATE_ADAPTER',File.join(root,'build/tests/blit-child-provider.so'))
unless ENV['BLIT_GATE_ADAPTER']
  unless ENV['BLIT_HIBR_ROOT'] && File.file?(File.join(ENV['BLIT_HIBR_ROOT'],'include/hibr.h'))
    puts 'NATIVE_CHILD_PROVIDER_SKIP requires=pinned_hibr_headers';exit 77
  end
  raise 'pinned adapter build failed' unless system(File.join(root,'tools/build-tcc.sh'),adapter,'adapter')
end
raise 'build the pinned adapter first; set BLIT_GATE_ADAPTER' unless File.file?(adapter)
Dir.mktmpdir('blit-child-provider') do |home|
  env={'HOME'=>home,'XDG_CONFIG_HOME'=>home,'HIBR_RC'=>rc,'BLIT_HIBR_ADAPTER'=>adapter,
    'BLIT_SURFACE_ID'=>nil,'BLIT_SESSION_ID'=>nil,'BLIT_API_TOKEN'=>nil}
  [nil,'terminal','api'].each do |provider|
    out,err,status=Open3.capture3(env.merge('BLIT_HIBR_PROVIDER'=>provider),'/usr/bin/hibr','-i','-c','need display; mod list')
    raise "provider #{provider}: #{err}" unless status.success?
    expected=provider=='api' ? 'console-blit' : 'console'
    providers=out.lines.map{|line|line.split.first}.grep(/\Aconsole(?:-blit)?\z/)
    raise "wrong provider for #{provider.inspect}: #{out} #{err}" unless providers==[expected]
  end
  # Use a fixture session entrypoint, not a second desktop or startup engine.
  fixture=File.join(home,'session.hibr')
  File.write(fixture,'printf "RC=%s;PROVIDER=%s;TERMMS=%s\\n" "${HIBR_RC-unset}" "$BLIT_HIBR_PROVIDER" "${DT_TERMMS-unset}"')
  [nil,rc].each do |explicit|
    out,err,status=Open3.capture3(env.merge('HIBR_RC'=>explicit,'BLIT_HIBR_PROVIDER'=>'terminal',
      'BLIT_HIBR_DESKTOP_SESSION'=>fixture),'/usr/bin/hibr',launcher)
    raise "launcher failed: #{err}" unless status.success?
    raise "native launcher injected/changed RC: #{out}" unless out.include?("RC=#{explicit || 'unset'};PROVIDER=terminal")
  end
  # Gitea #164: DT_TERMMS is Hibr's setting (Control Panel > Desktop > Frames
  # For Output, ADR 0229): the launcher leaves it alone -- unset stays unset,
  # so Hibr's default and the person's saved value apply, and a caller's
  # value reaches the desktop unchanged.
  [[nil,'unset'],['33','33']].each do |given,want|
    out,err,status=Open3.capture3(env.merge('BLIT_HIBR_PROVIDER'=>'terminal','DT_TERMMS'=>given,
      'BLIT_HIBR_DESKTOP_SESSION'=>fixture),'/usr/bin/hibr',launcher)
    raise "launcher failed: #{err}" unless status.success?
    raise "launcher changed DT_TERMMS (given #{given.inspect}): #{out}" unless out.include?("TERMMS=#{want}")
  end
end
puts 'NATIVE_CHILD_PROVIDER_PASS native=console stale_rc=safe explicit_api=console_blit user_rc=preserved termms=hibr_owned'
