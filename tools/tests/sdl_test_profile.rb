# frozen_string_literal: true

# One explicit presentation identity for shared SDL pixel fixtures. These
# fixtures start isolated servers; they never attach to a running desktop.
module SdlTestProfile
  ROOT = File.expand_path('../..', __dir__)

  def self.driver = ENV.fetch('BLIT_TEST_VIDEO_DRIVER', 'dummy')
  def self.renderer = ENV.fetch('BLIT_TEST_RENDERER', 'software')
  def self.binary = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
  def self.font = ENV.fetch('BLIT_GATE_FONT', File.join(ROOT, 'assets/fonts/Blit-Hibr-Terminus16.psf'))

  def self.environment
    {'SDL_VIDEODRIVER'=>driver, 'BLIT_RENDERER'=>renderer, 'SDL_RENDERER'=>renderer,
     'BLIT_DISPLAY_BACKEND'=>'sdl', 'BLIT_BIND_ADDRESS'=>'127.0.0.1',
     'BLIT_AUTH_TOKEN'=>'', 'BLIT_LOGIN_AUTOSTART'=>'0', 'BLIT_EXPECT_DESKTOP'=>'0',
     'BLIT_SETTINGS'=>'ignore', 'BLIT_FONT_SCALE'=>'1', 'BLIT_PSF_FONT'=>font,
     'LD_LIBRARY_PATH'=>ENV.fetch('BLIT_GATE_LIBS', File.join(ROOT, 'build/sdl3-kms'))}
  end

  def self.verify!(diagnostics)
    actual = diagnostics.fetch('render')
    return if diagnostics.dig('health', 'ready') &&
              diagnostics.dig('health', 'backend') == 'sdl3' &&
              actual.fetch('direct_kms') == false &&
              actual.fetch('video_driver') == driver && actual.fetch('renderer') == renderer
    raise "unexpected presentation path: #{actual.slice('direct_kms', 'video_driver', 'renderer')}"
  end

  def self.display_id!(inventory)
    displays = inventory.fetch('displays')
    id = ENV.fetch('BLIT_TEST_DISPLAY_ID') { displays.fetch(0).fetch('id') }
    raise "requested display #{id} is unavailable" unless displays.any? { |d| d.fetch('id') == id }
    id
  end
end
