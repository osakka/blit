# Monocypher 4.0.3

Unmodified files from the Monocypher 4.0.3 release archive
(`monocypher-4.0.3.tar.gz` from monocypher.org, released 2026-06-15): the
core library, the optional Ed25519 module (upstream `src/optional/`, placed
beside the core here so its `#include "monocypher.h"` needs no include
path) and the licence. Exact archive and file hashes: `source.json`. The
archive's SHA-512 is the one monocypher.org publishes beside it, and its
`src/` files are the GitHub tag `4.0.3`'s with the version line filled in.

Licence: dual, BSD-2-Clause or CC0-1.0, at the user's choice
(`LICENCE.md`).

What Blit uses: `crypto_ed25519_check` (RFC 8032 Ed25519, SHA-512) in the
root update helper, which refuses an update whose release signature does
not verify (security review SR-2, ADR 0232), and `crypto_ed25519_key_pair`
and `crypto_ed25519_sign` in `tools/release/blit-sign`, which signs a
release. Nothing else links it; it is compiled by TCC with the helper. Not
`crypto_eddsa_*`: Monocypher's own EdDSA hashes with BLAKE2b and is not
RFC 8032 Ed25519.

4.0.3 fixes a timing leak in signing (EdDSA/Ed25519), which is why it, and
not 4.0.2, is pinned. `tools/tests/release_signature_gate.sh` checks these
files against `source.json`.
