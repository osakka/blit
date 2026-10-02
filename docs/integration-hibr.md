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
SDL3/KMSDRM outputs
```

## Compatibility probe

The existing hibr desktop can already run inside a blit PTY. Build hibr with
TCC, then provide a wrapper executable whose complete command is the desktop
session:

```sh
#!/bin/sh
export HIBR_MODPATH=/opt/hibr/build/mods
exec /opt/hibr/build/hibr /opt/hibr/examples/desktop/session.hibr --session blit
```

Create it through the API with a surface sized to the PTY:

```sh
curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/opt/bin/hibr-desktop","display_id":"display-1","surface":{"width":120,"height":40}}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

The probe has been run successfully with the hibr desktop built by TCC. This
mode is useful for end-to-end testing, but it depends on blit's ANSI terminal
parser and therefore is not the final integration boundary.

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
4. Hibr starts one `desktop --join` client per auxiliary pane; each joins the
   primary held desktop session through the Blit API.
5. Blit reports authoritative cell dimensions and revisions for every pane.
6. Hibr handles disconnect, reconnect, and process recovery while Blit keeps
   the pane topology stable.

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

This proves the Blit transport and Hibr shared-session model. The remaining
integration work is to make the primary desktop's orchestration layer discover
the pane inventory and invoke each `desktop --join` lifecycle automatically.

## Current blocker

The standalone PTY route is operational. The remaining integration work is the
`console-blit` module and its ABI-tested mapping to hibr's console provider;
expanding the ANSI parser is a compatibility fallback, not a replacement for
that adapter.
