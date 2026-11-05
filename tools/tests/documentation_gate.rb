#!/usr/bin/env ruby
# frozen_string_literal: true

# The documentation has to keep up with the code without anyone remembering
# to make it (README had drifted to describe v0.1.589 and an SDL renderer):
#
#   - every BLIT_ name the server reads or gives its children is in blit(8),
#     so a new variable cannot ship undocumented;
#   - the manual pages format without a single groff warning;
#   - no relative Markdown link in the repository points at nothing.

root = File.expand_path('../..', __dir__)
Dir.chdir(root)

# Names that are deliberately not documented: the server sets and clears
# these itself -- the first after a GPU start that crashed, the second for
# the process it execs after an update (ADR 0205); setting them does nothing
# useful.
INTERNAL = %w[BLIT_KMS_GLES_BLOCKED BLIT_UPDATE_RESTARTED].freeze

names = Dir['src/*.{c,h}'].flat_map { |f| File.read(f).scan(/"(BLIT_[A-Z0-9_]+)"/).flatten }.uniq.sort
raise 'no BLIT_ names found in src/' if names.size < 40
manual = File.read('man/blit.8')
documented = manual.scan(/^\.B[R]? (BLIT_[A-Z0-9_]+)|(?:^|[ "])(BLIT_[A-Z0-9_]+)(?=[ "]|$)/).flatten.compact.uniq
missing = names - INTERNAL - documented
raise "BLIT_ names missing from man/blit.8: #{missing.join(' ')}" unless missing.empty?
stale = documented.select { |n| n.start_with?('BLIT_') } - names
raise "man/blit.8 documents names the code no longer has: #{stale.join(' ')}" unless stale.empty?

pages = Dir['man/*.[1-9]'].sort
raise 'manual pages missing' unless pages.map { |p| File.basename(p) } == %w[blit-update-helper.8 blit.8 blit.env.5]
pages.each do |page|
  raise "#{page}: no @VERSION@ in its title line" unless File.read(page)[/^\.TH .*"Blit @VERSION@"/]
  out = IO.popen(['groff', '-man', '-ww', '-z', page], err: %i[child out], &:read)
  raise "#{page}: groff warnings:\n#{out}" unless out.empty? && $?.success?
end

# blit --version names the packaged version; --help points at the manual.
binary = File.join(root, 'build/blit/blit-server')
if File.executable?(binary)
  version = File.read('packaging/VERSION').strip
  said = IO.popen([binary, '--version'], &:read)
  raise "blit --version said #{said.inspect}, the package is #{version}" unless said == "blit #{version}\n" && $?.success?
  help = IO.popen([binary, '--help'], &:read)
  raise "blit --help does not point at blit(8): #{help.inspect}" unless $?.success? && help.include?('blit(8)')
end

# The record of the outer-edge frame (ADR 0214) is in the index and says what
# it built and what it deprecated.
index = File.read('docs/adr/README.md')
frame_adr = Dir['docs/adr/*outer-edge-frame*.md'].first or raise 'no ADR records the outer-edge frame'
raise "#{frame_adr} is not in docs/adr/README.md" unless index.include?("(#{File.basename(frame_adr)})")
adr_text = File.read(frame_adr)
raise "#{frame_adr} does not record the frame, the title ends, the deprecation and the shadow note" unless
  ['U+1002AE', 'U+1002B6', 'U+1002BA', 'U+1002BB', 'title-end', 'title-end-plain', 'Revision 2026-11-05', 'deprecated: true', 'replaced_by', 'inset 0', 'Deprecation, not retirement'].all? { |t| adr_text.include?(t) }
raise 'docs/features.md does not list the outer-edge frame' unless File.read('docs/features.md').include?('| 0214 |')

broken = []
IO.popen(%w[git ls-files -z *.md], &:read).split("\0").each do |file|
  next if file.start_with?('vendor/')
  File.read(file).scan(/\]\(([^)#\s]+)(?:#[^)\s]*)?\)/).flatten.each do |target|
    next if target.include?('://') || target.start_with?('mailto:')
    path = File.expand_path(target, File.dirname(File.join(root, file)))
    broken << "#{file}: #{target}" unless File.exist?(path)
  end
end
raise "broken links:\n#{broken.join("\n")}" unless broken.empty?

puts "DOCUMENTATION_PASS names=#{names.size} internal=#{INTERNAL.size} pages=#{pages.size} links=checked " \
     "version_flag=#{File.executable?(binary) ? 'checked' : 'no_binary'}"
