# ADR 0038: Keep the optional Blit adapter outside Hibr's module directory

Status: accepted

## Context

The ordinary Hibr desktop uses `need display` and must select Hibr's native
console provider. Blit's `console-blit` provider is an optional transport
adapter for explicitly controlled standalone/provider sessions. Installing it
in Hibr's module search directory made the provider discoverable during the
ordinary desktop startup. The resulting live desktop attempted to flush
large cell batches through the optional adapter, producing empty HTTP responses,
stale revisions, and a visibly corrupted display.

## Decision

The Debian package installs `console-blit.so` under
`/usr/lib/blit/hibr/console-blit.so`, outside Hibr's module search directory.
The Blit-owned adapter entrypoint loads that file explicitly and loads Hibr's
`pty`, `term`, and `hold` modules from the external Hibr installation.

The canonical held desktop path remains `/usr/bin/desktop` with
`surface_mode: terminal`; it does not load `console-blit`. Hibr source and
installed Hibr modules remain outside Blit's ownership boundary.

## Consequences

This removes provider-name collision from the normal desktop path. Provider
tests must invoke the Blit entrypoint explicitly and must verify its adapter
ABI against the exact Hibr checkout. Package tests must assert that the
optional adapter is not installed below `/usr/lib/hibr`.
