# ADR 0086: Package the adapter from the pinned Hibr checkout

- Status: accepted

## Context

`console-blit.so` is compiled against Hibr's module ABI. The TCC adapter gate
already rejects a checkout whose `HIBR_VER` differs from
`packaging/HIBR_VERSION`, but the Debian builder previously copied an
arbitrary adapter from the ignored `build/` directory. That allowed a package
to contain a stale adapter even when the source and release metadata named a
different Hibr version.

The package also needs to report whether the Hibr executable it will launch
is compatible with the adapter. A package hash or exact release string alone
cannot answer that host-runtime question: Hibr's documented module boundary
is its numeric `HIBR_ABI`.

## Decision

`packaging/build-deb.sh` requires `BLIT_HIBR_ROOT` and compiles
`console-blit.so` into the package staging tree with the canonical TCC build
script. The build script validates the checkout's `include/hibr.h` against
both `packaging/HIBR_VERSION` and `packaging/HIBR_ABI`; no pre-existing
adapter is accepted as package input.

The server binary is compiled with the same Hibr pin and exposes
`hibr_compatibility` in `/v1/diagnostics/runtime`. At startup it invokes the
configured Hibr command with `--version` without a shell, records the result,
and reports `compatible: false` on an ABI mismatch or `compatible: null` when
the probe is unavailable. A release mismatch with an equal ABI is
informational drift and is accepted. Both identities are exposed for
provenance, and an ABI mismatch is also a structured runtime log event.

## Consequences

- A package build cannot silently reuse an adapter from another Hibr checkout.
- The release gate proves both the negative case (no checkout means no package)
  and the positive exact-pinned build.
- Operators can distinguish the packaged Hibr release and ABI from the host's
  runtime release and ABI through the existing diagnostics API.
- Patch or minor release movement within the same documented module ABI no
  longer causes a false compatibility failure; an ABI change still fails
  closed.
- The startup probe is bounded to one non-shell `hibr --version` invocation;
  an unavailable or non-conforming command is reported, not treated as a
  compatible runtime.

## Verification

- `tools/build-tcc.sh` compiles the server with `packaging/HIBR_VERSION` and
  `packaging/HIBR_ABI`.
- `tools/tests/package_artifact_gate.sh` rejects a package build without
  `BLIT_HIBR_ROOT`, rebuilds with the exact pinned checkout, and checks the
  resulting package hashes and reproducibility.
- `tools/run-gates.sh renderer` validates the runtime diagnostics schema and
  package artifact.

## Rejected alternative

Copying `build/blit/console-blit.so` was rejected because its provenance is
not recoverable from the package build inputs. Recording a second manually
maintained adapter version was rejected because it creates another source of
truth.
