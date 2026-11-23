# Blit release keys (public halves)

Each `*.pub` file here is the public half of a Blit release key, one line:
`blit-release-ed25519-public <64 lowercase hex digits>`. Every package
installs all of them in `/usr/lib/blit/release-keys/`, and the update helper
installs an update only when its signature verifies under one of them
(security review SR-2, ADR 0232). Secret halves are never here: they live
outside the checkout, by default `~/.config/blit-release/ed25519.secret`.

Today it holds one key, `blit-release-1.pub` (made with `tools/release/keygen
blit-release-1` on 2026-11-21); every release from 0.3.133 is signed with
it, and the update helper refuses any `blit` older than 0.3.133.
`packaging/build-deb.sh` refuses to build a package while this directory
holds no `*.pub`: a package without a key would leave its hosts unable to
take any update but root's own dpkg.

Rotation: a release adds the next key here (and is still signed with the
current one); the release after that may be signed with the new key.
Removing a file revokes that key for every host that installs the package
without it. See docs/development-workflow.md, "Release signing".
