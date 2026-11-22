# Blit's Hibr launchers and the console-blit adapter

This directory holds what Blit runs in Hibr: small Hibr-script launchers,
the optional `console-blit` display adapter, and probes. Blit never edits
Hibr ([Hibr integration](../docs/integration-hibr.md)).

## Launchers (installed in `/usr/share/blit/hibr`)

| File | What it is |
| --- | --- |
| `blit-login.hibr` | Blit's root login entrypoint: loads Hibr's `sys` module, passes the seat and VT, and sources Hibr's own login screen. The default login command is `/usr/bin/hibr /usr/share/blit/hibr/blit-login.hibr`. |
| `blit-login-session.hibr` | The session hook the login screen runs as the signed-in user: the identity handoff to Blit (ADR 0125), then the desktop through `blit-session.hibr`. |
| `blit-session.hibr` | The desktop entrypoint: Hibr's native console provider by default (`BLIT_HIBR_PROVIDER=terminal`), joins through `hold attach`, and the environment Blit reasserts (`DT_BIDI`, `HIBR_GFX`, `COLORTERM`). |
| `blit-runtime-console.hibr` | The `HIBR_RC` for interactive child shells. |
| `blit-desktop-observe.hibr`, `blit-desktop-place.hibr` | Native desktop observation and one guarded move, as the signed-in user (ADR 0126, ADR 0128). |

Not installed: `blit-console.hibr`, `blit-hibr-console.hibr` and
`blit-stock-session.hibr` (the adapter's integration launchers), and the
probes `blit-adapter-probe.hibr` and `blit-live-image-probe.hibr`.

## console-blit

`console-blit` is a Hibr `display` provider for a Blit-owned terminal
surface (`surface_mode: external`). The packaged desktop does not use it:
`blit-session.hibr` loads it only when `BLIT_HIBR_PROVIDER=api`, and
standalone Hibr programs may load it themselves. Blit remains authoritative
for surface cells, revisions, physical topology, input routing, and
presentation; Hibr remains authoritative for its desktop-local window panes.

The module reads the identity Blit exports into the session:

```text
BLIT_SURFACE_ID
BLIT_SERVER_PORT
BLIT_SERVER_HOST
BLIT_SESSION_ID
BLIT_SESSION_TOKEN
BLIT_API_TOKEN
```

`BLIT_API_TOKEN` is set only when the session runs as the server's own
user: a session started with `user` (another account) or a login's desktop
gets no root token (security review SR-8), so the module there talks to an
authenticated server only when its creator names the token in the
session's `environment`.

Build it against the Hibr tree that owns the ABI, the version in
`packaging/HIBR_VERSION`; the build checks the version and ABI, compiles
with TCC and links with the system linker (ADR 0238):

```sh
BLIT_HIBR_ROOT=/path/to/hibr tools/build-tcc.sh build/blit/console-blit.so adapter
```

The output path (`open`, `size`, `pen`, `put`, `fill`, `cursor`, `flush`, and
colour/attribute conversion), streamed keyboard/mouse path, and physical-pane
rectangle fallback are implemented against Blit's revisioned surface,
topology, and WebSocket APIs. Hibr-local named panes, their ownership mask,
stacking and clipping remain bounded adapter state because they are desktop
composition, not physical topology. Watched application descriptors share the
input wait so ready terminal output wakes Hibr immediately. The module does
not silently fall back to Hibr's local console provider.

The module also supplies Hibr's `console` command facade, including screen
operations and named application panes, in the same Hibr process that
sources the unchanged stock desktop session.

The QEMU/integration fixture preloads this provider plus Hibr's `pty`, `term`,
and `hold` modules by staged path, then sources Hibr's unchanged stock
desktop session (`programs/desktop/session.hibr`, installed as
`/usr/share/hibr/desktop/session.hibr`). It tests the same ownership boundary
without modifying Hibr.

The `link` callback is intentionally a no-op until Blit's resource-link
contract is defined; it cannot create a second ownership model for surfaces.
