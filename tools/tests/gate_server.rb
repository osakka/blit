# frozen_string_literal: true

# ADR 0181 step 1. The display a gate's server presents to, chosen once for
# the whole suite by BLIT_GATE_BACKEND:
#
#   native (default)  the production direct-KMS compositor, headless: the DRM
#                     boundary is tools/tests/fixtures/drm_hotplug_mock.c
#                     (LD_PRELOAD), one 1024x768 output named display-1.
#   sdl               SDL's dummy video driver, kept
#                     until the GLES cutover so the SDL path stays checked.
#
# A gate merges gate_display_env into its server environment and reads
# screenshots from /v1/diagnostics/screenshots/display-1 either way.

require 'digest'
require 'fileutils'
require 'tmpdir'

GATE_ROOT = File.expand_path('../..', __dir__)
GATE_BACKEND = ENV.fetch('BLIT_GATE_BACKEND', 'native')
abort "BLIT_GATE_BACKEND must be native or sdl, not #{GATE_BACKEND}" unless
  %w[native sdl].include?(GATE_BACKEND)

def gate_native?
  GATE_BACKEND == 'native'
end

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

# A gate that tests the SDL path itself passes backend: 'sdl'; it is retired
# with SDL at the GLES cutover (ADR 0181).
def gate_display_env(width: 1024, height: 768, backend: GATE_BACKEND)
  libraries = File.join(GATE_ROOT, 'build/sdl3-kms')
  unless backend == 'native'
    # Exactly what each gate set before ADR 0181 step 1: the driver alone.
    # A gate that wants a renderer still names it in its own environment.
    return { 'SDL_VIDEODRIVER' => 'dummy', 'LD_LIBRARY_PATH' => libraries }
  end
  directory = Dir.mktmpdir('blit-gate-display')
  at_exit { FileUtils.rm_rf(directory) }
  device = File.join(directory, 'drm')
  state = File.join(directory, 'state')
  File.write(device, '')
  File.write(state, "1 0 0 0\n1 #{width} #{height}\n")
  { 'BLIT_DISPLAY_BACKEND' => 'kms', 'BLIT_DRM_DEVICE' => device,
    'BLIT_DRM_MOCK_STATE' => state, 'LD_PRELOAD' => gate_drm_mock,
    'LD_LIBRARY_PATH' => libraries }
end
