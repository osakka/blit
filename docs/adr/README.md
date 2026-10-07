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

Pending decisions:

1. Multi-monitor frame atomicity policy.

The implementation cadence, evidence requirements, and commit hygiene are
defined in [the development workflow](../development-workflow.md), not
duplicated in individual ADRs.
