# ADR 0052: Expose surface content counters in compact diagnostics

Status: accepted

## Context

`GET /v1/surfaces/{surface_id}/summary` originally reported only
`non_empty_cells`. That count includes styled blank cells, so it could not
distinguish a surface that had been allocated and styled from one containing
actual terminal codepoints. This distinction matters for Hibr joins: the
stock desktop intentionally keeps its chrome on the primary display, so a
valid auxiliary join may contain only blank cells until a window is placed
there.

## Decision

Add the required `codepoint_cells` and `printable_cells` counters to
`SurfaceSummary`. `codepoint_cells` counts cells containing any codepoint,
including U+0020; `printable_cells` counts printable codepoints. The counters
are compact diagnostics, not a replacement for the authoritative cells
snapshot.

## Consequences

- API clients can classify blank, styled, and populated surfaces without
  transferring the full cell matrix.
- QEMU visual tests can distinguish an intentional Hibr blank join from a
  renderer that dropped populated content.
- The fields are additive within API version 0.1; unknown future fields remain
  ignorable for clients.
- A screenshot remains necessary to prove pixels were composed and presented;
  counters alone do not prove scanout.

## Alternatives rejected

- Treating `non_empty_cells > 0` as proof of visible text: rejected because
  styled spaces satisfy that predicate.
- Parsing the full `/cells` response in every diagnostic client: rejected as
  unnecessarily large and slower for remote troubleshooting.
- Changing Hibr to paint auxiliary joins: rejected; Hibr owns desktop
  semantics and its documented primary/auxiliary behavior is correct.
