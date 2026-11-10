# frozen_string_literal: true

# The environment of every Blit process a gate starts (blit-server,
# blit-compositor, a Blit helper). One rule, the same in all three gate
# languages: tools/tests/gate_env.rb (Ruby), tools/tests/gate_env.py (Python)
# and tools/tests/gate_env.sh (sh).
#
#   1. Start from the gate's own environment minus everything a Blit session,
#      Hibr or systemd puts there: every BLIT_*, HIBR_* and LISTEN_* variable,
#      WAYLAND_DISPLAY, WAYLAND_SOCKET and NOTIFY_SOCKET. PATH, HOME, LANG,
#      TMPDIR, XDG_RUNTIME_DIR, ASAN_OPTIONS and the rest stay.
#   2. BLIT_INPUT_ROOT is an empty directory of this process's own, so the
#      server opens no real /dev/input device.
#   3. The NAME=value words of BLIT_GATE_SERVER_ENV, the one sanctioned way for
#      an operator (or a wrapper script) to give gate servers a variable.
#   4. What the gate sets itself, which wins over all of the above -- a gate
#      with fabricated devices names its own BLIT_INPUT_ROOT.
#
# A gate run from inside a Blit session inherited that session's
# BLIT_AUTH_TOKEN, BLIT_DISPLAY_BACKEND, BLIT_EXPECT_DESKTOP, ... and its server
# opened the owner's keyboard and mouse. Nothing a gate's server sees may come
# from the host it happens to run on.
#
#   gate_spawn(env, BINARY, PORT.to_s, out: log)      # Process.spawn
#   spawn(*gate_sudo_argv(env, BINARY, PORT.to_s))    # a server run as root

require 'fileutils'
require 'tmpdir'

module GateEnv
  FOREIGN = /\A(?:BLIT_|HIBR_|LISTEN_)|\A(?:WAYLAND_DISPLAY|WAYLAND_SOCKET|NOTIFY_SOCKET)\z/

  def self.foreign?(name) = FOREIGN.match?(name)

  # Empty, private to this process, removed when it exits.
  def self.no_input
    @no_input ||= begin
      directory = Dir.mktmpdir('blit-gate-no-input')
      owner = Process.pid
      at_exit { FileUtils.rm_rf(directory) if Process.pid == owner }
      directory
    end
  end

  def self.operator
    ENV.fetch('BLIT_GATE_SERVER_ENV', '').split.to_h do |word|
      name, value = word.split('=', 2)
      [name, value.to_s]
    end
  end

  def self.explicit(extra)
    { 'BLIT_INPUT_ROOT' => no_input }.merge(operator)
                                     .merge(extra.to_h.transform_keys(&:to_s))
                                     .compact.transform_values(&:to_s)
  end

  # The complete environment: pass it with unsetenv_others: true.
  def self.server(extra = {})
    ENV.to_h.reject { |name, _| foreign?(name) }.merge(explicit(extra))
  end
end

def gate_server_env(extra = {}) = GateEnv.server(extra)

def gate_spawn(extra, *argv, **options)
  Process.spawn(GateEnv.server(extra), *argv, unsetenv_others: true, **options)
end

# sudo's env_reset already hands root a fresh environment; the -u words keep
# anything a sudoers env_keep would carry over out of it as well.
def gate_sudo_argv(extra, *argv)
  unset = ENV.keys.select { |name| GateEnv.foreign?(name) }.flat_map { |name| ['-u', name] }
  ['sudo', '-n', 'env', *unset, *GateEnv.explicit(extra).map { |k, v| "#{k}=#{v}" }, *argv]
end
