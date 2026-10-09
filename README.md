# Blit

Blit is an API-driven, text-first display server for Linux. It owns terminal
surfaces, physical outputs, input routes, effects, diagnostics, and bounded
graphic overlays behind one versioned OpenAPI contract. Its display path is
SDL3/KMS-oriented and does not require X11 or Wayland.

The project is written and built with TCC at the Blit boundary. Hibr is an
independent runtime dependency: Blit launches and observes the installed Hibr
desktop/session commands, but never edits the Hibr source tree.

## What exists today

- TCP HTTP API at port 8080, with a versioned `/v1` resource namespace.
- OpenAPI 3.1 source of truth in [`openapi/blit-0.1.yaml`](openapi/blit-0.1.yaml).
- WebSocket event and bounded pixel-stream upgrade at `/v1/stream`.
- PTY-backed terminal sessions with revisioned cell surfaces and scrollback.
- Direct KMS and SDL3 presentation paths, refresh-aware scheduling, and
  diagnostics for frames, render time, missed deadlines, input, and streams.
- Logical multi-display topology and pane layouts, including Hibr primary plus
  `desktop --join` ownership rules.
- API-selectable font profiles and logical scale, Kitty-first graphics, and a
  bounded Sixel capability.
- TCC builds, QEMU/VKMS fixtures, terminal conformance, package gates, and
  live-host diagnostic gates.

Physical multi-monitor, physical evdev, physical high-refresh, and full Hibr
geometry acceptance remain explicitly open. See the [verification ledger](docs/verification-status.md);
the project does not turn a virtual or partial result into a hardware claim.

## Start with the contract

Read the [API guide](docs/api.md) for the operator and client workflow, then
use the [OpenAPI document](openapi/blit-0.1.yaml) for exact schemas. The most
useful first requests are:

```sh
curl http://127.0.0.1:8080/v1/version
curl http://127.0.0.1:8080/v1/server
curl http://127.0.0.1:8080/v1/displays
curl http://127.0.0.1:8080/v1/diagnostics/runtime
```

When authentication is configured, add `Authorization: Bearer TOKEN` to API
requests. Never put credentials in source, logs, screenshots, or commits.

## Build and verify

```sh
tools/build-tcc.sh build/blit/blit-server server
ruby tools/tests/terminal_conformance.rb
ruby tools/tests/hibr_singleton_gate.rb
tools/tests/package_artifact_gate.sh blit_0.1.398-1_amd64.deb
```

The exact test matrix and QEMU commands are in
[`docs/implementation-and-verification.md`](docs/implementation-and-verification.md).
The development workflow requires one source-of-truth change, a focused gate,
an evidence update, `git diff --check`, and a deliberate commit/push for each
cycle.

## Documentation map

- [Vision and scope](docs/vision.md)
- [Architecture](docs/architecture.md)
- [API guide](docs/api.md)
- [Hibr integration](docs/integration-hibr.md)
- [Terminal engine](docs/terminal-engine.md)
- [Testing and evidence](docs/implementation-and-verification.md)
- [Development workflow](docs/development-workflow.md)
- [Roadmap](docs/roadmap.md)
- [ADR index](docs/adr/README.md)
- [Verification status](docs/verification-status.md)

## Project boundaries

The repository's `tools/` scripts are build, test, deployment, and diagnostic
fixtures. They are not runtime dependencies or an alternative implementation.
Generated build trees, vendor dependency trees, screenshots, logs, old
packages, and QEMU outputs belong outside version control.
