# console-blit

`console-blit` is the Hibr `display` provider for a Blit-owned terminal
surface. It is loaded explicitly by Blit's packaged Hibr entrypoint and may
also be loaded by standalone Hibr programs. Blit remains authoritative for
surface cells, revisions, physical topology, input routing, and presentation;
Hibr remains authoritative for its desktop-local window panes.

The module binds to the PTY identity exported by Blit:

```text
BLIT_SURFACE_ID
BLIT_SERVER_PORT
BLIT_PANE_ID
```

Build it against the Hibr tree that owns the ABI:

```sh
tcc -I/path/to/hibr/include -I/path/to/hibr/mods \
  -shared -fPIC -o console-blit.so console-blit.c
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
operations and named application panes. The packaged held-desktop path loads
it explicitly, in the same Hibr process that sources the unchanged stock
desktop session. A direct `/usr/bin/desktop` launch is the deliberate native
console-provider test path, not Blit's graphics integration path.

The QEMU/integration fixture preloads this provider plus Hibr's `pty`, `term`,
and `hold` modules by staged path, then sources Hibr's unchanged stock
`examples/desktop/session.hibr`. It tests the same ownership boundary as the
packaged entrypoint without modifying Hibr.

The `link` callback is intentionally a no-op until Blit's resource-link
contract is defined; it cannot create a second ownership model for surfaces.
