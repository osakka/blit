# The compiler and linker flags of tools/build-flags.sh, the one place
# Blit's C build finds its dependencies (pkg-config, docs/toolchain.md), for
# the Ruby gates and tools that compile C. A missing or too-old module stops
# the gate with the script's own message, which names the module and the
# minimum: a blocked capability gate, never empty flags.
require 'open3'

module BuildFlags
  SCRIPT = File.expand_path('../build-flags.sh', __dir__)

  def self.ask(kind, groups)
    out, err, status = Open3.capture3(SCRIPT, kind, *groups)
    abort(err.empty? ? "blit: build-flags.sh #{kind} #{groups.join(' ')} failed" : err) unless status.success?
    out.split
  end

  def self.cflags(*groups)
    ask('--cflags', groups)
  end

  def self.libs(*groups)
    ask('--libs', groups)
  end
end
