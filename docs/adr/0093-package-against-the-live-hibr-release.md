# ADR 0093: Package against the installed Hibr release

- Status: superseded by ADR 0094

## Context

Blit 0.1.498 correctly stopped mirroring Hibr's desktop tree and began
sourcing Hibr's installed session directly. After deployment, the live
diagnostics reported that the package's adapter pin was Hibr 0.99.191 while
the host's installed Hibr executable was 0.99.204. Treating that package as
compatible would make the installation dependent on an unverified ABI
combination.

## Decision

For each physical-host candidate, `packaging/HIBR_VERSION` names the exact
released Hibr version installed on that host. The adapter and server are
built and gated against a checkout of that exact Hibr release. A package whose
runtime probe reports a different Hibr version is incompatible and must not be
accepted as a final deployment.

Blit continues to source Hibr's installed desktop session directly. This
decision changes only Blit's package provenance and compatibility boundary; it
does not modify Hibr source, installed Hibr files, or Hibr session behavior.

## Consequences

- The package's Hibr ABI provenance is explicit and reproducible.
- A Hibr upgrade requires a deliberate Blit candidate rebuild and acceptance.
- The live API exposes a clear incompatibility instead of silently running an
  unverified adapter.
- QEMU may use an explicit fixture session/version, but that fixture cannot be
  promoted as physical-host acceptance without matching the target host.

## Rejected alternatives

- Keeping the 0.99.191 pin was rejected because the live host runs 0.99.204.
- Silencing the compatibility diagnostic was rejected because it hides an ABI
  boundary failure.
- Copying or symlinking Hibr files into the Blit package was rejected because
  it creates a second source of truth and diverges from normal Hibr installs.
