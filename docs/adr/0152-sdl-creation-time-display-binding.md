# ADR0152: select SDL output before window resource allocation

- Date:2026-10-20
- Status: implemented; physical multi-connector validation pending
- Superseded by [ADR 0184](0184-sdl-is-removed.md): SDL is removed; native KMS is the only presentation path.
- Tracking: Blit104; SDL dependency unchanged

Blit created every window with SDL_CreateWindow's default position and only
then called SDL_SetWindowPosition, ignoring its return. The pinned KMSDRM
driver resolves its display and creates GBM/EGL surfaces in CreateWindow;
its SetWindowPosition returns SDL_Unsupported. Consequently post-creation
positioning cannot be the authority for per-output resource ownership.

Use SDL_CreateWindowWithProperties with display-specific centred X/Y at
creation, preserving size/borderless policy. Verify SDL_GetDisplayForWindow
before fullscreen/renderer initialization. Never accept an unexpected binding
and label its renderer as the requested output. Destroy creation properties
on all paths and report window/binding/fullscreen/renderer failures through
the existing diagnostics log, including the requested display ID.
Keep the existing direct-KMS fallback behavior explicit; it does not count as
successful SDL acceptance. No settings schema or Hibr/vendor changes.

The production initialize_outputs regression fails before correction with
two unsupported moves and zero creation bindings. After correction, non-
contiguous IDs11/57 get distinct renderers with exact creation bindings and
zero moves. The display boundary is mocked; software and offscreen GLES
renderers are real. Injected creation, wrong binding, fullscreen and renderer
failures verify diagnostics and exact window cleanup. Native fallback is
mocked unavailable, so this test never opens a physical DRM device.

This does NOT prove SDL enumerates both physical connectors, fixes connector
hotplug, or meets physical presentation timing. The earlier host exposed only
one SDL display while native exposed two; that remains a separate investigation.
Keep native567 active during local tests, then capture enumeration and output
initialization independently in any bounded physical comparison.

References:
- https://wiki.libsdl.org/SDL3/SDL_CreateWindowWithProperties
- https://wiki.libsdl.org/SDL3/SDL_WINDOWPOS_CENTERED_DISPLAY
- pinned vendor/SDL3/src/video/kmsdrm/SDL_kmsdrmvideo.c (read only).
