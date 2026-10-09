# Architecture decision records

ADRs capture decisions that are expensive to reverse or affect multiple
subsystems. Each record states context, options, the decision, consequences,
and migration impact. The filename is the canonical link. A few historical
records share a numeric heading because they were developed on parallel
branches; they remain separate decisions and are all indexed below. New ADRs
must use a unique filename and number.

## Accepted decisions

- [0001 — TCC is the project compiler](0001-tcc-toolchain.md)
- [0002 — OpenAPI control plane and network streaming plane](0002-network-api-and-streaming.md)
- [0003 — Logical cell-based pane layouts](0003-logical-cell-layouts.md)
- [0004 — Canonical terminal state and direct KMS presentation](0004-terminal-state-and-direct-kms.md)
- [0005 — Kitty graphics first, sixel compatibility second](0005-kitty-graphics-first.md)
- [0006 — Display refresh is an output invariant](0006-display-refresh-invariant.md)
- [0007 — Preserve UTF-8 across PTY reads and ship Hibr glyph coverage](0007-terminal-utf8-and-hibr-font-boundary.md)
- [0008 — API-supervised package lifecycle](0008-api-supervised-package-lifecycle.md)
- [0009 — Compiled runtime version is the API identity](0009-compiled-runtime-version-identity.md)
- [0010 — Launch the stock Hibr session in one module-owning process](0010-stock-hibr-session-launcher.md)
- [0011 — Event-driven input-device discovery](0011-event-driven-input-discovery.md)
- [0012 — Cache SDL glyphs and suppress unchanged terminal damage](0012-sdl-glyph-atlas-and-noop-damage.md)
- [0013 — Interactive games and emulation are first-class workloads](0013-interactive-games-and-emulation.md)
- [0014 — Batch SDL glyph geometry for full-screen terminal redraws](0014-sdl-glyph-geometry-batching.md)
- [0015 — Nearest sampling for the SDL glyph atlas](0015-nearest-glyph-atlas-sampling.md)
- [0016 — Timestamp every streamed input event](0016-timestamped-input-stream.md)
- [0017 — Batch SDL terminal background geometry](0017-sdl-background-geometry-batching.md)
- [0018 — Expose bounded stream back-pressure telemetry](0018-stream-backpressure-telemetry.md)
- [0019 — Deterministic box-drawing glyph geometry](0019-deterministic-box-drawing-glyphs.md)
- [0020 — Reject unresolved fallback glyphs](0020-font-fallback-coverage.md)
- [0021 — Advertise the bounded sixel capability](0021-bounded-sixel-capability.md)
- [0022 — Versioned network terminal input](0022-network-terminal-input.md)
- [0023 — Bounded stream service for refresh fairness](0023-bounded-stream-service.md)
- [0024 — Pixel presentation cadence follows the producer and output path](0024-pixel-presentation-cadence.md)
- [0025 — Preserve Hibr startup diagnostics and full-surface writes](0025-hibr-startup-output-and-full-surface-writes.md)
- [0026 — Commit external Hibr cell frames atomically](0026-hibr-cell-frame-atomicity.md)
- [0027 — Invalidate the cached scene on cursor visibility transitions](0027-cursor-visibility-scene-invalidation.md)
- [0028 — Use one owner for physical pointer input](0028-single-owner-physical-pointer-input.md)
- [0029 — Hit-test display-attached sessions](0029-display-attached-session-hit-testing.md)
- [0030 — Coalesce terminal PTY writes before presentation](0030-pty-frame-quiet-window.md)
- [0031 — Make Hibr console flush the atomic frame boundary](0031-hibr-flush-is-the-frame-boundary.md)
- [0032 — Redraw the complete SDL scene for external Hibr commits](0032-external-frame-full-scene-redraw.md)
- [0033 — Reassert external Hibr sessions after PTY attach](0033-attach-time-pty-reassert.md)
- [0034 — Restrict the Hibr adapter stream to input events](0034-hibr-adapter-input-only-stream.md)
- [0035 — Clear external surfaces when their PTY exits](0035-clear-external-surface-on-session-exit.md)
- [0036 — Expose a bounded API diagnostic trace](0036-api-diagnostic-trace.md)
- [0036 — Separate external cell damage from graphics-scene invalidation](0036-external-cell-damage-and-graphics-scene-boundary.md)
- [0037 — Own Hibr adapter streams by session](0037-owned-hibr-stream-lifecycle.md)
- [0038 — Keep the optional Blit adapter outside Hibr's module directory](0038-keep-optional-adapter-out-of-hibr-moddir.md)
- [0038 — Keep framebuffer screenshot delivery out of the render loop](0038-nonblocking-screenshot-delivery.md)
- [0039 — Ignore evdev mouse-button autorepeat](0039-ignore-evdev-mouse-autorepeat.md)
- [0040 — Compose pointer motion over cached scene damage](0040-pointer-damage-uses-cached-scene.md)
- [0041 — Aggregate button state across evdev mice](0041-aggregate-evdev-button-state.md)
- [0042 — Isolate the SDL GLES2 glyph path](0042-gles2-glyph-path-isolation.md)
- [0042 — Correlated live evidence bundles](0042-live-evidence-bundle.md)
- [0042 — Explicit terminal child identity](0042-terminal-child-identity.md)
- [0043 — Define multi-monitor frame atomicity at the scene boundary](0043-multi-monitor-frame-atomicity.md)
- [0044 — Separate high-refresh scheduling from scanout evidence](0044-high-refresh-evidence-boundary.md)
- [0045 — Fail closed on unresolved Hibr resume attachments](0045-fail-closed-hibr-resume-attachments.md)
- [0046 — Free panes do not swallow display-attached input](0046-free-pane-does-not-swallow-display-input.md)
- [0047 — API-selectable fonts and logical scale](0047-api-selectable-fonts-and-logical-scale.md)
- [0047 — Restore under-text graphics on incremental damage](0047-incremental-damage-restores-under-text-graphics.md)
- [0048 — One primary Hibr desktop owner](0048-one-hibr-desktop-owner.md)
- [0048 — Temporal physical-experience capture is the troubleshooting boundary](0048-temporal-physical-experience-capture.md)
- [0049 — Retire KMS work only after output submission](0049-kms-submission-retirement-boundary.md)

## Adding an ADR

Use [the template](TEMPLATE.md). Add the record to this index in the same
commit as the decision. An implementation change that alters the public API,
terminal semantics, rendering boundary, input ownership, display topology,
security model, or dependency/toolchain policy requires an ADR or an explicit
reference to an existing one.
