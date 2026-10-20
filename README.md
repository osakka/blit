# Blit

> API-driven, text-first display service for SDL3/KMS Linux hosts.

Blit is the display and terminal boundary; Hibr remains the desktop/session
owner. The normal integration is one Blit service, one primary Hibr desktop,
and zero or more Hibr `desktop --join` attachments placed by Blit's logical
topology API. Blit does not patch Hibr, launch competing desktop owners, or
require an X11/Wayland compositor.

The project is written and built with TCC at the Blit boundary. Hibr is an
independent runtime dependency: Blit launches and observes the installed Hibr
desktop/session commands, but never edits the Hibr source tree.

## The short version

Presentation configuration has one owner: saved API settings. The environment
supplies startup fallbacks, not silent overrides. Read
`/v1/settings` → `effective.presentation_configuration` to see the source,
validity and actual backend/driver. Use `kms` for native DRM/KMS or `sdl`
for SDL; `kmsdrm` is an SDL video-driver value, not a Blit backend selector.
See [the configuration ownership guide](docs/api.md#presentation-configuration-ownership-01563).

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

## What Blit owns—and what it does not

Blit owns the network contract, terminal state, logical display topology,
input routing, diagnostics, and presentation. Hibr remains the desktop/session
owner. The intended deployment has one primary Hibr desktop and zero or more
`desktop --join` attachments orchestrated through Blit's API; Blit does not
patch Hibr or start competing desktop owners.

Keep these evidence boundaries separate:

| Boundary | What proves it | What it does not prove |
| --- | --- | --- |
| API/state | versions, resources, revisions, routes, diagnostics | pixels reached scanout |
| Renderer/presentation | cadence counters, missed deadlines, screenshots | Hibr supplied the expected scene |
| Host acceptance | the exact binary on named hardware | behavior on another GPU, monitor, or input device |

The [verification ledger](docs/verification-status.md) is the single place
where these claims are separated.

Pointer appearance is API-owned through the settings document: three shared
antialiased shapes, four palettes,24–64px size and shadow, independent of font
scale. The catalogue's **Blit Mouse** pane exposes appearance and per-device
controls alongside **Blit**, using one connection and settings draft. See the
[API guide](docs/api.md) and [ADR0133](docs/adr/0133-shared-api-owned-pointer-appearance.md)
for the0.1.550 contract and the verification ledger for deployment status.

Acceptance is bounded to the recorded tests and host observations. Read the
[current state and restart point](docs/current-state.md) before resuming work:
it records the user's accepted one-output split, native attachment evidence,
remaining hardware coverage, and current implementation gaps. Do not infer a
correct UI from healthy timing counters or a successful API response.

## Five-minute operator path

The package installs a systemd service and the native `blit` executable. The
API is plain HTTP over TCP; the default development port is 8080. Start with
identity and readiness, then inspect hardware before creating a session:

```sh
curl --fail http://127.0.0.1:8080/v1/version
curl --fail http://127.0.0.1:8080/v1/server
curl --fail http://127.0.0.1:8080/v1/displays
curl --fail http://127.0.0.1:8080/v1/diagnostics/runtime
```

For a real deployment, configure a bearer token and restrict the listener at
the network boundary. An unauthenticated `0.0.0.0` listener is a test-LAN
configuration only. The complete client transaction, Hibr ownership rules,
stream protocol, error model, and shutdown/update behavior are in the
[API guide](docs/api.md).

The smallest useful acceptance loop is:

```text
version -> server -> displays/topology -> create one session
        -> observe revisions/runtime -> delete session -> verify zero resources
```

A client must preserve opaque IDs and revisions returned by Blit; it must not
derive resource IDs from display names or assume that an HTTP success means a
frame has already been presented.

The OpenAPI document defines wire behavior. The C implementation defines the
runtime. ADRs explain durable decisions. Tests and the verification ledger are
the evidence for claims. No generated client, shell wrapper, screenshot, or
package is an alternate source of behavior.

## Hibr Control Panel

The catalogue pane is published separately from the Blit Debian package:

```sh
catalogue install control-panel/blit
```

Open **Control Panel → Hardware → Blit**. Set the server URL and
API bearer token in the pane. Pane0.1.3 remembers the URL through Hibr's
normal settings and the token in a private0600 JSON file under
`$XDG_CONFIG_HOME/hibr/blit` (default `~/.config/hibr/blit`). The token is
URL-bound, masked and excluded from exports; this is filesystem-protected
plaintext, not encryption. The privileged updater token is separate and
never requested. Use Blit0.1.546 or newer (independent grids were introduced
in0.1.545;0.1.546 makes legacy migration durable). The pane requires
Hibr0.99.244. Catalogue availability depends on the configured mirror.

Stage auxiliary pane selections, use **Apply auxiliaries now** for the running
policy, and **Save as Default** for restart-persistent intent. Login placement
preferences are startup-applied. Font controls use the live font API; Hibr's
Displays pane owns arrangement after an auxiliary's first confirmed join.
Control Panel0.1.7 selects font cell height8–64px, with one-pixel keyboard
steps. The Font selector offers existing Terminus, pixel-art Spleen retro,
and native antialiased `blit-modern` with colour emoji in0.1.564. Candidate562
was held; config-only563 does not include the new profiles. Font changes
save through Blit's API. Native horizontal sliders also select each monitor's independent
columns/rows (1–8 each). **Save as Default** applies/saves the grid; new
unconfigured monitors start1x1. Blit refuses removing occupied panes instead
of killing their terminals. See [the API layout contract](docs/api.md#displays-topology-and-fonts).
The [pane guide](https://git.home.arpa/itdlabs/hibr-apps/src/branch/main/docs/blit.md)
documents connection, conflict handling, import/export and diagnostics. The
canonical pane lives only in hibr-apps, not in Hibr core or this package.

Offline candidate0.1.577 includes smooth `blit-retro`, full pinned Nerd
coverage in both outline profiles (10,386 codepoints) and Noto2D2.057 colour
emoji. Existing bitmap profiles stay selectable. Generate an actual-raster
specimen without a display using `ruby tools/fonts/preview.rb`.
The [font guide](assets/fonts/README.md) records API selection, provenance and
limits. Independent font shaping passes all5,244 pinned Emoji18.0 entries;
the shared bounded cluster raster now shapes and draws the full corpus in
offline16px module tests. Terminal segmentation, presentation policy and
end-to-end conformance remain [issue110](https://git.home.arpa/itdlabs/blit/issues/110).
Owned clusters now reach native KMS and both SDL compositors, with full-key
atlas caching and representative pixel checks; terminal grouping remains pending.
HarfBuzz>=6.0.0 is an explicit runtime dependency, not a second renderer.
Native KMS now reads the canonical sixteen-slot attachment layout correctly
([113](https://git.home.arpa/itdlabs/blit/issues/113)). Not deployed. The active queue is
[fonts → browser sizing → SDL parity → performance/correctness → release](docs/roadmap.md).
See [the font completion contract](docs/font-evolution-plan.md) for remaining
work; shaping coverage alone does not mean complete terminal emoji support.

## Who should read what

- **API client author:** start with [`docs/api.md`](docs/api.md), then use the
  OpenAPI schema for exact request and response types.
- **Hibr integrator:** read [`docs/integration-hibr.md`](docs/integration-hibr.md)
  and the Hibr lifecycle ADRs before starting a desktop or `--join` session.
- **Blit contributor:** read the [engineering constitution](docs/engineering-constitution.md)
  and [development workflow](docs/development-workflow.md) before editing.
- **Release/operator:** read [gateway testing](docs/testing-gateway.md) and the
  [verification ledger](docs/verification-status.md) before deployment.
- **Contributor/reviewer:** read the [repository hygiene standard](docs/repository-hygiene.md)
  before staging or pushing a change.

## What exists today

- TCP HTTP API at port 8080, with a versioned `/v1` resource namespace.
- OpenAPI 3.1 source of truth in [`openapi/blit-0.1.yaml`](openapi/blit-0.1.yaml).
- WebSocket event and bounded pixel-stream upgrade at `/v1/stream`.
- PTY-backed terminal sessions with revisioned cell surfaces and scrollback.
- A single supervised Hibr login manager with root-owned KMS/input and
  per-user desktop or shell children.
- Direct KMS and SDL3 presentation paths, refresh-aware scheduling, and
  diagnostics for frames, render time, missed deadlines, input, and streams.
- Direct-KMS live output reconciliation with a background connector observer,
  stable output lifetimes, API-visible hotplug errors and zero-output recovery.
  SDL's KMSDRM path still requires startup-connected monitors; see the
  [API's backend-specific hotplug contract](docs/api.md#live-auxiliary-displays-01543).
- Logical multi-display topology and pane layouts, including Hibr primary plus
  `desktop --join` ownership rules.
- API-selectable font profiles and logical scale, Kitty-first graphics, and a
  bounded Sixel capability.
- Saved auxiliary defaults and a live Apply API with authenticated Hibr
  standby startup, native placement confirmation and observable progress.
- A catalogue **Blit** Control Panel in
  [hibr-apps](https://git.home.arpa/itdlabs/hibr-apps), with separate Apply/Save,
  fonts, login preferences, configuration import/export and diagnostics.
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

The server requires FreeType headers, libfreetype6>=2.12.1 and the
OS-maintained libharfbuzz.so.0>=6.0.0. HarfBuzz public headers are pinned in
`vendor/harfbuzz`; no private runtime is bundled. Install your
distribution's `libfreetype-dev` or stage the reviewed Debian development
package headers at `build/deps/prefix/include/freetype2`. Set
`BLIT_FREETYPE_INCLUDE=/usr/include/freetype2` when using system headers;
`BLIT_FREETYPE_LIBRARY` can override the default amd64 soname library path.
This links the distro runtime library; no private FreeType runtime is bundled.
Pinned font assets and their licenses are checked in under `assets/fonts`.

```sh
tools/build-tcc.sh build/blit/blit-server server
ruby tools/tests/hibr_singleton_gate.rb
tools/tests/api_schema_gate.rb
tools/tests/restart_gate.sh
tools/tests/package_artifact_gate.sh /path/to/blit_<version>-1_amd64.deb
```

The conformance runner deliberately targets a caller-owned server so the same
test can validate a local dummy backend or a controlled remote fixture. Start
the server in one terminal, then run the gate in another:

```sh
BLIT_PSF_FONT=build/blit-desktop-current/fonts/Lat15-Terminus16.psf \
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=build/sdl3-kms \
  build/blit/blit-server 19195

BLIT_TEST_SERVER=http://127.0.0.1:19195 \
BLIT_EXPECTED_VERSION="$(sed -n '1p' packaging/VERSION)" \
  ruby tools/tests/terminal_conformance.rb
```

The bounded soak uses the same ownership rule and additionally requires the
server PID so it can enforce the RSS ceiling. See
[`docs/implementation-and-verification.md`](docs/implementation-and-verification.md)
for the exact two-process invocation and cleanup contract. Gates that own
their dummy server, such as the frame-rate gate, state that explicitly.

The package is a derived release artifact. Build it only after the source,
schema, tests, documentation, and verification entry agree; the package gate
checks its embedded version, manifest hash, runtime binary, schema, and
documentation payload.

The exact test matrix and QEMU commands are in
[`docs/implementation-and-verification.md`](docs/implementation-and-verification.md).
The development workflow requires one source-of-truth change, a focused gate,
an evidence update, `git diff --check`, and a deliberate commit/push for each
cycle.

## API contract at a glance

The API is intentionally discoverable and revisioned. A client should treat
the following sequence as a transaction boundary:

```text
version -> capabilities -> outputs/topology -> session -> surface/input
        -> stream/diagnostics -> cleanup -> zero-resource verification
```

On a packaged Hibr host, `/v1/login` is the lifecycle boundary for the
authenticated desktop or shell: logout retires the user session and returns
to a fresh login screen.

`openapi/blit-0.1.yaml` is the machine-readable contract; `docs/api.md` is
the operator and client guide. The schema gate checks that the documented
terminal-input route remains `POST /v1/terminal-sessions/{id}/input`, that
the parent session route remains lifecycle-only, and that the public path
count stays aligned with the release evidence. This prevents a client from
silently depending on a route that the server does not implement.

## Documentation map

- [Vision and scope](docs/vision.md)
- [Architecture](docs/architecture.md)
- [API guide](docs/api.md)
- [Hibr integration](docs/integration-hibr.md)
- [Terminal engine](docs/terminal-engine.md)
- [Testing and evidence](docs/implementation-and-verification.md)
- [Development workflow](docs/development-workflow.md)
- [Repository hygiene and handoff standard](docs/repository-hygiene.md)
- [Roadmap](docs/roadmap.md)
- [ADR index](docs/adr/README.md)
- [Verification status](docs/verification-status.md)
- [Current state and restart point](docs/current-state.md)

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
