# Architecture decision records

ADRs capture decisions that are expensive to reverse or that affect multiple
subsystems. Proposed decisions should describe the context, considered
options, decision, consequences, and migration impact.

## Index

Accepted decisions:

1. [TCC is the project compiler](0001-tcc-toolchain.md).
2. [OpenAPI control plane and network streaming plane](0002-network-api-and-streaming.md).
3. [Logical cell-based pane layouts](0003-logical-cell-layouts.md).
4. [Canonical terminal state and direct KMS presentation](0004-terminal-state-and-direct-kms.md).
5. [Kitty graphics first, sixel compatibility second](0005-kitty-graphics-first.md).
6. [Display refresh is an output invariant](0006-display-refresh-invariant.md).
7. [PTY UTF-8 boundaries and Hibr font coverage](0007-terminal-utf8-and-hibr-font-boundary.md).
8. [API-supervised package lifecycle](0008-api-supervised-package-lifecycle.md).
9. [Compiled runtime version identity](0009-compiled-runtime-version-identity.md).
10. [Stock Hibr session launcher](0010-stock-hibr-session-launcher.md).
11. [Event-driven input-device discovery](0011-event-driven-input-discovery.md).
12. [SDL glyph atlas and unchanged-damage suppression](0012-sdl-glyph-atlas-and-noop-damage.md).
13. [Interactive games and emulation are first-class workloads](0013-interactive-games-and-emulation.md).
14. [SDL glyph geometry batching for full-screen redraws](0014-sdl-glyph-geometry-batching.md).
15. [Nearest sampling for the SDL glyph atlas](0015-nearest-glyph-atlas-sampling.md).
16. [Timestamp every streamed input event](0016-timestamped-input-stream.md).
17. [Batch SDL terminal background geometry](0017-sdl-background-geometry-batching.md).
18. [Expose bounded stream back-pressure telemetry](0018-stream-backpressure-telemetry.md).
19. [Deterministic box-drawing glyph geometry](0019-deterministic-box-drawing-glyphs.md).
20. [Reject unresolved fallback glyphs](0020-font-fallback-coverage.md).
21. [Advertise the bounded sixel capability](0021-bounded-sixel-capability.md).
22. [Versioned network terminal input](0022-network-terminal-input.md).
23. [Bounded stream service for refresh fairness](0023-bounded-stream-service.md).
24. [Pixel presentation cadence follows the producer and output path](0024-pixel-presentation-cadence.md).
25. [Preserve Hibr startup diagnostics and full-surface writes](0025-hibr-startup-output-and-full-surface-writes.md).
26. [Commit external Hibr cell frames atomically](0026-hibr-cell-frame-atomicity.md).
27. [Ignore evdev mouse-button autorepeat](0039-ignore-evdev-mouse-autorepeat.md).
28. [Compose pointer motion over cached scene damage](0040-pointer-damage-uses-cached-scene.md).
29. [Aggregate button state across evdev mice](0041-aggregate-evdev-button-state.md).
30. [Correlated live evidence bundles](0042-live-evidence-bundle.md).
31. [Define multi-monitor frame atomicity at the scene boundary](0043-multi-monitor-frame-atomicity.md).
32. [Free panes do not swallow display-attached input](0046-free-pane-does-not-swallow-display-input.md).
33. [API-selectable fonts and logical scale](0047-api-selectable-fonts-and-logical-scale.md).
34. [One primary Hibr desktop owner](0048-one-hibr-desktop-owner.md).

Pending decisions:

No pending architecture decision records.

The implementation cadence, evidence requirements, and commit hygiene are
defined in [the development workflow](../development-workflow.md), not
duplicated in individual ADRs.

Use [the ADR template](TEMPLATE.md) for a new decision, then add the accepted
record to this index.
