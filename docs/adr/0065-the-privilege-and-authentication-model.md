# ADR 0065: The privilege and authentication model

- Status: accepted
- Date: 2026-10-10
- Scope: who Blit runs as, what the session API may execute and as whom, and
  when the gateway is willing to accept a network request

## Context

Two individually defensible decisions combined into remote unauthenticated
code execution on the project's own deployed host (issue #25).

**The session API executes a client-supplied command, by design.**
`POST /v1/terminal-sessions` takes `command`, `arguments` and `environment`
and `execve`s them directly. That is not a flaw to be removed: an API-driven
display server whose purpose is presenting PTY-backed programs has to be able
to start one, ADR 0010 and the whole Hibr integration depend on it, and it is
the first endpoint in the documented minimal client workflow.

**The service runs as root.** `packaging/blit.service` sets `User=root` and
`NoNewPrivileges=false`, because a session request may name a `user` to run
as and dropping to another user needs privilege.

**Authentication defaulted to off, and the listener defaulted to the
network.** `BLIT_BIND_ADDRESS` defaulted to `0.0.0.0`; a missing
`BLIT_AUTH_TOKEN` produced a line on stderr and the server carried on. So a
default install was a network-reachable API that would `execve` anything as
root. On the live host this was confirmed rather than theorised: a `/bin/sh`
session created over plain HTTP rendered a `#` prompt, and its output was
readable back through `GET /v1/surfaces/{id}/cells` — a complete
read-write-execute loop with no credential.

The package's own `postinst` wrote `BLIT_BIND_ADDRESS=0.0.0.0` into
`/etc/blit/blit.env` and generated an update token, but no API token. So the
open posture was not an operator mistake; it was the shipped default.

## Options considered

1. **Restrict what the session API may execute.** `BLIT_COMMAND_ALLOWLIST`
   already exists and the deployment should use it, but it is a policy an
   operator configures, not a default. An allowlist containing a shell — which
   a desktop deployment wants — is not a boundary at all. Necessary, not
   sufficient.

2. **Require a token always.** Breaks every local fixture and every gate in
   this repository, all of which talk to `127.0.0.1` with no credential, for
   no gain: a loopback listener on a developer's machine is not the exposure.

3. **Refuse to start without a token.** Fails closed, and would have prevented
   this. But it turns a missing environment variable into a host that will not
   come up, and on an API-only deployment a host that will not come up cannot
   be fixed over the API. Too blunt as the only rule.

4. **Make the safe thing the default and refuse only the stated mistake.**
   Chosen.

## Decision

**The listener's posture depends on whether a network bind was asked for.**

- No token and no `BLIT_BIND_ADDRESS`: bind `127.0.0.1`, and say so on stderr.
  A developer gets a working server; nobody gets a network-reachable one by
  omission.
- No token and an explicitly requested non-loopback address: **refuse to
  start**, naming the three ways out. That combination cannot be an accident
  worth obeying.
- `BLIT_ALLOW_UNAUTHENTICATED_NETWORK=1` turns the refusal back into a
  warning, for a fixture that genuinely has no network — a QEMU guest, a VKMS
  rig. It has to be asked for by name, and while it is in force
  `/v1/diagnostics` reports `unauthenticated_network_listener` in its
  `warnings`, so the posture is visible where health is read and not only in a
  startup line nobody re-reads.
- With a token, nothing changes: the bind is what was asked for.

**The posture is reported, not merely logged.** `/v1/server` carries
`listener: {bind, port, authentication, command_allowlist}`. A control panel
can show it, an operator can check it, and a gate can assert it.

**A package install is an authenticated deployment.** The `postinst`
generates a random `BLIT_AUTH_TOKEN` into `/etc/blit/blit.env`, the way it
already generates the update token, and appends one to an existing
configuration that has none. On an upgrade that turns a previously open
deployment into one whose clients get `401` until they are told the new token
— a deliberate break, taken because the alternative is a host that refuses to
start or one that is knowingly open.

**The privileged default stays, and the unprivileged profile ships beside
it.** `packaging/blit-unprivileged.conf` is a systemd drop-in that runs the
gateway as the `blit` system user the package already creates, with
`video render input` and `NoNewPrivileges=true`. It is not the default, for
two reasons:

- **It changes what a session can be.** A session request naming a `user`
  requires `geteuid() == 0`; a non-root gateway refuses that request rather
  than silently running the command as itself. Under the unprivileged profile
  every session runs as `blit`. For a Hibr desktop that is what you want; for
  a deployment relying on per-user sessions it is not.
- **Blit cannot know from inside a package whether the `blit` user can reach
  this host's DRM device.** That depends on udev rules and on whether anything
  else holds DRM master. A gateway that comes up unable to present has no way
  back in on an API-only deployment, so switching profiles is an operator
  action taken with access to the machine.

## Consequences

- A default install is no longer a remote root shell: it is network-reachable
  with a generated token, or loopback-only if the operator removed the bind
  line and set no token.
- The update-token boundary stops being a strong control beside an open one.
  `/v1/control/update` verifies SHA-256 and the package identity; that is now
  defence in depth behind authentication rather than the only lock on the
  building.
- Fixtures that bind the network without a token must now say
  `BLIT_ALLOW_UNAUTHENTICATED_NETWORK=1`. The three QEMU probes in this
  repository already bind `127.0.0.1` and are unaffected.
- An upgrade of an unauthenticated deployment breaks its clients until they
  are given the generated token. Recorded here rather than left to be
  discovered.
- `User=root` remains the default, so **an authenticated caller can still run
  a command as root**. That is the API's purpose and not a defect, but it
  means the token is a root credential and must be treated as one. The
  allowlist and the unprivileged profile are how a deployment narrows it.

## Evidence

- `tools/tests/network_auth_gate.sh` — four postures, three of them new. No
  token and no bind: `listener.bind` is `127.0.0.1`. No token and an explicit
  `0.0.0.0`: the process exits non-zero and stderr says why. The override:
  `/v1/diagnostics` reports `unauthenticated_network_listener`. A token with an
  explicit `0.0.0.0`: the bind is honoured and an unauthenticated request is
  `401` — which the gate already asserted, along with the command allowlist and
  `BLIT_API_TOKEN` injection. Teeth: restored to warn-and-continue, the
  default-posture case reads `bind=0.0.0.0 authentication=none`.
- The `postinst` token path exercised against a staged `/etc/blit`: a fresh
  install gains one 64-character token line, a second run adds no second line,
  and a server started with that environment binds `0.0.0.0` with
  `authentication: required` and answers `401` without a credential.
- Live, on 192.168.10.54 before the change: an unauthenticated
  `GET /v1/server` and `POST /v1/terminal-sessions` both return `401` and
  survive a clean `POST /v1/control/restart`, so that host's token is
  persisted and the fail-safe build cannot strand it. That check is the reason
  the change was safe to deploy to an API-only host.
- **Not verified, and the reason the unprivileged profile is not the default:**
  whether the `blit` user can drive DRM/KMS on a real host. The gateway runs
  as an unprivileged user under `SDL_VIDEODRIVER=dummy` in every gate in this
  repository, which shows the API, the device scan and the renderer do not
  need root — it does not show that a KMS deployment survives the switch.
