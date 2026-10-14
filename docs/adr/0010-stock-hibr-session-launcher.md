# ADR 0010: Launch the stock Hibr session in one module-owning process

- Status: Accepted; production-path wording amended by ADR 0069
- Date: 2026-10-07

## Context

Blit must exercise Hibr's ordinary `desktop`/`--join` session without
duplicating or modifying Hibr's desktop implementation. Hibr's non-interactive
scripts do not read `HIBR_RC`, and a root Hibr process deliberately ignores
`HIBR_MODPATH` and searches only its compiled module directory. A wrapper that
loads a module and then `exec`s a second Hibr process therefore loses the
loaded interfaces.

## Decision

Blit's QEMU/integration fixture uses the Blit-owned launcher
`hibr/blit-hibr-console.hibr`. It loads Blit's `console-blit` provider and
Hibr's `pty`, `term`, and `hold` modules by explicit staged paths before the
unchanged stock `session.hibr` is sourced in the same process. The provider
supplies both Hibr's `display` interface and the `console` commands; loading
Hibr's local `console` provider alongside it would be ambiguous and would not
exercise Blit's display path. The launcher defaults Hibr's `DT_TERMMS` to
zero, unless the caller supplies it, because Blit already enforces the
display-refresh cadence and Hibr's stock 70 ms terminal throttle would cap
program-driven redraws below the display rate.

## Consequences

The fixture tests Hibr's actual held-session and `--join` behavior, including
supervisor restart and auxiliary reconnect, while remaining independent of
Hibr's install prefix and root module-search policy. The launcher is an
integration fixture, not a runtime shell wrapper or a replacement for Hibr's
desktop command. The packaged `blit-session.hibr` entrypoint now applies the
same one-process provider rule in production, as ADR 0069 records. A physical
multi-monitor run remains a separate release gate.
