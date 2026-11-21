# ADR 0232: An update installs blit and its compositor together

- Status: accepted
- Note: Shipped in 0.3.97; the evidence below was recorded on 2026-11-13.
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
