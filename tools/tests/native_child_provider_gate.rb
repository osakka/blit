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
  File.write(fixture,'printf "RC=%s;PROVIDER=%s\\n" "${HIBR_RC-unset}" "$BLIT_HIBR_PROVIDER"')
  [nil,rc].each do |explicit|
    out,err,status=Open3.capture3(env.merge('HIBR_RC'=>explicit,'BLIT_HIBR_PROVIDER'=>'terminal',
      'BLIT_HIBR_DESKTOP_SESSION'=>fixture),'/usr/bin/hibr',launcher)
    raise "launcher failed: #{err}" unless status.success?
    raise "native launcher injected/changed RC: #{out}" unless out.include?("RC=#{explicit || 'unset'};PROVIDER=terminal")
  end
end
puts 'NATIVE_CHILD_PROVIDER_PASS native=console stale_rc=safe explicit_api=console_blit user_rc=preserved'
