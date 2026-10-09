# Blit documentation

blit is a schema-first, API-driven text display server with a native SDL3
renderer and an integrated library of CRT/retro-terminal effects. It is
intended to provide a text-centric display surface without requiring X11 or
Wayland, while still supporting a desktop-sized canvas spanning multiple
monitors.

Canonical repository: `git.home.arpa/itdlabs/blit`

This directory is the project contract. Implementation work should update the
relevant documentation and schema in the same change.

## Source-of-truth order

When two documents appear to disagree, resolve the discrepancy in this order:

1. `openapi/blit-0.1.yaml` for HTTP, WebSocket, schemas, status codes, and
   public field names.
2. The C implementation for behavior already inside the published contract.
3. An accepted ADR for an architectural or semantic decision.
4. The verification ledger for what has actually been built and observed.
5. Guides and READMEs for explanation, examples, and operator workflow.

Guides must not invent fields or promise capabilities absent from the schema.
If a useful behavior is not yet in the schema, record it as roadmap work or an
ADR proposal instead of documenting it as an API guarantee.

The installed runtime boundary is the TCC-built `blit-server` binary. No
shell launcher is shipped in the runtime package: Hibr remains an
independently installed executable and is controlled through Blit's API and
Hibr's own desktop/session commands. Blit starts an API-requested command
directly with `execve`; it does not insert a shell between the PTY and the
requested executable. Shell files in `tools/` are only reproducible build and
test fixtures, never runtime dependencies.

Do not distribute `build/blit-desktop-current` as a desktop bundle. It is a
build staging directory, and the Debian builder rejects stale launchers or
other non-runtime files found beside the native server. Start Hibr separately
using Hibr's installed `desktop` command and use Blit's API for surfaces,
panes, input routing, and diagnostics.

## Documentation map

- [Vision and scope](vision.md) — what blit is, who it serves, and the
  initial non-goals.
- [Architecture](architecture.md) — the proposed runtime model and the major
  boundaries between API, terminal state, effects, layout, and rendering.
- [API guide](api.md) — endpoint map, safe call ordering, Hibr orchestration,
  diagnostics, streaming, errors, and client rules.
- [API principles](spec/api-principles.md) — schema-first rules for designing
  the public API.
- [OpenAPI contract](../openapi/blit-0.1.yaml) — the canonical version 0.1
  network API and stream message schemas.
- [Engineering constitution](engineering-constitution.md) — the project’s
  non-negotiable development constraints.
- [Development workflow](development-workflow.md) — ADR, testing, evidence,
  commit, and push cadence for each implementation cycle.
- [Toolchain baseline](toolchain.md) — the compiler and dependency gates for
  this workspace.
- [Roadmap](roadmap.md) — the ordered implementation milestones and exit
  criteria.
- [Implementation and verification contract](implementation-and-verification.md)
  — normative requirements, release gates, test matrix, and handover package.
- [Bare-metal testing](testing-baremetal.md) — how to test direct KMSDRM on a
  Linux host without X11 or Wayland.
- [Gateway testing](testing-gateway.md) — how to build and exercise the first
  TCP/OpenAPI/WebSocket gateway slice.
- [Effect testing](testing-effects.md) — deterministic SDL effect validation
  and direct-KMS capability behavior.
- [hibr integration](integration-hibr.md) — standalone-service decision,
  PTY compatibility probe, and the operational console-module adapter.
- [ADR index](adr/README.md) — decisions that constrain implementation.
- [Verification status](verification-status.md) — the current evidence ledger,
  release gates, and explicitly unverified items.

The authenticated `GET /v1/diagnostics/runtime` endpoint is the canonical
remote troubleshooting surface. It combines the normal diagnostics snapshot
with uptime, rendered-frame count, measured render duration, missed refresh
deadlines, and a bounded in-memory event log. It is intentionally bounded so
diagnostics cannot consume unbounded server memory.

## Documentation maintenance

Every public API change updates the OpenAPI file, this guide, the relevant ADR
or roadmap entry, and at least one focused gate in the same cycle. Every
release entry names the exact source commit, package checksum, toolchain, and
limitations. Do not commit generated packages, screenshots, logs, QEMU images,
or local host captures; link to the command and evidence location instead.

## Working status

The project has a TCC-built server, deterministic VKMS/KMS coverage, passing
terminal and Hibr acceptance fixtures, and an operational Hibr console
adapter. Physical-host release acceptance remains open; the exact evidence
and the separate physical pointer, multi-monitor, and 120/240 Hz hardware
gates are recorded in
[verification-status.md](verification-status.md). Items marked as open
questions should be resolved through an ADR before they become hidden
implementation assumptions.
