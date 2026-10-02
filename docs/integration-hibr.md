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

## Current blocker

The standalone PTY route is operational. The remaining integration work is the
`console-blit` module and its ABI-tested mapping to hibr's console provider;
expanding the ANSI parser is a compatibility fallback, not a replacement for
that adapter.
