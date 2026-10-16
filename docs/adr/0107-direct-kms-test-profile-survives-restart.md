# ADR 0107: The direct-KMS test profile survives package restart

- Status: accepted
- Date: 2026-10-17
- Scope: packaged service boot configuration and renderer verification
- Related: [ADR 0004](0004-terminal-state-and-direct-kms.md)

## Context

The physical motion investigation is scoped to direct DRM/KMS with one
display split into an upper and lower pane. Direct KMS is selected at process
startup by `BLIT_DISPLAY_BACKEND=kms`; it is not the same path as SDL's
`kmsdrm` video driver and cannot be inferred from the API's requested SDL
renderer.

The package previously preserved an existing environment file but did not
ensure that a deployment created by the physical test workflow retained the
direct-KMS selection after a restart. A package update could therefore return
the host to SDL/OpenGL and invalidate the renderer comparison.

## Decision

The packaged test profile writes `BLIT_DISPLAY_BACKEND=kms` when the service
environment has no explicit backend. An explicit operator choice is preserved.
The package gate checks that this boot contract remains present. Pane placement
and login ownership remain API state; the service environment selects only
the presentation backend.

## Consequences

- A restart no longer silently changes the renderer under test.
- SDL remains available as an explicit operator-selected path.
- Live verification must read `diagnostics.runtime` before any visual claim.
- Hibr source and Hibr session semantics are unchanged.

## Verification

The package gate checks the generated service configuration. The live release
must report `backend=drm-kms`, `direct_kms=true`, and
`presentation_sync=drm-page-flip` before the motion reproduction begins.
