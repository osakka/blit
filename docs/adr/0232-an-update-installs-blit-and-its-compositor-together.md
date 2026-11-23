# ADR 0232: An update installs blit and its compositor together

- Status: accepted
- Note: Shipped in 0.3.97; the evidence below was recorded on 2026-11-13.
- Note: Amended in 0.3.132 (the helper installs its own checked copies,
  blit Breaks a newer blit-compositor, retired packages are replaced),
  0.3.133 (only a signed release is installed) and 0.3.135
  (`update-live.sh --rollback-to` reinstalls the previous signed pair):
  the sections below. `blit-font-sources`, named under Consequences, is
  no longer built since 0.3.131 (ADR 0243).
- Date: 2026-11-12
- Scope: `PUT /v1/control/update`, the root update helper
  (`blit-update-helper`), `tools/deploy/update-live.sh`, the release
  manifest (`releases/<version>.yaml`)

## Context

`blit-compositor` depends on `blit (= <its own version>)`. The supervised
update (`PUT /v1/control/update`, ADR 0065) installs one package, and the
helper refuses any package that is not `blit`. Every Blit release was
deployed that way, so between a deployment and the owner's next apt
upgrade, a host with both packages has `blit` at the new version and
`blit-compositor` at the old one.

Reproduced in Debian 13 with 0.3.95 → 0.3.96:

- `dpkg --install blit_0.3.96-1_amd64.deb` succeeds and `dpkg --audit` is
  clean.
- `apt-get check` reports `blit-compositor : Depends: blit (= 0.3.95-1)
  but 0.3.96-1 is installed`.
- Plain `apt-get upgrade` and `apt-get full-upgrade` refuse ("Unmet
  dependencies. Try 'apt --fix-broken install'"). They still refuse when
  the repository already has blit-compositor 0.3.96.

What happened on the owner's mini PC (apt history, 2026-11-12):

- At 17:05 their updater resolved the conflict by downgrading `blit`
  0.3.90 → 0.3.89, back to the version the installed compositor allowed.
  That undid the deployment.
- Later upgrades worked only because their updater repairs broken
  dependencies.
- A desktop restarted in the window between deploy and upgrade runs the
  new server against the old compositor binary.

Found while doing this: nothing ever removed a staged package. The mini PC's
`/var/lib/blit/updates` held 333 of them, 3.4 GB, one per deployment
since October.

## Options considered

1. **The deploy script also uploads the compositor, as a second update.**
   This depends on whoever deploys remembering to do it. The helper would
   still accept `blit` alone, so the broken state stays reachable.
2. **The helper installs from hibr-apt with apt.** That means a network
   fetch as root, and it depends on the mirror having synced. It also
   inverts the release order: Blit deploys, then publishes.
3. **One upload carries the packages, and the helper owns the rule.** The
   helper installs `blit` together with every installed package that pins
   blit's version, in one `dpkg --install`. It refuses an upload that would
   leave such a package behind.

## Decision

Option 3.

- **The upload.** `PUT /v1/control/update` takes the packages back to
  back in one body. The header `X-Blit-Packages: <length>:<sha256>
  [<length>:<sha256>]` names each one's length and SHA-256, in body order.
  It replaces `X-Blit-SHA256`. `Content-Length` must equal the sum of the
  lengths. At most two packages.
- **The server** stages each package in its own file in the update
  directory. Before asking the helper, it checks each file's SHA-256 and
  that its `Package` field is `blit` or `blit-compositor`.
- **The helper is the one place the rule lives.** It checks:
  - each file's SHA-256;
  - that the names are distinct, from `blit` and `blit-compositor`, with
    `blit` present;
  - that every `Version` is equal.

  If `blit-compositor` is installed (`dpkg-query` status `installed`), the
  upload must carry it, or the helper refuses with "blit-compositor is
  installed; the update must carry it". If it isn't installed, a carried
  compositor is left out: Blit's update never adds a package the host
  didn't choose. The helper then runs one `dpkg --install` with what it
  installs.
- **Staged files.** The helper accepts only `blit-update-*` files directly
  in its staging directory (`/var/lib/blit/updates`). It removes them once
  it has answered, installed or refused. It runs as root and deletes what
  it is handed, so it refuses any other path. The package's postinst
  clears leftover `blit-update-*` files once, for the ones earlier releases
  left behind.
- **`tools/deploy/update-live.sh HOST BLIT_DEB`** always sends `blit` and
  the `blit-compositor` of the same version from the same directory. It
  refuses if that file is missing or doesn't match the manifest.
- **The release manifest** records `compositor_package:` and
  `compositor_package_sha256:`, generated from the sibling package by
  `generate-manifest.sh`, so `--check` covers it.

## Consequences

- After a deployment, a host has both packages at one version. apt has
  nothing to repair, and its updater can't downgrade `blit`.
- **What this doesn't change.** The running compositor process is still
  the old binary until the desktop restarts (Restart Desktop or log-out).
  The packages on disk are consistent at once; the process isn't, and
  nothing in this decision claims it is.
- **Bootstrap.** A 0.3.96 server only accepts `X-Blit-SHA256` and `blit`
  alone. So 0.3.97 is deployed with v0.3.96's `update-live.sh`, and then
  deployed again with 0.3.97's, which installs the pair. The second deploy
  is the live evidence.
- An update's staged files no longer accumulate. The first 0.3.97
  install frees the 3.4 GB on the mini PC.
- **No compatibility path.** `X-Blit-SHA256` is gone. A client that sends
  it gets 413 `update_invalid`, as any other malformed update does.
- `blit-font-sources` has no `Depends` on `blit` and isn't part of the
  update.

## Evidence

- `update_pair_gate.sh` (new) runs in the build environment with the real
  helper and real dpkg, on 0.3.96 → current packages in a scratch dpkg
  root. It shows:
  - the pair installs and `apt-get check` is clean;
  - `blit` alone is refused while the compositor is installed;
  - mismatched versions are refused;
  - a host without the compositor gets `blit` alone;
  - a path outside the staging directory is refused, and that file is
    kept;
  - after every answer, the staged files are gone.

  Built against the 0.3.96 helper, the gate fails at its first case: that
  helper answers OK to `blit` alone.
- The four `update_*` gates and `restart_after_update_gate` use
  `X-Blit-Packages`, and the server's split is checked across a part
  boundary.
- The mini PC's 0.3.97 second deploy shows both packages at 0.3.97
  without an apt upgrade.

Recorded for 0.3.97 (docs/verification-status.md):
- `update_pair_gate` 6/6, in the release build;
- `update_restart_gate` passing in the packaging suite;
- on the mini PC, the second deploy upgraded both packages in one dpkg
  run, with `apt-get check` and `dpkg --audit` clean and no staged files
  left.

## Amended in 0.3.132 (security review SR-10, SR-12, SR-13, SR-21; Gitea #209)

- **What the helper installs is its own copy.** It opens each staged file
  once (`O_NOFOLLOW`, regular, at most 64 MiB), copies it into
  `/var/lib/blit-update-helper` (0700, refused unless the helper owns it;
  not under `/var/lib/blit`, which is the server's under the unprivileged
  profile), and hashes, reads and installs the copy. The server stages
  with `mkostemp(O_CLOEXEC)`. (SR-10, #192.)
- **The order and the stop.** `dpkg --install --abort-after=1`, `blit`
  first unless the installed compositor is newer than the new `blit` (a
  rollback), then the compositor first: `blit`
  Breaks every newer `blit-compositor` (SR-13, #195), so on a rollback it
  can only be unpacked once the compositor is at its version. On an
  update, anything that refuses `blit` (an old Hibr) leaves the host as it
  was; before, dpkg went on and left the compositor unpacked at the new
  version. Only the newer half of the Breaks: helpers before 0.3.132
  unpack `blit` first and would be refused by an older half.
- **Retired packages.** `blit` Conflicts with and Replaces `steamer,
  blit-gamescope` instead of Breaking them (#209): dpkg --install refused
  the Breaks ("deconfiguration is not permitted"), so a host that still
  had one could take no update through the API; it now removes them in
  the same run. Such a host is still on 0.3.74 or earlier (no later `blit`
  could be installed beside them), so its server takes `blit` alone with
  `X-Blit-SHA256` and is reached with an `update-live.sh` from before
  0.3.97, as in the bootstrap above; its helper's plain `dpkg --install`
  needs no change for it. A package depending on another retired one is listed first. apt
  treats both relations alike (checked with apt 3.0 against scratch
  packages): `apt upgrade` keeps `blit` back while they are installed,
  `apt full-upgrade` removes them.
- **Bounded, and off the main loop.** The helper stops dpkg after 600 s
  (each other command after 30 s) and says so; the server asks the helper
  and reads its answer from the main loop, so an install no longer
  freezes the display or the API, and the console watchdog's 120 s install
  lease is gone. While installing, another `PUT`, `reset` or `cancel` is
  409 `update_installing`; the server stops waiting after 900 s. (SR-12,
  #194.)
- **The error, not the preamble.** The helper keeps the end of a command's
  output and the server's detail its end too (SR-21, #203).
- Evidence: update_pair_gate gains `retired`, `swapped` and `bound` (nine
  cases), rollback_recovery_gate's `local-half` requires dpkg's refusal
  when N-1 carries the Breaks and `failed-configure` the postinst's error,
  and update_install_live_gate is new.

## Amended in 0.3.133 (security review SR-2, Gitea #184): only a signed release

- **The helper installs only a release the owner signed.** It rebuilds a
  statement from what the update carries -- `blit-release-signature 1`,
  `blit <Version> <sha256>` and, when carried, `blit-compositor <Version>
  <sha256>`, from its own copies (src/blit_release_signature.h) -- and
  installs only when the signature verifies as Ed25519 (RFC 8032) under one
  of `/usr/lib/blit/release-keys/*.pub`. The check comes after the hashes
  and the identity checks and before the compositor is left out on a host
  without it, so one signature over the pair serves every host. Unsigned,
  another key, other packages or another version: refused, nothing
  installed, the reason in the answer.
- **Transport.** `PUT /v1/control/update` takes `X-Blit-Signature: <128
  lowercase hex>`; the server refuses any other value (400
  `update_invalid`) and passes it to the helper as `SIGNATURE <hex>` at the
  end of the request line. The server does not require it: the helper is
  still the one place the rule lives, and an upload without it is refused
  there. `tools/deploy/update-live.sh` sends the
  `blit-release_<version>_amd64.sig` beside the packages and refuses to
  start without it.
- **Crypto with no new dependency.** Monocypher 4.0.3 (BSD-2-Clause or
  CC0-1.0), unmodified and pinned in `vendor/monocypher` (archive and file
  hashes in `source.json`), compiled by TCC into the helper and into
  `tools/release/blit-sign`, the release tool that makes keys and signs.
  No gpg or openssl on either side.
- **Keys.** Public halves in `packaging/release-keys/*.pub`, installed by
  `blit`; `build-deb.sh` builds no package without one. The secret half
  never enters the repository, a package or the build environment:
  `tools/release/keygen` writes it to `~/.config/blit-release/ed25519.secret`
  (0600), and `build-release.sh` checks it first and signs last, on the
  host, after every gate (`tools/release/sign-release.sh`). Rotation: a
  release adds the next key while signed with the current one; removing a
  key file revokes it.
- **A floor for rollbacks.** Rollbacks to older signed releases are kept
  (the rollback runbook needs them), but the helper refuses a `blit` older
  than 0.3.133, the first release that verifies, even signed: a signature
  can never reinstall a helper that installs anything. Below it is root's
  own dpkg. A later release may raise the floor.
- **Bootstrap.** As in 0.3.97: through the API, 0.3.133 is installed by the
  0.3.132 helper, which verifies nothing; it exits after answering and
  systemd starts the new one, so every update after it must be signed.
  Installed by apt or root's dpkg instead, the old helper process would
  keep running (installing unsigned uploads, refusing a new server's signed
  line), so the postinst now restarts a running helper unless it runs under
  the helper itself.
- Evidence: `release_signature_gate` (new, light: RFC 8032 vectors and the
  refusals, `blit-sign`, and the helper's refusals with its install
  replaced by a failing hook); `update_pair_gate` signs every request with
  a throwaway key and gains `unsigned`, `other-key`, `old-pair-signature`,
  `other-hashes`, `blit-alone-signature`, `rotation` and `below-oldest`
  (16 cases); `rollback_recovery_gate` signs what it hands the helper and
  `update-live.sh`; `package_artifact_gate` checks the packaged keys and
  that no secret is packaged. The heavy gates' new cases first run in the
  0.3.133 release build.
- Lab evidence (2026-11-21), real dpkg in a scratch root only: the 0.3.132
  helper installs the 0.3.133 pair from a 0.3.132 server's request; the
  0.3.133 helper refuses unsigned, other-key, other-version, other-hash and
  truncated signatures with nothing changed, installs a signed update,
  rolls back to a signed 0.3.133, refuses a signed 0.3.132 (the floor),
  rotates and revokes keys, and installs `blit` alone from the pair's
  signature on a host without the compositor; through this checkout's
  `blit-server` the same refusals are 500 `update_install_failed` with the
  reason, a malformed header 400 with nothing staged, and the signed pair
  202 and installed. The postinst's helper restart is not shown there.

## Amended in 0.3.135 (Gitea #206, #207): update-live.sh rolls back, and judges a repaired box

- **`--rollback-to PREVIOUS_BLIT_DEB`** names the pair the box runs now.
  Before anything is sent, `update-live.sh` checks it as it checks the
  update -- the compositor beside it, its own
  `blit-release_<version>_amd64.sig`, its manifest -- and that it is not
  the release being installed, not older than the helper's floor
  (`UPDATE_OLDEST_RELEASE`, read from the checkout's helper source) and the
  version the box answers with. When the smoke check after the update
  fails, it installs that pair through the same API, signed by its own
  `.sig`, and runs the smoke check again against the state it recorded
  before the update. It is an option: the standing deploy command names
  one package, and N-1's `.sig` is not always kept (a release's output
  directory keeps it; `tools/release/sign-release.sh` recreates it from
  any copy of the packages). Making it the default is the owner's call.
  It does not roll back after an install that timed out: the API may be
  down, or dpkg still running under the helper's 600 s bound.
- **Exit status**: 0 installed and passed; 1 failed (read the message;
  the update may be installed); 2 refused before anything was sent; 3 the
  smoke check failed and the previous pair is back and passes it; 4 the
  rollback failed too.
- **The smoke check holds the box to at least a state**, not to equality:
  as many displays up or more, a surface in as many panes or the login
  screen up, and frames advancing when it has displays. A box that had no
  display ready passes with displays up and frames advancing, or, after
  10 s (a restarting display's grace), with none: the rollback that
  brought a display back used to end "smoke failed" because the broken box
  it found had none, and could pass "API only" before its display was up.
  `BLIT_SMOKE_BASELINE` gives it the state a failed run printed, for a
  rollback by hand; `BLIT_SMOKE_SECONDS` (default 60) bounds it.
- **A manifest named for the whole package version**
  (`releases/<version>-<revision>.yaml`) is taken before
  `releases/<version>.yaml`. Releases have only the latter; the rollback
  rehearsal's fabricated N-1 is N's version at revision 0, and
  `--rollback-to` needs both manifests at once.
- Evidence: `update_live_rollback_gate` (new, light: update-live.sh whole
  against a fake server on 127.0.0.1 with scratch packages) -- the
  rollback with N-1's own parts, signature and both tokens, exit 3; a
  rollback by hand passing the repaired box; a recorded baseline that the
  repaired box does not meet; a box with no display, passed "API only"
  after the grace; a failed rollback, exit 4; a good update
  with one upload; and the refusals (no `.sig`, below the floor, not the
  version the box runs) with nothing sent. With the update-live.sh before
  this change it fails at `rollback` (usage, exit 2) and at `recovery`
  ("smoke failed", exit 1). `rollback_recovery_gate`'s `smoke-fails` now
  runs `--rollback-to` and ends at N-1 with the display back, exit 3;
  `smoke-rollback` breaks N again and requires "smoke passed" with the
  display up (none before), exit 0.
