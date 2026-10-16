# Repository hygiene and handoff standard

This document defines what belongs in the Blit repository and what must stay
outside it. It is part of the engineering contract: a clean tree and a
traceable commit are prerequisites for making a runtime claim.

## Durable source of truth

The following files are authoritative for their boundaries:

| Boundary | Authority | Derived material |
| --- | --- | --- |
| HTTP, WebSocket, schemas, status codes | `openapi/blit-0.1.yaml` | `docs/api.md`, client examples, probes |
| Runtime behavior | `src/` | binaries, QEMU initramfs, package payload |
| Hibr integration boundary | `docs/integration-hibr.md`, accepted ADRs, `hibr/` adapter sources | adapter `.so`, staged Hibr fixtures |
| Architecture and policy | `docs/adr/` | implementation choices and test names |
| What is proven | `docs/verification-status.md`, `releases/*.yaml` | screenshots, logs, temporary captures |
| Build and acceptance procedure | `tools/`, `docs/implementation-and-verification.md` | generated build trees and test output |

Documentation may explain the contract, but it must not invent a field,
endpoint, lifecycle rule, or capability that is absent from the schema,
implementation, or accepted ADR.

## Keep out of Git

The following are disposable or machine-specific and belong in `/tmp` or an
explicit external artifact store:

- `build/`, `vendor/` dependency trees, QEMU disk/initramfs images;
- Debian packages, extracted package trees, screenshots, logs, and captures;
- bearer/update credentials, private host paths, and physical-host dumps;
- generated bindings and copied Hibr installations.

The repository may contain the small deterministic fixtures and source probes
that create those artifacts. It must not contain their generated output.
Existing user files are never swept into a commit merely because they are in
the workspace.

## One implementation per boundary

Every behavior has one owner. The API schema owns wire shape; the C server
owns runtime state; Hibr owns desktop semantics; the adapter only transports
events; probes observe and assert behavior. A shell script may stage or test a
component, but it is not a substitute runtime implementation. When a test
needs a new behavior, add the smallest source-level regression fixture and
document why it does not create a second implementation.

## Required cycle

For each change:

1. Identify the invariant, source of truth, and evidence required.
2. Add or update an ADR when the change affects a public or architectural
   boundary; otherwise reference the existing ADR.
3. Make the smallest implementation and regression-test change.
4. Build with TCC and run the narrow gate, then the applicable end-to-end gate.
5. Inspect diagnostics and captures; record failures as failures.
6. Update the API guide, README/map, roadmap, or verification ledger when the
   user-visible context changed.
7. Run `git diff --check`, review the staged file list, commit, and push.

A failed acceptance cycle may be committed when it adds useful diagnostics or
test coverage, but it must not be described as a passing release. The next
cycle starts from that explicit failure.

## Commit and push rules

Before committing:

```sh
git status --short --branch
git diff --check
git diff --stat
git diff --cached --name-status
```

The staged set must contain only intentional source, schema, tests, ADRs,
documentation, and release evidence. It must not contain packages, logs,
screenshots, QEMU images, secrets, or Hibr checkout changes. Push the commit
to `origin main`, then verify:

```sh
git status --short --branch
git log -1 --oneline
git ls-remote --heads origin main
```

Never rewrite pushed history to hide an experiment. If a release artifact is
published, its checksum and source commit belong in `releases/<version>.yaml`.

## Claim vocabulary

- **specified**: in the schema or an accepted ADR;
- **implemented**: present in source and covered by a focused test;
- **validated**: the matching artifact passed the relevant end-to-end gate;
- **host accepted**: the exact deployed artifact passed on the named host and
  its actual displays/input devices.

These terms are intentionally strict. A screenshot, a QEMU result, or an API
health response cannot be promoted to physical-host acceptance without the
matching evidence.

## No inference from partial evidence

Timing stability, process liveness, API acceptance, pixel correctness, physical
input delivery, and Hibr interaction are different claims. Never use a broad
word such as “stable”, “working”, “correct”, “complete”, or “fixed” when only a
narrower boundary was measured. A screenshot that is incomplete or visually
wrong is a failed visual result even if frame timing is healthy. A synthetic
input trace is not physical input evidence. A Blit input receipt is not proof
that Hibr acted on the event. Record the first unproven boundary explicitly;
do not fill it with inference. See [the current restart point](current-state.md).
