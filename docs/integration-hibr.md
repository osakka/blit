# hibr integration

## Decision

blit remains the standalone display server. hibr integration is a thin
loadable hibr module that implements the existing `console`/`display` boundary
by using blit's OpenAPI and WebSocket contracts.

The SDL3 state engine must not be linked into hibr, and hibr must not become a
second owner of display state. This preserves process isolation and lets other
clients use blit without hibr.

```text
hibr desktop scripts
        |
console-blit hibr module
        |
OpenAPI + WebSocket
        |
standalone blit server
        |
direct DRM/KMS outputs (SDL3 is optional for effects)
```

## Compatibility probe

The existing Hibr desktop runs inside Blit PTYs through the ordinary Hibr
desktop command. Blit does not wrap or replace that command: it starts the
standalone server, then receives the Hibr command through the terminal-session
API. The complete Hibr desktop tree must remain installed in Hibr's own
standard locations, including `desktop.hibr`, `wm/`, `widgets/`, themes,
applications, and control-panel files.

Start the direct server binary, with no Blit shell launcher involved:

```sh
BLIT_DISPLAY_BACKEND=kms BLIT_BIND_ADDRESS=127.0.0.1 blit 8080
```

Create it through the API with a surface sized to the PTY:

```sh
curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/opt/bin/hibr-desktop","display_id":"display-1","surface":{"width":120,"height":40}}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

The held Hibr desktop uses the ordinary `console` path. Blit owns one
PTY-backed terminal session per physical surface and parses each client's
terminal stream locally. The primary and every `desktop --join` client use
`"surface_mode":"terminal"`; Hibr's hold layer is the single source of truth
and fans the desktop stream to each joined PTY. The held desktop must not
load `console-blit`: that provider writes directly to one HTTP surface and
cannot represent Hibr's joined-display coordinate space.

`console-blit` remains available for standalone, non-held Hibr programs that
explicitly need the cell-level Blit provider. It is not part of the held
desktop integration path.

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

The probe has been run successfully with the Hibr desktop built by TCC. This
mode uses the external surface boundary above; it does not depend on Blit's
ANSI terminal parser for desktop rendering.

## QEMU verification record

The repeatable VKMS QEMU fixture verifies these boundaries independently:

- the Blit provider opens a surface, submits styled cells, and advances the
  server-owned revision;
- a primary desktop and a `--join` client remain attached to separate panes on
  two outputs;
- the primary and joined Hibr surfaces receive independent PTY streams through
  Blit's terminal parser;
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
- `console flush` becomes a frame commit.
- `console key` waits on the WebSocket stream and returns decoded input.
- `console watch` subscribes to terminal and surface events.
- console mouse reports become blit input events.
- terminal resize becomes a surface/session resize mutation.
- a replay gap is repaired with the canonical surface snapshot endpoint.

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

This proves the Blit transport and Hibr shared-session model. The packaged
launcher now performs that orchestration deterministically from the
authoritative topology snapshot: it creates the primary, then one join session
for every other pane. Re-running against an existing held Hibr desktop uses
the documented `--resume` path, while auxiliary clients always use `--join`.

## Current integration status

The standalone PTY route and the ABI-level `console-blit` provider are
operational. `console-blit` owns no duplicate surface model: it forwards cell
mutations and cursor state to Blit, reads topology for named-pane rectangles,
and consumes the server's default all-subscriptions WebSocket stream for
keyboard and mouse input. Its flush path drains full-screen clears iteratively,
so the stock Hibr desktop remains alive under large redraws.

The current QEMU evidence covers the packaged Hibr desktop, primary plus
auxiliary `--join` sessions, the four logical pane layouts, and input-route
creation over TCP to a two-output VKMS fixture. Both Hibr surfaces reach
`running`, receive independent terminal streams, and advance revisions. With
the current Hibr executable and modules staged, the control client reports
both displays; a dedicated Blit test workload creates a window and validates
Hibr `move`, `resize`, and `focus` across the joined display. The fixture must
also provide the BusyBox `mkdir` applet because Hibr invokes `mkdir` as an
external command during control-socket setup. Blit does not modify Hibr. The
adapter deliberately does not send a redundant WebSocket subscription control
frame; the gateway's default subscription set is the current source of truth.
Resource links remain an explicitly deferred extension because they are not
part of the display ownership contract.
