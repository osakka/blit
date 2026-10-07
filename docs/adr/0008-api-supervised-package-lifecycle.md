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

Update payloads are staged under `/var/lib/blit/updates`, which is shared by
the Blit service and the root helper. `BLIT_UPDATE_DIRECTORY` is available for
isolated local test fixtures; production does not depend on `/tmp` visibility.

## Consequences

An API update has one transaction owner and reports package failures with
actionable detail. Host-specific package conflicts remain observable through
the helper diagnostic rather than being silently discarded.

## Evidence

The physical host logs proved the original failure: the server's `PrivateTmp`
mount hid `/tmp/blit-update-*` from the helper, producing `sha256sum: No such
file or directory`. After the shared staging fix, 0.1.232 was accepted through
the API, restarted, and reported ready. The package also installs through real
`dpkg --unpack` and `dpkg --configure` on the local VM.
