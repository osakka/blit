# Blit

Blit is an API-driven, text-first display server for Linux. It owns terminal
surfaces, physical outputs, input routes, effects, diagnostics, and bounded
graphic overlays behind one versioned OpenAPI contract. Its display path is
SDL3/KMS-oriented and does not require X11 or Wayland.

The project is written and built with TCC at the Blit boundary. Hibr is an
independent runtime dependency: Blit launches and observes the installed Hibr
desktop/session commands, but never edits the Hibr source tree.

## The short version

Blit is a server, not a terminal launcher script and not a replacement for
Hibr. A client connects to Blit's TCP API, discovers hardware, creates or
observes terminal sessions, assigns surfaces to logical panes, and receives
state, diagnostics, input, and frame events. Blit presents the result through
SDL3/KMS and can expose bounded Kitty/Sixel-compatible graphics alongside
terminal cells.

The canonical boundaries are deliberately simple:

```text
OpenAPI/HTTP + WebSocket
          |
          v
  Blit resource/lifecycle state  <---- physical input
          |
          +---- PTY + terminal state <---- Hibr desktop/session
          |
          +---- logical topology and panes
          |
          +---- SDL3/KMS presentation
```

The OpenAPI document defines wire behavior. The C implementation defines the
runtime. ADRs explain durable decisions. Tests and the verification ledger are
the evidence for claims. No generated client, shell wrapper, screenshot, or
package is an alternate source of behavior.

## Who should read what

- **API client author:** start with [`docs/api.md`](docs/api.md), then use the
  OpenAPI schema for exact request and response types.
- **Hibr integrator:** read [`docs/integration-hibr.md`](docs/integration-hibr.md)
  and the Hibr lifecycle ADRs before starting a desktop or `--join` session.
- **Blit contributor:** read the [engineering constitution](docs/engineering-constitution.md)
  and [development workflow](docs/development-workflow.md) before editing.
- **Release/operator:** read [gateway testing](docs/testing-gateway.md) and the
  [verification ledger](docs/verification-status.md) before deployment.

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

The first three responses answer different questions: `/v1/version` identifies
the running binary and API contract, `/v1/server` describes server capabilities,
and `/v1/displays` describes currently discovered outputs. A successful HTTP
response alone does not mean a display is ready; inspect its state and runtime
diagnostics before creating a workload.

When authentication is configured, add `Authorization: Bearer TOKEN` to API
requests. Never put credentials in source, logs, screenshots, or commits.

## Build and verify

```sh
tools/build-tcc.sh build/blit/blit-server server
ruby tools/tests/terminal_conformance.rb
ruby tools/tests/hibr_singleton_gate.rb
tools/tests/package_artifact_gate.sh blit_0.1.399-1_amd64.deb
```

The package is a derived release artifact. Build it only after the source,
schema, tests, documentation, and verification entry agree; the package gate
checks its embedded version, manifest hash, runtime binary, schema, and
documentation payload.

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

The current release ledger is intentionally conservative: QEMU/VKMS proves
repeatable virtual topology and lifecycle behavior, while physical monitor
count, physical evdev behavior, physical high-refresh scanout, and full Hibr
geometry remain host-specific acceptance work. See
[`docs/verification-status.md`](docs/verification-status.md) before describing
any result as hardware validated.
