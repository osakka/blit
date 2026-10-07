# ADR-0008: API-supervised package lifecycle

## Context

Blit upgrades are initiated over the authenticated API by a root-owned Unix
socket helper. Restarting the helper service from the package's own post-
install script can terminate the helper before it reports the result.

## Decision

The Debian post-install script may create configuration and enable the Blit
units, but it must not start or restart either service. The API owns the
post-install restart boundary. The helper captures bounded, sanitized `dpkg`
output, returns it to the API, and exits successfully after an install so its
`Restart=always` unit loads the newly installed helper binary.

The package keeps SDL3 private under `/usr/lib/blit/lib` and sets
`LD_LIBRARY_PATH` on the Blit service. It does not claim global SDL3 library
paths that may belong to the host distribution.

## Consequences

An API update has one transaction owner and reports package failures with
actionable detail. Host-specific package conflicts remain observable through
the helper diagnostic rather than being silently discarded.

## Evidence

The package installs through real `dpkg --unpack` and `dpkg --configure` on the
local VM. The physical host remains on 0.1.223 because its pre-ADR-0008 helper
still returns only `ERROR`; the next successful package transaction will
replace that helper and expose the missing host-specific diagnostic.
