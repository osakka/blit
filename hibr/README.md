# console-blit

`console-blit` is an optional Hibr `display` provider for a Blit-owned terminal
surface. It is a transport adapter for Hibr programs that explicitly load it:
Blit remains authoritative for surface
cells, revisions, topology, pane geometry, input routing, and presentation.

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
colour/attribute conversion), streamed keyboard/mouse path, and named-pane
rectangle lookup are implemented against Blit's revisioned surface,
topology, and WebSocket APIs. The module does not silently fall back to Hibr's local console
provider. Blit enables the input subscription on upgrade, so the adapter does
not send a redundant subscription control frame.

The module also supplies Hibr's `console` command facade, including screen
operations and named application panes, so the stock Hibr desktop session can
run when that program explicitly selects the provider. The normal held Hibr
desktop path (`desktop` and `desktop --join`) does not load this module; it
uses Blit's terminal-session API directly.

The `link` callback is intentionally a no-op until Blit's resource-link
contract is defined; it cannot create a second ownership model for surfaces.
