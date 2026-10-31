# ADR 0009: compiled runtime version is the API identity

- Status: accepted

## Context

Blit exposes its version through `/v1/version` and `/v1/server`. A locally
built binary can run on a host that has an older installed package. Reading a
system documentation file at runtime allowed that stale file to override the
version compiled into the executable, making the API identify the wrong
binary.

## Decision

The version compiled from `packaging/VERSION` is the sole runtime version
source. The API reports that compiled value. The installed documentation copy
remains package metadata and is not consulted by the executable.

## Consequences

- A binary and its API identity cannot silently diverge because of another
  installation on the host.
- Package construction still checks that the binary and package metadata carry
  the same version.
- Tests that launch a locally built binary can use the API without inheriting
  stale host package metadata.
- Changing a runtime version requires rebuilding the binary and package from
  the same `packaging/VERSION` source.

## Rejected alternative

Runtime lookup of `/usr/share/doc/blit/VERSION` was rejected because it is a
mutable deployment artifact, not part of the executable's identity.
