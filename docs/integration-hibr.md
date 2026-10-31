# Hibr integration

How the Hibr desktop runs on Blit today. For glyphs and colour icons, see
the [font and glyph reference](font-reference.md); for solid window moves,
the [rectangle-copy contract](rectangle-copy.md).

## The boundary

Blit is a standalone display server. Hibr is a program that runs in Blit's
terminal sessions. Hibr's native console writes the desktop, Kitty images
and Sixel to a PTY; Blit parses that stream exactly as it would Kitty's or
xterm's ([ADR 0079](adr/0079-hibr-desktop-uses-the-native-pty-boundary.md)).
Blit owns the monitors, input, panes and the API; Hibr owns its desktop,
windows and session lifecycle.

```text
 Hibr login screen, desktop, desktop --join      Hibr's Control Panel
          │ PTY: text, Kitty, Sixel                       │ HTTP API
          │ frame socket: pictures by memfd               │
          ▼                                               ▼
 ┌───────────────────────────── blit ──────────────────────────────┐
 │ terminal emulator · panes per monitor · input · login manager    │
 └───────────────────────────────┬──────────────────────────────────┘
                                 │ DRM/KMS
                                 ▼
                             monitors
```

Blit's state is never linked into Hibr, and Hibr never becomes a second
owner of display state, so other clients can use Blit without Hibr. Blit
does not modify Hibr; it runs Hibr's installed commands through a few
Blit-owned entry scripts in `/usr/share/blit/hibr/`.

The package is built against a pinned Hibr version and ABI
(`packaging/HIBR_VERSION`, `packaging/HIBR_ABI`;
[ADR 0094](adr/0094-hibr-abi-is-the-runtime-compatibility-contract.md)).
`GET /v1/diagnostics/runtime` reports the pinned and running Hibr under
`hibr_compatibility`.

## Session environment

Every program Blit starts gets `TERM=xterm-kitty`, `COLORTERM=truecolor`,
`HIBR_GFX=kitty` and `DT_BIDI=on`. Blit stores and draws cells in display
order, so Hibr does the right-to-left ordering; Hibr's console uses Kitty
graphics. An API request may override either value. The full list of
variables Blit sets is in [`blit(8)`](../man/blit.8). Those a Hibr
maintainer meets most:

- `BLIT_SESSION_ID`, `BLIT_SURFACE_ID`, `BLIT_PANE_ID` (when the session is
  in a pane) and `BLIT_SERVER_PORT`: the session's identity.
- `BLIT_VERSION`: the server's version, the same as `GET /v1/version`'s
  `server_version`. Key Blit-only features on it: the battery and volume
  icons (ADR 0196) need 0.2.38 or later. It is not `TERM_PROGRAM`, which
  Hibr uses to name a display.
- `BLIT_FRAME_SOCKET`, `BLIT_FRAME_TOKEN`: pictures by descriptor (below).
- `BLIT_VTNR`: the virtual terminal Blit holds, for the login's seat.
- `BLIT_SESSION_TOKEN` and `BLIT_API_TOKEN`: two different credentials,
  used only by the optional adapter (below).

Kitty placement follows the Kitty model: `x y w h` crop, `X Y` offset in
the cell, up to 8 placements per image with `p`, and `a=d` with the
`a i c p q x y z` targets
([ADR 0060](adr/0060-kitty-placements-source-rectangles-and-delete-targets.md)).
Keys Blit does not implement are refused and reported in
`graphics_diagnostics.last_error`, not ignored. The
[terminal reference](terminal.md) has the full protocol.

## Login manager

With `BLIT_LOGIN_AUTOSTART=1`, which the package sets, Blit starts Hibr's
login screen on the first display
([ADR 0083](adr/0083-blit-root-owned-hibr-login-manager.md)). The command is
`/usr/bin/hibr /usr/share/blit/hibr/blit-login.hibr`. That script loads
Hibr's `sys` module, sets `LG_SEAT=seat0` and `LG_VTNR` from `BLIT_VTNR` so
the login is a local seat session, and sources Hibr's own
`login/login.hibr`. Hibr does PAM, the screen and the choice of session;
Blit keeps KMS, input and the API throughout.

Hibr runs `LG_SESSIONCMD` as the signed-in user. Blit's hook,
`blit-login-session.hibr`, starts either the user's shell or the held
desktop, `blit-session.hibr --session=desktop --resume`, with
`HIBR_SIGNEDIN=1` so a resumed desktop does not show its saver or lock
again. Logout retires the login PTY and Blit starts a fresh login screen.

Hibr clears the environment before the hook runs, so Blit learns who signed
in another way. It passes the hook a private socket descriptor
(`--blit-handoff-fd`) and a client name (`--blit-client-name`); the hook
sends the chosen session kind, and Blit checks the sender's kernel
credentials ([ADR 0125](adr/0125-authenticated-login-identity-handoff.md)).
It then finds the named desktop client in Hibr's `hold list` and
`hold clients`, as that user, and associates it with the login
([ADR 0126](adr/0126-native-login-desktop-association.md)). The session then
reports `hibr_desktop.role` as `primary`. If the handoff fails, sign-in
still works; only the automatic joins below are off.

The login screen is an API resource:

```sh
curl --fail -X POST -H 'Content-Type: application/json' -d '{}' \
  http://127.0.0.1:8080/v1/login     # start it, or move it to another pane
curl --fail http://127.0.0.1:8080/v1/login
curl --fail -X DELETE http://127.0.0.1:8080/v1/login
```

When a signed-in session exits and the login screen returns,
`GET /v1/diagnostics/runtime` has one `login_session_exit` entry with the
session, its state and exit code. A PAM refusal stays inside Hibr's login
screen and leaves no such entry.

## The desktop entry script

`/usr/share/blit/hibr/blit-session.hibr` starts a desktop or a join:

- It loads Hibr's `img`, `pty`, `term` and `hold` modules, then executes
  Hibr's installed `/usr/share/hibr/desktop/session.hibr` at its own path,
  because the session finds its sibling `desktop.hibr` from `$0`
  ([ADR 0095](adr/0095-execute-installed-hibr-session-at-its-own-path.md)).
  `BLIT_HIBR_DESKTOP_SESSION` names another session file, for test fixtures.
- It reasserts `DT_BIDI=on`, `HIBR_GFX=kitty` and `COLORTERM=truecolor` when
  they are unset, because the login boundary starts the desktop with a clean
  environment; without them Hibr falls back to half-block wallpaper
  ([ADR 0097](adr/0097-reassert-hibr-terminal-contract-after-login.md)).
- It sets `DT_TERMMS=0` unless the caller set it: Blit already paces
  redraws to the display, so Hibr's 70 ms throttle is not needed.
- For a join with `BLIT_HIBR_PANE_ROW` and `BLIT_HIBR_PANE_COL`, it runs
  `hold attach` at those coordinates and never moves the primary.

To start a desktop through the API instead of the login screen:

```sh
curl --fail -X POST -H 'Content-Type: application/json' \
  -d '{"command":"/usr/share/blit/hibr/blit-session.hibr","arguments":["--session","blit","--name","display-1"],"display_id":"display-1","surface_mode":"terminal"}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

Hibr's `desktop` defines the options; Blit does not reinterpret them.
`--session NAME` (`-s`) is the held desktop's name, the same for the
primary and every join; it is not a Blit session ID. `--join` attaches
another display to that desktop. `--name NAME` (`-n`) names the display in
Hibr's Displays panel. `--resume` reattaches an existing held session.
`--standby` waits for a later join; Blit counts it as a join, not another
primary.

## Joined displays

One Hibr desktop can show on several displays and panes. Each
`desktop --join` is a hold client of the one desktop, with its own PTY;
Blit parses each PTY into its own surface. There is one desktop, not a
copy of a framebuffer and not a second desktop owner.

- Pointer, wheel and keyboard input go to the hit-tested or focused
  client's own PTY, with no offset added; Hibr's hold client rewrites the
  coordinates itself
  ([ADR 0127](adr/0127-native-client-pty-input-and-login-owned-joins.md)).
- Writing cells to a joined surface through the API is refused with 409
  `hibr_join_surface_read_only`.
- A session's `hibr_desktop.role` is `primary`, `join` or `none`.
- `hibr_join_position` on `POST /v1/terminal-sessions` gives a join its
  first native row and column. Later moves in Hibr's Displays panel are
  Hibr's and are not reasserted.

Joins on other displays start by themselves once a login desktop is
associated. The saved `auxiliary_displays` setting names which output and
pane each should use; `GET /v1/auxiliary-displays` reports each binding's
state, and `POST /v1/auxiliary-displays/apply` changes them for the current
desktop without saving
([ADR 0128](adr/0128-live-auxiliary-apply-contract.md)). Blit starts each
join with `blit-session.hibr`, finds it with `blit-desktop-observe.hibr` and
places it once with `blit-desktop-place.hibr`, all as the signed-in user. A
join never covers the login or another client: an occupied pane is a
`conflict`. Blit does not restart a join that Hibr quit.

Blit owns pane geometry. A monitor is split into panes first, then a whole
cell grid is centred in each pane, so no row straddles a pane edge. The
topology reports each pane's `pixel_region` and `cell_grid`; those are the
only source for placement and input mapping (ADR 0075, 0076). Hibr does not
compute or rename panes, and Blit does not invent a desktop when a pane or
display disappears.

## Pictures: by descriptor here, through the terminal from elsewhere

Hibr's pictures -- video, browser pages, games, the wallpaper -- reach Blit
by one of two paths, and Hibr picks between them on its own:

- **On this machine** (Hibr 0.99.398 or later, Blit 0.3.19 or later): Hibr's
  console writes each frame to a sealed memfd and passes it over
  `BLIT_FRAME_SOCKET`, through hold where the desktop is held. Placement,
  layering and deletion stay Kitty escapes on the terminal, under the same
  image id ([ADR 0202](adr/0202-local-frames-by-descriptor.md)). The login
  screen passes the two frame variables, and only those, into the session
  it starts. The wallpaper stays a Kitty image, since it is placed rarely.
- **From anywhere else** -- Hibr reached over ssh, run on another machine,
  or on macOS: the variables are absent or the socket unreachable, so Hibr
  sends Kitty images through the terminal (one image id per picture,
  changed in place, [ADR 0200](adr/0200-kitty-images-change-in-place.md)).
  A held desktop with even one such display sends all its displays this
  way, so hold needs only one encoder.

Nothing needs configuring. To see which path is in use, read a session's
`frames.accepted` in `GET /v1/terminal-sessions`: it climbs while pictures
arrive by descriptor and stays still otherwise. With a video playing on the
owner's machine, the descriptor path cut Hibr's hold from 23% to 3% of a
core and Blit from 21% to 10% (the ADR's "Result").

## Pointer lock

Hibr asks for mode ?7701 with DECRQM, and engages it with `CSI ? 7701 h`
for a window that wants relative mouse motion, such as a Boxer game. It
gives the lock back with `CSI ? 7701 l`, from its Release Pointer shortcut,
on window focus loss, and on menus. Blit sends `CSI ? 7701 ; 2 $ y` when it
takes the lock back itself. The reports are deltas, so hold must forward
them without its per-display position offset
([ADR 0198](adr/0198-pointer-lock.md)).

## When a click does nothing

Blit sends mouse reports to a PTY only while the program there has turned
on a mouse tracking mode; with none, the wheel scrolls the history and a
click only focuses the pane. A desktop that never enables reporting cannot
be clicked, however correct Blit's `pointer_button` trace looks: the event
is published, routed and reaches the right session the whole time. Before
changing anything, find out which claim fails: Blit delivering the event,
or the program having asked for it.

## Terminal default colours

A native PTY session sets its default colours with OSC 10 and 11, or the
session request sets `BLIT_TERMINAL_FG` and `BLIT_TERMINAL_BG` in its
environment. Until it does, `terminal_defaults.background_configured` is
false and its pixels do not prove a Hibr desktop
([ADR 0109](adr/0109-hibr-terminal-default-colours-cross-the-api-boundary.md)).

## The optional console adapter

`console-blit` (`hibr/console-blit.c`, installed as
`/usr/lib/blit/hibr/console-blit.so`) is a Hibr `display` provider that
writes cells to a Blit surface through the API instead of a PTY. The
packaged desktop does not use it. `blit-session.hibr` loads it only when
`BLIT_HIBR_PROVIDER=api`, for a session created with
`surface_mode: "external"`, and points `HIBR_RC` at
`blit-runtime-console.hibr` so Hibr's child shells load it too. It sits
outside Hibr's module directory, so the stock desktop's `need display`
still picks Hibr's own console.

What it does:

- `console flush` becomes one atomic commit of only the cells that changed
  since the last acknowledged frame, so an idle screen sends nothing. A
  surface revision counts accepted writes
  ([ADR 0058](adr/0058-a-revision-counts-accepted-writes.md)), so re-sending
  unchanged cells would make it useless to other clients. A frame larger
  than one request goes as `presentation: defer` chunks and a final
  `commit`; one that fits is `immediate`.
- Input arrives on one `/v1/stream` WebSocket subscribed to `input` only.
  Stream cells are one-based, Hibr's are zero-based, and the adapter
  subtracts once. Keys use Blit's resolved UTF-8 text; F1 to F12 become
  Hibr's `f1` to `f12`; a held-button motion becomes `mouse drag <button>
  row column` ([ADR 0068](adr/0068-the-hibr-adapter-translates-input-vocabulary.md),
  [ADR 0070](adr/0070-pointer-modifiers-cross-the-hibr-boundary.md)).
- `console watch` adds an application descriptor to the same wait as the
  input stream.
- Hibr-local panes, clipping and stacking stay inside the adapter; they
  are not Blit panes.
- It registers its process at `POST /v1/terminal-sessions/{id}/hibr-owner`
  with `BLIT_SESSION_TOKEN`
  ([ADR 0051](adr/0051-fail-closed-hibr-owner-handshake.md)), and sends
  `BLIT_API_TOKEN` as `Authorization: Bearer` when the server has a token
  ([ADR 0055](adr/0055-adapter-api-authentication.md)). Without it the
  handshake gets 401 and the session is retired as `hibr_attach_unresolved`.
  An API `environment` entry overrides either variable; an empty
  `BLIT_API_TOKEN` keeps the credential from a child.
- `BLIT_ADAPTER_TRACE=1` logs its WebSocket and pixel-frame transport, for
  one probe session at a time.

Its gates are `tools/tests/adapter_input_gate.sh`,
`tools/tests/adapter_provider_gate.sh` and
`tools/tests/adapter_coalesce_gate.sh`. The native path's gates include
`tools/tests/hibr_join_viewport_gate.rb`,
`tools/tests/native_join_pty_gate.rb`,
`tools/tests/login_identity_gate.rb` and
`tools/tests/auxiliary_native_gate.rb`.
