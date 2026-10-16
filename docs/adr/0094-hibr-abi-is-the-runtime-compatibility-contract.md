# ADR 0094: Use Hibr module ABI for runtime compatibility

## Context

Hibr `--version` reports both a release version and a numeric module ABI. Hibr's
module documentation identifies that ABI as the boundary checked when a module
is loaded. Blit previously treated exact release equality as runtime
compatibility, so a host moving from Hibr 0.99.212 to 0.99.213 was rejected
even though both releases report ABI 16.

## Decision

Blit packages one canonical `packaging/HIBR_ABI` value and compiles the server
and adapter against it. At startup, Blit probes the configured Hibr command
without a shell and parses both identities. `/v1/diagnostics/runtime` reports
`pinned_version`, `runtime_version`, `pinned_abi`, and `runtime_abi`.

Runtime compatibility is true only when the probe succeeds and the two ABI
values are equal. Release-version drift with equal ABI is accepted and logged
as informational provenance. ABI mismatch or an unavailable probe remains a
fail-closed condition.

## Consequences

- Compatible Hibr release movement no longer creates a false login block.
- A future Hibr ABI change cannot silently load the Blit adapter.
- Release records retain exact version provenance for reproducibility and
  diagnosis.
- The public runtime schema and network gate must evolve with the diagnostics
  fields.

## Rejected alternatives

- Exact release equality was rejected because it is stricter than Hibr's own
  module contract and rejects compatible patch releases.
- A manually maintained list of compatible releases was rejected because it
  duplicates Hibr's ABI source of truth.

## Verification

- The TCC build rejects an adapter checkout whose `HIBR_ABI` differs from the
  packaged value.
- The API schema and network gate require both ABI fields.
- The live host is accepted only after its reported ABI matches the packaged
  ABI and the login handoff is retested through the diagnostics API.
