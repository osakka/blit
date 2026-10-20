# Blit development workflow

This is the operating procedure for every implementation cycle. It keeps the
repository, the running server, and the evidence about them aligned.

## Before changing code

1. Read the relevant schema, architecture note, ADR, and current verification
   entry before editing.
2. Check `git status --short --branch`. Existing untracked logs, screenshots,
   packages, and user files are preserved; they are never swept into a commit
   accidentally.
3. State the invariant being changed and the exact evidence that will prove it.
4. If the change affects a public API, protocol, renderer boundary, terminal
   semantics, display topology, dependency, security policy, or another
   subsystem, write or update an ADR before implementation. Record options,
   decision, consequences, migration, and rejected alternatives.

Small defect fixes may proceed without a new ADR when they preserve an existing
decision. They still require a test and an evidence update.

## One cycle, one source of truth

Each cycle has one narrow change set: change the canonical implementation or
schema; add a regression test; update normative documentation and the
verification ledger; build with TCC; run the relevant gates; inspect captures
and diagnostics; and clean up temporary processes and resources.

Adapters, packages, screenshots, generated bindings, and staging trees are
derived artifacts. They must not become a second implementation or an
undocumented source of behavior. Hibr remains an external dependency: Blit
tests its released executable and modules but does not modify the Hibr tree.
The adapter build must name the exact Hibr checkout whose `HIBR_VER` matches
`packaging/HIBR_VERSION`; a newer development checkout is a deliberate QEMU
fixture input, not a valid package build target.

## Commit and push cadence

Every completed cycle ends with `git diff --check`, an intentional-file review,
a focused commit, `git push origin main`, and a final branch-status check. The
commit contains the implementation, tests, ADR/documentation changes, and the
evidence needed to understand the result. Do not make a “code now, docs later”
commit or push a package whose binary was not built from that commit. A
generated package may remain untracked locally, but its version, SHA-256, and
build commit belong in the handoff or verification ledger.

A failed cycle is also recorded: commit only a useful, coherent diagnostic or
test improvement, explain the failure, and do not claim the feature passed.
Never amend or rewrite pushed history to hide a failed experiment.

The commit message should identify the boundary changed and the evidence
updated, for example `document API lifecycle and release evidence`. Before
push, inspect the staged file list and confirm that it contains no package,
screenshot, log, QEMU image, token, or unrelated Hibr checkout. After push,
record the commit ID in the release manifest or evidence ledger before making
the next runtime claim.

## Reconcile and retire completed branches

`main` is the canonical integration branch. A temporary maintenance branch may
isolate a released runtime from pending feature work, but its complete tip,
including release/documentation follow-ups, must be reconciled back into main.
Copying/cherry-picking equivalent changes does not by itself reconcile ancestry.

Before merging, fetch the remote, inspect both worktrees (including ignored
build/evidence files), compare unique commits and actual content, and identify
changes already integrated. Merge the reviewed tip without rewriting history.
Resolve overlapping evidence by preserving one historical record and all newer
results, not by duplicating sections or downgrading current source/manifests.
If only history/docs differ, assert production-path tree identity and rebuild
to verify unchanged binary hashes; run the relevant regression gates anyway.

After verification, push main and retire only the exact merged branch tip.
Use an explicit expected remote revision when deleting the remote branch so a
concurrent update cannot be discarded. Preserve historical ignored artifacts:
detach their worktree at the integrated historical commit rather than forcing
its removal. Update the tracking issue and restart-point documentation. A
retained historical build snapshot is not an active development/config owner.

## Required gates

Use the narrowest gate that proves the change, then run the broader gate before
handoff:

- schema/API changes: schema validation, API conformance, and version identity;
- terminal changes: TCC build and `tools/tests/terminal_conformance.rb`;
- font/Unicode changes: glyph coverage, UTF-8 snapshots, and a Hibr capture;
- renderer/input changes: dummy backend plus direct-KMS/VKMS pixel and input;
- multi-display/Hibr changes: two-output topology, pane matrix, primary plus
  `--join`, routing, and cleanup;
- packaging changes: allowlist, embedded version identity, install/service
  checks, and checksum;
- presentation startup/recovery changes: lifecycle suite including
  `presentation_trial_gate.sh` and `presentation_trial_helper_gate.rb`; record
  their mocked-systemd boundary separately from real timer/service recovery;
- performance/timing changes: diagnostics, measured intervals, CPU/RSS, and a
  bounded workload rather than a subjective “feels faster” claim.

`tools/run-gates.sh` is that list, executable. Its suite names match the
categories above, so the narrowest run is one suite and the pre-handoff run is
`all`:

```sh
tools/run-gates.sh terminal          # the narrowest gate for a terminal change
tools/run-gates.sh all               # before handoff
tools/run-gates.sh gles              # explicit offscreen GLES2 composition check
BLIT_GATE_PACKAGE=blit_X.Y.Z-1_amd64.deb tools/run-gates.sh packaging
BLIT_GATE_LIVE=http://HOST:8080 tools/run-gates.sh timing
```

Prefer it over invoking a gate directly.

The explicit `gles` suite runs shared glyph, Kitty layering, motion-trail,
wallpaper-damage, held-scene pointer and two-renderer output-metrics fixtures through SDL
offscreen/OpenGLES2 using software GL. It requires working
EGL/GLES libraries and fails if actual renderer identity differs. It is
separate from `all`, which remains runnable without EGL/GLES. Record its
result alongside `all` for SDL changes; inability to run it is an open
coverage boundary. This proves GL composition on software GL, not GPU/KMSDRM
scanout or physical refresh-rate performance.

The gates do not share one calling
convention: some take `BLIT_TEST_SERVER`, some `BLIT_GATE_SERVER`, some
`BLIT_GATE_PORT`, some a positional argument; `terminal_conformance.rb` and
`terminal_soak_gate.rb` assert against a server the caller must start; and
`http_fragmentation_gate.rb` and `http_backpressure_gate.rb` are sub-gates
that `network_auth_gate.sh` invokes with its own port, so running either
standalone reports a failure that is not one. The runner owns those details.
A gate that needs QEMU, physical hardware, a package, or a live host is
skipped with that reason rather than counted as a pass or a failure.

QEMU is the repeatable multi-display oracle, and the runner never implies it:
run `tools/tests/qemu_hibr_lifecycle_gate.sh` explicitly. A physical host is
evidence for that host and its current hardware only; it cannot prove untested
monitor counts, refresh modes, GPUs, or input devices.

## What a green suite does not mean

`RUN_GATES_PASS pass=69 skip=4 fail=0` means the behaviour the suite can reach
is correct. It is not a statement about the behaviour it cannot reach, and the
distance between those two has cost this project multiple releases. The known
blind spots, so that a green run is read for what it is:

**Renderer coverage depends on the selected suite.** Historically the SDL
renderer gates ran only against `software` in a container without a GPU,
while the physical SDL path used `opengles2`. An optimisation that assumed
render-target persistence produced a visible screensaver trail that survived
two "fixed" claims. Five reproductions passed because all five ran on
`software`. See ADRs0070/0099 and issue79. The explicit `gles` suite now covers
glyphs, Kitty layering, motion trails, wallpaper damage/deletion, held-scene pointer pixels and independent output
metrics through offscreen/OpenGLES2 using software GL. The remaining fixtures
and physical KMSDRM/GPU timing still require matching-backend acceptance.

**A gate that cannot fail is not evidence.** Every gate should be run once
against the reverted change and observed to fail; a gate added alongside a fix
and never seen red proves only that it is consistent with the fix.
`deferred_damage_gate.rb` has no teeth and its header says so and why —
that is the minimum honest treatment, not a precedent to copy.
`evdev_char_gate.c` asserts all 26 letters rather than a sample for this
reason: the arithmetic it replaced was correct for exactly one letter, so a
spot check would have passed against the bug.

**The fixtures are not the devices.** A fabricated evdev node is a regular
file, so it is never opened as a keyboard and `state.open` stays false — the
hotplug gate exercises the device table but not the poll set, which had to be
measured on the host directly. Fabricated nodes also cannot reproduce a USB
hub that resets 66 times.

**The deployment path is partly unexercised.** The direct-KMS presentation
path is not covered by the default run; `physical_multi_display_gate.rb`,
`screenshot_nonblocking_gate.sh`, and the QEMU oracle are opt-in and absent
from the reported totals. `tools/probes/host_cell_pixel_agreement.rb`, which
states the cells-versus-pixels invariant, is run by no automated path at all.
Unprivileged-profile access to backlight and control devices is unmeasured.
A skip that is never taken is indistinguishable from a test that does not
exist.

**The external boundary is the thinnest-covered and the most load-bearing.**
`console-blit` is Blit-owned code compiled against external Hibr headers and
loaded into an external process. Two of four consecutive releases fixed
defects there — a key *release* translated into a keystroke, and
`'a' + code - KEY_A`, which assumes evdev numbers letters alphabetically when
it numbers them by QWERTY row. Both were invisible to a green suite, because
a plain terminal session goes through Blit's own correct keycode table and a
desktop does not. ADRs 0068 and 0069 and issue #81.

**A desktop's behaviour is not Blit's behaviour.** An owned surface takes
input from the event stream rather than the PTY (ADR 0057), so "keys not
delivered" is the expected reading for a desktop and not a fault. A pointer
click reaches Hibr only once the owner enables mouse reporting; a Blit-hosted
desktop that never sends `console mouse <mode>` cannot be clicked no matter
how correct Blit's `pointer_button` events are. Blit publishing an event and
the desktop acting on it are separate claims requiring separate evidence.

The general rule behind all of these: **the gate environment is not the
deployment environment**, and a fault that lives in the difference is one a
green suite will report as absent. When the machine and the suite disagree,
the machine is right.

## Evidence and claims

The verification ledger records the source commit, TCC version, package,
Hibr, kernel/QEMU fixture, command, result, and known limitations. “Implemented”
means source and focused tests pass; “validated” means the matching runtime
artifact passed the relevant end-to-end gate. Screenshots supplement API/state
assertions; neither substitutes for the other.

Building a Debian artifact requires the exact pinned Hibr checkout as an
explicit input; omission fails closed:

```sh
BLIT_HIBR_ROOT=/path/to/hibr-$(sed -n '1p' packaging/HIBR_VERSION) \
  packaging/build-deb.sh /tmp/blit-package
```

The machine-owned identity fields in `releases/<version>.yaml` are generated
from the package, not copied by hand. After building the package from the
source/documentation commit, generate or check them with:

```sh
BLIT_SOURCE_COMMIT=$(git rev-parse HEAD) \
BLIT_DOCUMENTATION_COMMIT=$(git rev-parse HEAD) \
  tools/release/generate-manifest.sh \
  /path/to/blit_X.Y.Z-1_amd64.deb /tmp/generated-release-identity.yaml

tools/release/generate-manifest.sh --check \
  /path/to/blit_X.Y.Z-1_amd64.deb releases/X.Y.Z.yaml
```

The generator owns package/version/checksum/hash/Hibr-pin facts. Measured
acceptance fields and notes remain explicit evidence in the release record;
the generator never invents physical, GPU, Hibr, or refresh results. The
release identity decision is [ADR 0091](adr/0091-generated-release-identity.md).

Package documentation is compared with `package_documentation_commit`, not
whatever handoff notes have since been committed to HEAD. Rebuilding that
artifact uses `BLIT_PACKAGE_DOCUMENTATION_COMMIT=<recorded-commit>` so updated
verification notes do not silently change the package or force a checksum
rewrite. Both installed documentation locations derive from the same snapshot.

When evidence contradicts an assumption, stop repeating the same invocation.
Preserve the log, identify the boundary that produced the contradiction, and
fix or instrument that boundary before the next cycle.

## End-of-cycle checklist

- [ ] Source of truth and affected contract are identified.
- [ ] ADR added or existing ADR explicitly applies.
- [ ] Regression test and relevant end-to-end gate pass.
- [ ] Exact binary/package version is recorded and matches the source version.
- [ ] Logs, captures, process state, and resource cleanup were checked.
- [ ] Documentation and verification ledger match the evidence.
- [ ] Only intended files are committed.
- [ ] Commit is pushed and branch status is reported.

## Service-boundary regression

`ruby tools/tests/service_boundary_gate.rb --static` verifies the shipped
unit policy. Without `--static`, it needs local systemd/passwordless sudo and
uses isolated transient units: real socket visibility and restart limiting.
`session_log_gate.rb` verifies private default log directories through API
sessions; both belong to the lifecycle suite. Neither test changes the live
display host or edits Hibr. See ADR0167.
