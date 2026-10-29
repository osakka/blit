# frozen_string_literal: true

# ADR 0181 step 1. The display a gate's server presents to, chosen once for
# the whole suite by BLIT_GATE_BACKEND:
#
#   native (default)  the production direct-KMS compositor, headless: the DRM
#                     boundary is tools/tests/fixtures/drm_hotplug_mock.c
#                     (LD_PRELOAD), one 1024x768 output named display-1.
#   gles              native with ADR 0181's GLES compositor on Mesa's
#                     surfaceless platform: every frame is drawn by GLES and
#                     read back, so screenshots show what GLES produced.
#
# Native KMS is the only presentation path (ADR 0184).
#
# A gate merges gate_display_env into its server environment and reads
# screenshots from /v1/diagnostics/screenshots/display-1 either way.

require 'digest'
require 'fileutils'
require 'tmpdir'

GATE_ROOT = File.expand_path('../..', __dir__)
GATE_BACKEND = ENV.fetch('BLIT_GATE_BACKEND', 'native')
abort "BLIT_GATE_BACKEND must be native or gles, not #{GATE_BACKEND}" unless
  %w[native gles].include?(GATE_BACKEND)

# The mock is compiled once per source revision and shared by every gate.
def gate_drm_mock
  source = File.join(GATE_ROOT, 'tools/tests/fixtures/drm_hotplug_mock.c')
  digest = Digest::SHA256.file(source).hexdigest[0, 16]
  path = File.join(Dir.tmpdir, "blit-gate-drm-mock-#{Process.uid}-#{digest}.so")
  return path if File.exist?(path)
  partial = "#{path}.#{Process.pid}"
  ok = system('tcc', '-shared', '-fPIC',
              '-I' + File.join(GATE_ROOT, 'build/deps/prefix/include'),
              '-I' + File.join(GATE_ROOT, 'build/deps/prefix/include/libdrm'),
              source, '-o', partial)
  abort 'gate_server: the DRM mock does not compile' unless ok
  File.rename(partial, path)
  path
end

def gate_display_env(width: 1024, height: 768, backend: GATE_BACKEND)
  directory = Dir.mktmpdir('blit-gate-display')
  at_exit { FileUtils.rm_rf(directory) }
  device = File.join(directory, 'drm')
  state = File.join(directory, 'state')
  File.write(device, '')
  File.write(state, "1 0 0 0\n1 #{width} #{height}\n")
  env = { 'BLIT_DISPLAY_BACKEND' => 'kms', 'BLIT_DRM_DEVICE' => device,
          'BLIT_DRM_MOCK_STATE' => state, 'LD_PRELOAD' => gate_drm_mock }
  return env.merge('BLIT_KMS_COMPOSITOR' => 'cpu') unless backend == 'gles'
  env.merge('BLIT_KMS_COMPOSITOR' => 'gles', 'BLIT_GLES_PLATFORM' => 'surfaceless')
end
