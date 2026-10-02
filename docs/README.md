# blit documentation

blit is a schema-first, API-driven text display server with a native SDL3
renderer and an integrated library of CRT/retro-terminal effects. It is
intended to provide a text-centric display surface without requiring X11 or
Wayland, while still supporting a desktop-sized canvas spanning multiple
monitors.

Canonical repository: `git.home.arpa/itdlabs/blit`

This directory is the project contract. Implementation work should update the
relevant documentation and schema in the same change.

## Documentation map

- [Vision and scope](vision.md) — what blit is, who it serves, and the
  initial non-goals.
- [Architecture](architecture.md) — the proposed runtime model and the major
  boundaries between API, terminal state, effects, layout, and rendering.
- [API principles](spec/api-principles.md) — schema-first rules for designing
  the public API.
- [OpenAPI contract](../openapi/blit-0.1.yaml) — the canonical version 0.1
  network API and stream message schemas.
- [Engineering constitution](engineering-constitution.md) — the project’s
  non-negotiable development constraints.
- [Toolchain baseline](toolchain.md) — the compiler and dependency gates for
  this workspace.
- [Roadmap](roadmap.md) — the ordered implementation milestones and exit
  criteria.
- [Bare-metal testing](testing-baremetal.md) — how to test direct KMSDRM on a
  Linux host without X11 or Wayland.
- [Gateway testing](testing-gateway.md) — how to build and exercise the first
  TCP/OpenAPI/WebSocket gateway slice.
- [hibr integration](integration-hibr.md) — standalone-service decision,
  PTY compatibility probe, and the planned console-module adapter.
- [ADR index](adr/README.md) — decisions that constrain implementation.

## Working status

The project has a running TCC-built vertical slice and deterministic VKMS
coverage. The hibr PTY compatibility probe is passing; the production hibr
console adapter and operational hardening remain planned work. Items marked as
open questions should be resolved through an ADR before they become hidden
implementation assumptions.
