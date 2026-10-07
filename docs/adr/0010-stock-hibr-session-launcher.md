# ADR 0010: Launch the stock Hibr session in one module-owning process

- Status: Accepted
- Date: 2026-10-07

## Context

Blit must exercise Hibr's ordinary `desktop`/`--join` session without
duplicating or modifying Hibr's desktop implementation. Hibr's non-interactive
scripts do not read `HIBR_RC`, and a root Hibr process deliberately ignores
`HIBR_MODPATH` and searches only its compiled module directory. A wrapper that
loads a module and then `exec`s a second Hibr process therefore loses the
loaded interfaces.

## Decision

Blit's QEMU/integration fixture uses `hibr/blit-stock-session.hibr`. It loads
Hibr's canonical `console`, `pty`, `term`, and `hold` modules by explicit
staged paths, then sources the unchanged stock `session.hibr` in the same
process. The existing `console-blit` module remains limited to its dedicated
provider/adapter probe and is not used by the stock desktop path.

## Consequences

The fixture tests Hibr's actual held-session and `--join` behavior, including
supervisor restart and auxiliary reconnect, while remaining independent of
Hibr's install prefix and root module-search policy. The launcher is an
integration fixture, not a runtime shell wrapper or a replacement for Hibr's
desktop command. A physical multi-monitor run remains a separate release gate.
