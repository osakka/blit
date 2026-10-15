# hibr integration

## Decision

blit remains the standalone display server. The Hibr desktop is an external
client of Blit's OpenAPI and WebSocket contracts. The `console-blit` Hibr
module implements the existing `console`/`display` boundary for standalone
programs that explicitly request an API-owned surface. It is a
transport/provider adapter, not a desktop implementation. The packaged held
desktop uses Hibr's native console and Blit's PTY terminal parser so hold
clients receive the same stream as Kitty and xterm.

Blit starts terminal children with `DT_BIDI=on` and `HIBR_GFX=kitty` by
default. This is intentional: Blit stores and draws cells in display order,
so Hibr must perform right-to-left ordering, while Hibr's console still uses
the Kitty graphics protocol. An API session may explicitly override either
environment value when integrating a different terminal client.

Blit implements the Kitty placement model: `x y w h` crop the image, `X Y`
offset within the cell, `p` numbers up to 8 placements per image, and
`a=d` supports the `a i c p q x y z` targets
([ADR 0060](adr/0060-kitty-placements-source-rectangles-and-delete-targets.md)).
Keys it does not implement are refused and reported in
`graphics_diagnostics.last_error` rather than ignored. Hibr's emitter uses
only whole-image display and the `I`/`A` deletes, which are unchanged.

The SDL3 state engine must not be linked into hibr, and hibr must not become a
second owner of display state. This preserves process isolation and lets other
clients use blit without hibr.

```text
hibr desktop / desktop --join
        |
OpenAPI + WebSocket client boundary
        |
standalone blit server
        |
direct DRM/KMS outputs (SDL3 is optional for effects)
```

## Compatibility probe

The packaged Blit integration runs the unchanged Hibr desktop through the
package's Hibr entrypoint. That entrypoint loads Hibr's existing `img`, `pty`,
`term`, and `hold` modules in one Hibr process, then sources the stock
`session.hibr`. Hibr's native console writes the desktop's terminal and
Kitty/Sixel stream to the PTY, which Blit parses for both the primary and every
joined hold client. The complete Hibr
desktop tree must remain installed in Hibr's own standard locations, including
`desktop.hibr`, `wm/`, `widgets/`, themes, applications, and control-panel
files.

Start the direct server binary, with no Blit shell launcher involved:

```sh
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=127.0.0.1 blit 8080
```

The packaged default binds to `0.0.0.0` for network clients. Keep the explicit
loopback override above when Hibr and Blit are intentionally local, or set
`BLIT_AUTH_TOKEN` and firewall the API when it must be reachable remotely.

The Debian package's `/usr/bin/blit` is only a symlink to the native
`blit-server` binary. It is not a shell wrapper. When a client creates a
terminal session, Blit uses the command path and argument vector from the
schema directly with `execve(2)`. Hibr therefore remains the executable and
the owner of desktop/session behavior; the repository's shell probes are
limited to QEMU staging, build, and regression-test orchestration.

Create the primary desktop through the API using the packaged entrypoint and
authoritative display geometry:

```sh
curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/usr/share/blit/hibr/blit-session.hibr","arguments":["--session","blit","--name","display-1"],"display_id":"display-1","surface_mode":"terminal"}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

Use `/usr/bin/desktop` only when deliberately testing Hibr without Blit's API
and renderer. The packaged entrypoint uses the same native Hibr terminal
provider while Blit remains the presentation boundary.

The packaged entrypoint uses Hibr's native console for the primary and every
`desktop --join` client. Blit parses the primary PTY and each joined hold
client's own PTY into distinct surfaces; there is one Hibr desktop, not a
framebuffer copy and not a second provider owner. Hit-tested input from the
joined pane is delivered to the primary owner. Direct cell mutation of a
joined surface is rejected with `hibr_join_surface_read_only`
([ADR 0079](adr/0079-hibr-desktop-uses-the-native-pty-boundary.md)).

For a Blit topology, the entrypoint also receives a deterministic logical
column for each pane. The primary remains Hibr's logical slot zero and each
join attaches in the next horizontal Hibr slot, while Blit presents those
local PTY viewports in their authoritative physical panes. This keeps Hibr's
primary bar/window placement on the physical primary even for a 1x2 stack
(primary below, join above); 2x1 and 2x2 use the same projection. It does not
mirror the primary surface. Direct Hibr invocations without Blit's topology
environment retain Hibr's documented default placement
([ADR 0080](adr/0080-map-blit-panes-to-hibr-hold-union-coordinates.md)).

The package also installs `blit-runtime-console.hibr`, and the entrypoint sets
`HIBR_RC` to it when the caller has not supplied an explicit fixture or
operator RC. This is required for API-launched Hibr children under systemd:
they do not inherit the QEMU fixture's environment. The RC loads the Blit
adapter and Hibr's native `pty`, `term`, and `hold` modules only. An explicit
`HIBR_RC` always wins, so controlled fixtures retain their own paths.

`console-blit` remains available for standalone, non-held Hibr programs and
explicit `surface_mode: external` sessions that need the cell-level Blit
provider. It is not loaded by the packaged held-desktop path.

### Desktop invocation options

Hibr's `desktop` entrypoint defines the orchestration options; Blit does not
reinterpret them:

- `--session NAME` / `-s NAME` selects the held desktop session. Use the same
  value for the primary and every join; it is a Hibr hold name, not a Blit
  terminal-session ID.
- `--join` attaches an auxiliary display beside that held session. It does
  not create another desktop state owner.
- `--name NAME` / `-n NAME` gives the attached Hibr display a stable name in
  Hibr's Displays panel. The launcher uses the authoritative Blit display ID.
- `--resume` reattaches an existing held client/session; it is for recovery,
  not for creating an auxiliary display.
- `--standby` waits for a later join request and is appropriate for a display
  that should remain available without joining immediately.

An explicitly external API-owned Hibr attachment fails closedly if its bounded
startup discovery finds no token-bearing Hibr owner. Native terminal primaries
and joins do not require that adapter handshake; inspect their PTY output and
terminal diagnostics instead. In either mode, do not start another primary
against the same held name until Hibr's own lifecycle has been resolved.

For a provider-backed Hibr process, the Blit-owned adapter registers its PID
at `/v1/terminal-sessions/{id}/hibr-owner` using the per-session token that
Blit placed in the child environment. This is an internal lifecycle
handshake, not a second display owner; it only gives Blit an exact process to
resize and signal. The session response reports `owner_registered` and
`owner_pid` alongside the bounded fallback discovery counters.

The optional adapter probe has been run successfully with the Hibr desktop
built by TCC. The packaged held-desktop mode intentionally depends on Blit's
terminal parser for the native Hibr PTY boundary.

## QEMU verification record

The repeatable VKMS QEMU fixture verifies these boundaries independently:

- a native Hibr primary and its hold clients emit terminal streams that Blit
  parses into their own surfaces, while the optional provider gate separately
  proves API-owned cells;
- a primary desktop and a `--join` client remain attached to separate panes on
  two outputs;
- the four-pane acceptance mode starts one primary and three `--join` clients
  in a Blit-owned `2x2` layout and verifies four attached surfaces in one
  held desktop session;
- the primary and every joined surface are distinct PTY viewports of one Hibr
  desktop; no joined surface becomes a second desktop writer;
- a direct Hibr executable launch is required for `hold` re-exec; launching
  through the ELF loader breaks Hibr's executable self-discovery;
- the minimal Alpine fixture must provide the glibc loader and `libc.so.6` at
  their normal system paths before launching Hibr.

The fixture exposes Hibr script diagnostics in the surface instead of calling
them Blit failures. The strict-mode issue at `wm/apps.hibr:177` was fixed in
Hibr's source tree by declaring and using a named discard variable; the final
two-display run completed without that warning.

## Production adapter mapping

The hibr module should be a transport adapter only:

- `console put` becomes revisioned surface cell/style updates.
- `console flush` becomes one atomic frame commit, reduced to what the frame
  actually changed. Hibr blanks the screen and redraws it every frame whether
  or not anything moved, so the adapter keeps a shadow of the cells Blit has
  acknowledged and sends only the delta: an idle screen sends nothing. This is
  not an optimization but a contract obligation — a surface revision counts
  accepted writes rather than observed changes
  ([ADR 0058](adr/0058-a-revision-counts-accepted-writes.md)), so a client
  that re-sends unchanged cells makes the revision useless to every other
  client. A frame too large for the 8 KiB request bound is split into
  `presentation: defer` requests with only the final one `presentation:
  commit`, so Blit never presents a partial wallpaper/menu redraw between HTTP
  chunks; a frame that fits in one request is `presentation: immediate`,
  because Blit answers 409 `frame_not_open` to a commit with no preceding
  defer.
- `console key` waits on the WebSocket stream and returns decoded input. The
  stream's terminal cells are one-based while Hibr's provider coordinates are
  zero-based, so `console-blit` subtracts exactly once at this boundary. Its
  pixel fallback uses the session's authoritative `cell_pixels`, including a
  font-scale change, and reports the same values through `console cellpx`.
  Linux F1 through F12 codes become Hibr's native `f1` through `f12` names,
  including the non-contiguous F11/F12 codes. Printable keys use the stream's
  Blit-resolved UTF-8 `text`, after per-device keymap, Shift, CapsLock, and
  AltGr processing; the adapter owns no second keyboard layout. Hibr's console
  contract remains unchanged; see [ADR 0068](adr/0068-the-hibr-adapter-translates-input-vocabulary.md).
- A held pointer motion becomes Hibr's complete drag action, including the
  button: `mouse drag left row column` (or the corresponding modifier prefix).
  This field is required by Hibr's parser; omitting it shifts the row/column
  arguments and makes horizontal movement appear vertical. The adapter picks
  the lowest-numbered held button when more than one is down. The focused
  adapter gate protects this boundary; see [ADR 0070](adr/0070-pointer-modifiers-cross-the-hibr-boundary.md).
- `console watch` adds a Hibr application descriptor to the same wait as the
  input WebSocket. Readiness wakes the desktop immediately but leaves the
  descriptor for its owning application to drain; `console key -q` excludes
  watched descriptors deliberately.
- local pane ownership, clipping, stack order and hit testing follow Hibr's
  provider contract. They do not create a second set of Blit topology panes.
- console mouse reports become blit input events.
- terminal resize becomes a surface/session resize mutation.
- a replay gap is repaired with the canonical surface snapshot endpoint.

A pointer event reaching the adapter is not the same as a click reaching the
desktop, and the difference is easy to lose days to. Hibr's console delivers
mouse reports to the session only while a mouse mode is active, which the
owning application turns on by sending `console mouse <mode>`; with no mode
set the adapter correctly discards the event. A Blit-hosted desktop that never
enables reporting therefore cannot be clicked, however correct Blit's
`pointer_button` events are — and Blit's own diagnostics will show the event
published, routed, and arriving at the right session the whole time. When
investigating "the mouse does not work", establish which of the two claims is
failing before changing anything: Blit publishing the event, or the owner
having asked for it.

The OpenAPI schema remains normative for all wire messages. The module must
not duplicate layout, revision, style, cursor, or frame semantics.

## Desktop and pane orchestration

Blit internally splits a physical display into logical cell panes and assigns
opaque, server-owned pane identifiers. Each `desktop --join` invocation is an
additional display client attached to the primary desktop's held session, not
an independent desktop state owner:

```text
display-1 / primary       <- hibr desktop
display-2 / pane-1        <- hibr desktop --join
display-2 / pane-2        <- hibr desktop --join
display-2 / pane-3        <- hibr desktop --join
display-2 / pane-4        <- hibr desktop --join
```

Blit owns authoritative pane geometry, placement, and pane naming. The API
reports the available panes and their logical cell bounds. Hibr uses that
inventory to attach clients to one shared desktop session, but does not
calculate or rename panes. Hibr remains responsible for desktop lifecycle,
detachment, reconnection, and recovery when a pane or display disappears. A
missing pane must not cause Blit to invent a replacement desktop, and Blit
must not restart Hibr processes.

For a 1x2 split, row zero is the upper pane and row one is the lower pane.
The orchestration explicitly attaches the primary Hibr desktop to the lower
pane and starts desktop --join for the upper pane. Pane bounds are terminal
cell bounds, not arbitrary pixel rectangles. Blit partitions the physical
output first, then centres a complete-cell grid independently inside each
physical pane region. This prevents a menu or glyph row from straddling a
hinge when the physical half-height is not divisible by the current cell
height. The topology reports each pane's `pixel_region` and pane-local
`cell_grid`; those are the single source of truth for SDL placement and input
mapping (ADR 0075). The adapter must not derive a second origin from the font
name or display mode (ADR 0076).

The production join flow is:

1. The Blit API configures or discovers the physical-display split.
2. The Blit API starts the primary Hibr desktop on the primary pane.
3. The primary desktop queries Blit's pane inventory and discovers auxiliaries.
4. The launcher starts one Hibr `desktop --join` client per auxiliary pane;
   each joins the primary held desktop session through the Blit API. The
   launcher passes the Blit display identifier as Hibr's `--name` for
   deterministic Displays-panel identity, transports opaque pane IDs only,
   and does not calculate geometry or create a second layout model. Hibr's
   `--session` value identifies the held desktop; it is not a Blit session ID.
5. Blit reports authoritative cell dimensions and revisions for every pane.
6. Hibr handles disconnect, reconnect, and process recovery while Blit keeps
   the pane topology stable.

Every PTY child receives `BLIT_SESSION_ID`, `BLIT_SURFACE_ID`,
`BLIT_PANE_ID` (when attached to a pane), and `BLIT_SERVER_PORT`. These are
the stable binding coordinates for the ABI-level adapter; the adapter must
use them to address the server-owned surface and must not discover or invent
another surface identity.

Two further variables are credentials and are not interchangeable:

- `BLIT_SESSION_TOKEN` is the owner handshake secret from
  [ADR 0051](adr/0051-fail-closed-hibr-owner-handshake.md). It travels in the
  body of `POST /v1/terminal-sessions/{id}/hibr-owner` and answers which
  process owns the session.
- `BLIT_API_TOKEN` is the gateway bearer credential from
  [ADR 0055](adr/0055-adapter-api-authentication.md). Blit sets it only when
  `BLIT_AUTH_TOKEN` is configured, and the adapter sends it as
  `Authorization: Bearer` on its HTTP requests and on the `/v1/stream`
  upgrade. Authentication is checked before route dispatch, so without it the
  owner handshake returns 401, the owner never registers, and the session is
  retired as `hibr_attach_unresolved` — a failure that names Hibr rather than
  authentication.

An API `environment` entry overrides either value, because those entries are
applied after Blit's own. Setting `BLIT_API_TOKEN` to an empty string opts a
particular child out of receiving the credential.

## Primary/auxiliary probe

The shared-session orchestration path has been exercised with two TCC-built
Hibr processes:

1. `PUT /v1/topology` selects `{"columns":2,"rows":1}`.
2. `POST /v1/terminal-sessions` starts the primary desktop with the first
   discovered `pane_id`.
3. A second `POST /v1/terminal-sessions` starts the auxiliary desktop with
   the second discovered `pane_id`.
4. `GET /v1/topology` reports both attached surface IDs.
5. Hibr's `hold clients blit` reports one logical desktop with `primary` and
   `auxiliary` clients at adjacent logical offsets.

This proves the Blit transport and Hibr shared-session model. The orchestration
is performed by Hibr's installed `desktop` command and its `--join` path, using
the authoritative topology snapshot supplied by Blit. Blit does not ship or
invoke a shell launcher. Re-running against an existing held Hibr desktop uses
the documented `--resume` path, while auxiliary clients always use `--join`.

## Current integration status

The standalone PTY route and the ABI-level `console-blit` provider are
operational. `console-blit` owns no duplicate surface model: it forwards cell
mutations and cursor state to Blit, keeps Hibr-local pane composition separate
from physical topology, and subscribes one session-owned WebSocket to input
events only. Its flush path drains full-screen clears iteratively, so the stock
Hibr desktop remains alive under large redraws.

The current QEMU evidence covers the packaged Hibr desktop, primary plus
auxiliary `--join` sessions, the four logical pane layouts, and input-route
creation over TCP to a two-output VKMS fixture. The join-viewport gate covers
distinct PTY surfaces, ownership, and input routing; the QEMU screenshot gate
must prove the same independent viewports with real Kitty/Sixel wallpaper.
With the current
Hibr executable and modules staged, the control client reports both displays;
a dedicated Blit test workload creates a window and validates Hibr `move`,
`resize`, and `focus` across the joined display. The fixture must also provide
the BusyBox `mkdir` applet because Hibr invokes `mkdir` as an external command
during control-socket setup. Blit does not modify Hibr. The adapter sends one
masked subscription control frame naming `input` and the owning primary
terminal session. This bounds its frame reader away from large surface
messages and lets session teardown close the stream deterministically.
Resource links remain an explicitly deferred extension because they are not
part of the display ownership contract.

The adapter input vocabulary has an independent executable gate. It feeds the
real adapter WebSocket frames and proves zero-based Hibr pointer coordinates,
font-scaled pixel fallback, all three button names, wheel events, and F1
through F12 names. `adapter_provider_gate.sh` separately proves pane ownership,
clipping/stacking, watched-descriptor wakeups, UTF-8 width, and shadows against
the exact pinned Hibr release. It also proves that a steady `console resizing`
check performs no HTTP poll; SIGWINCH is the resize edge. Physical-host clicks,
shortcuts, redraws, and application wakeups are still measured after package deployment; passing
translation/provider gates are not reported as physical acceptance.

The local join-viewport gate now proves the server-side contract without
modifying Hibr: one primary and one `--join` session are created, each PTY
paints a different surface, direct writes to the joined surface are rejected
with `hibr_join_surface_read_only`, and a pointer hit in the joined pane is
traced with the primary as `input_session`. This is a Blit integration gate,
not physical-display acceptance. The live host still needs a fresh package
deployment and screenshot/input validation before we claim that the wallpaper,
context menu, and full Hibr window-placement experience work on the real
display.
