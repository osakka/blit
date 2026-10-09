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
- performance/timing changes: diagnostics, measured intervals, CPU/RSS, and a
  bounded workload rather than a subjective “feels faster” claim.

QEMU is the repeatable multi-display oracle. A physical host is evidence for
that host and its current hardware only; it cannot prove untested monitor
counts, refresh modes, GPUs, or input devices.

## Evidence and claims

The verification ledger records the source commit, TCC version, package,
Hibr, kernel/QEMU fixture, command, result, and known limitations. “Implemented”
means source and focused tests pass; “validated” means the matching runtime
artifact passed the relevant end-to-end gate. Screenshots supplement API/state
assertions; neither substitutes for the other.

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
