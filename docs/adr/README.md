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

Pending decisions:

1. Multi-monitor frame atomicity policy.

The implementation cadence, evidence requirements, and commit hygiene are
defined in [the development workflow](../development-workflow.md), not
duplicated in individual ADRs.

Use [the ADR template](TEMPLATE.md) for a new decision, then add the accepted
record to this index.
