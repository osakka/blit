# frozen_string_literal: true

require_relative 'gate_server'

# One explicit presentation identity for shared pixel fixtures. These
# fixtures start isolated servers; they never attach to a running desktop.
# The display follows gate_server.rb: native KMS on the DRM mock, with the
# CPU or (BLIT_GATE_BACKEND=gles) GLES compositor. ADR 0184.
module TestProfile
  ROOT = File.expand_path('../..', __dir__)

  def self.binary = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
  def self.font = ENV.fetch('BLIT_GATE_FONT', File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))

  def self.environment
    gate_display_env.merge(
      {'BLIT_BIND_ADDRESS'=>'127.0.0.1',
       'BLIT_AUTH_TOKEN'=>'', 'BLIT_LOGIN_AUTOSTART'=>'0', 'BLIT_EXPECT_DESKTOP'=>'0',
       'BLIT_SETTINGS'=>'ignore', 'BLIT_FONT_SCALE'=>'1', 'BLIT_PSF_FONT'=>font})
  end

  def self.verify!(diagnostics)
    return if diagnostics.dig('health', 'ready') &&
              diagnostics.dig('health', 'backend') == 'drm-kms'
    raise "unexpected presentation path: #{diagnostics['health'].inspect}"
  end

  def self.display_id!(inventory)
    displays = inventory.fetch('displays')
    id = ENV.fetch('BLIT_TEST_DISPLAY_ID') { displays.fetch(0).fetch('id') }
    raise "requested display #{id} is unavailable" unless displays.any? { |d| d.fetch('id') == id }
    id
  end
end
