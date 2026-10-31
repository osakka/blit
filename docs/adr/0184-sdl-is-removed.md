# ADR 0184: SDL is removed; native KMS is the only presentation path

- Status: accepted; in use since 0.2.22
- Date: 2026-10-25
- Scope: presentation, startup, settings, diagnostics, gates, packaging
- Supersedes: ADR 0129, 0137, 0138, 0152, 0153, 0155
- Retires: the SDL-internal ADRs 0012, 0014, 0017, 0099 (they describe code
  that no longer exists)

## Context

ADR 0181 put a GLES compositor inside native KMS and said the SDL path would
be "kept until the GLES cutover". On the host, every frame is now composed
by the GPU (0.4–1.7 ms per frame), with the CPU compositor as the fallback
inside the same path. The package has always defaulted to `kms`.

SDL was a second compositor end to end. It had its own:
- renderer;
- glyph atlas and geometry batches;
- picture and scene-damage caches;
- per-surface compose pass;
- route to the screen (KMSDRM);
- startup trial machinery (ADR 0153).

Every rendering change had to be built and verified twice, and ADR 0137
("backend parity") was the standing cost of that. Issue #84 shows it: the
same rule was fixed on the SDL path, then had to be re-verified on KMS
under #88.

The owner never runs Blit in a window on X11 or Wayland, which is the one
thing only SDL could do.

## Decision

Native KMS is the single presentation path. Its GLES compositor is the
default, and the CPU compositor is its fallback. SDL is deleted:
- the SDL renderer and compose pass;
- SDL display discovery and the SDL screenshot route;
- SDL mouse events;
- the KMSDRM policy;
- the presentation trial (`/v1/control/presentation-trial`, and the
  helper's `TRIAL_*` commands and recovery timer), which only ever chose an
  SDL renderer;
- `libSDL3` in the package.

There is no compatibility path; this is a fresh project.
- `server.display_backend` keeps its key with `kms` as the only valid value.
  A saved `sdl` is refused like any other invalid value.
- `BLIT_DISPLAY_BACKEND`, if set to anything else, is diagnosed the same
  way.

Degrading gracefully stays inside the one path:
- GPU compose fails → the CPU compositor;
- no DRM device → Blit starts API-only with no outputs, and the reason in
  `/v1/diagnostics`.

## Consequences

- One compositor for every future change. The parity issues #104, #105,
  #129 and #130 close as obsolete by design.
- The gate suite has two backends, `native` (the DRM mock, CPU compositor)
  and `gles` (surfaceless GLES readback).
  - Gates that tested SDL internals are retired.
  - Gates that tested shared behaviour run on `native`/`gles`.
  - Throughput benchmarks run on the DRM mock. Its device is a regular file,
    always readable, so it paces each event poll by 1 ms, which caps loop
    throughput; making it readable only when a flip is pending is a
    follow-up.
- `tools/build-tcc.sh` no longer includes or links SDL, and the package no
  longer ships `libSDL3`.
- Display rotation goes with SDL, the only path that had a render transform:
  `rotation` is not a capability, and a PATCH with it is a 422
  `display_rotation_unsupported`. Rotation on KMS would be a GPU transform
  and is its own work; the now-unused rotation plumbing (always 0) is a
  follow-up cleanup.
- The settings key `server.renderer` and `BLIT_RENDERER` are removed; the
  runtime diagnostics lose the SDL-only fields (`direct_kms`, `video_driver`,
  `renderer`, `sdl_kms_mode`, `sdl_*`, the SDL texture-cache and batch
  counters).
