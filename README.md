# Blit

**A display server for text.** Blit drives your monitors directly through
the Linux kernel, with no X11 or Wayland, and fills them with fast,
good-looking terminals: truecolour, colour emoji, Nerd Font and icon
glyphs, Kitty and Sixel images, CRT effects. Everything is controlled
through an HTTP and WebSocket API.

Blit is the display under the [Hibr](https://github.com/osakka/hibr) text
desktop, but it does not need Hibr: any program can run in a Blit session.

Current release: see [`packaging/VERSION`](packaging/VERSION).
Repository: [github.com/osakka/blit](https://github.com/osakka/blit).

## What it does

- **Monitors:** several at once, each at its own refresh rate, each split
  into a grid of up to 8×8 panes. Monitors can be plugged in while running.
- **Fast:** frames are composed on the GPU (OpenGL ES), and only what
  changed is redrawn. Choose the CPU in the Control Panel if you prefer;
  if the GPU can't be used, the CPU takes over by itself.
- **Terminals:** xterm-compatible with the Kitty keyboard protocol, mouse
  reporting, bracketed paste, synchronized output, hyperlinks and pointer
  lock for games. See the [terminal reference](docs/terminal.md).
- **Images:** the Kitty graphics protocol (including in-place edits) and
  Sixel. Hibr's wallpaper and video play through it.
- **Fonts:** Hack, JetBrains Mono NL, Fira Code, IBM Plex Mono, IBM 3270,
  Terminus and Spleen, any size from 8 to 64 pixels, changed live. Drop
  in your own monospaced font. Under every font sits one glyph stack: Nerd
  Fonts, colour emoji, Arabic, Unifont, and Blit's own colour icons.
- **Input:** keyboards, mice, touchpads and gamepads, found as they are
  plugged in, each configurable: repeat rate, remapping, pointer speed,
  natural scrolling, a choice of pointer looks.
- **Effects:** rounded corners, scanlines, screen curvature, glow, RGB
  shift and an old CRT's hum bar, per pane.
- **Sound and power:** sound cards' volume and mixer, backlight brightness.
- **Safe to run:** a bearer token on every request, an allow-list of
  commands, an unprivileged mode, updates through a separate checked
  helper, and Ctrl+Alt+Backspace to stop it.

The full list, with when each arrived and how it has been proved, is in
[features](docs/features.md).

![Blit's text fonts, icons, file icons, Nerd glyphs and colour emoji, drawn by its own raster](docs/images/blit-fonts.png)

## Install

Blit is published in Hibr's apt repository (amd64; it needs HarfBuzz 6.0 or
later, so Debian 12, Ubuntu 24.04 or newer):

```sh
curl -fsSL https://osakka.github.io/hibr-apt/hibr.gpg \
  | sudo tee /usr/share/keyrings/hibr.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/hibr.gpg] https://osakka.github.io/hibr-apt stable main" \
  | sudo tee /etc/apt/sources.list.d/hibr.list
sudo apt update
sudo apt install blit hibr
```

The package installs and starts `blit.service`, generates an API token in
`/etc/blit/blit.env`, and shows Hibr's login screen on the first monitor.
`apt upgrade` keeps it current.

## First steps

```sh
man blit                                   # the server, its settings and files
blit --version                             # the installed Blit
TOKEN=$(sudo sed -n 's/^BLIT_AUTH_TOKEN=//p' /etc/blit/blit.env)
curl -s http://127.0.0.1:8080/v1/version    # the running Blit
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8080/v1/displays
```

Start a shell as yourself in a session:

```sh
curl -s -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d "{\"command\":\"/bin/bash\",\"arguments\":[\"-l\"],\"user\":\"$USER\"}" \
  http://127.0.0.1:8080/v1/terminal-sessions
```

From the Hibr desktop, everything is in the Control Panel: the **Blit**
pane (fonts, panes per monitor, the compositor, the login screen, Restart
Blit, the Ctrl+Alt+Backspace switch), **Blit Mouse** and **Blit Effects**.
The [pane guide](https://github.com/osakka/hibr-apps/blob/main/docs/blit.md)
explains each control.

## Configure

- **Startup values** go in `/etc/blit/blit.env`
  ([`blit.env(5)`](man/blit.env.5)), then `sudo systemctl restart blit`.
- **Everything else** is saved through the API in one settings document
  (`/v1/settings`) and replaces the startup value. The Control Panel uses
  the same API.
- **Restart** without a reboot: `sudo systemctl restart blit`, or Restart
  Blit in the Control Panel.
- **Recover** from a bad setting: stop the service and start once with
  `BLIT_SETTINGS=ignore` (see `man blit`).

## Documentation

| If you want to… | Read |
| --- | --- |
| See everything Blit can do | [Features](docs/features.md) |
| Run and configure it | `man blit`, `man blit.env`, [operator runbook](docs/api.md#operator-runbook-prove-the-server-before-touching-a-desktop) |
| Write a client | [API guide](docs/api.md), then the [OpenAPI document](openapi/blit-0.1.yaml) |
| Know which escape sequences work | [Terminal reference](docs/terminal.md) |
| Use glyphs and icons from an application | [Font and glyph reference](docs/font-reference.md) |
| Integrate a desktop | [Hibr integration](docs/integration-hibr.md) |
| Understand how it is built | [Architecture](docs/architecture.md), [decision records](docs/adr/README.md) |
| Contribute | [Development workflow](docs/development-workflow.md), [engineering constitution](docs/engineering-constitution.md) |
| Know what is next | [Roadmap](docs/roadmap.md) |
| Check what has been proved | [Verification ledger](docs/verification-status.md) |

All of it: [docs/README.md](docs/README.md).

## Build from source

Blit is C, built with TCC. It needs FreeType (≥ 2.12.1) headers and
library, HarfBuzz (≥ 6.0.0, headers pinned in `vendor/harfbuzz`), libdrm
and zlib; Mesa's EGL/GLES/GBM are loaded at run time.

```sh
tools/build-tcc.sh                                      # build/blit/blit-server
tools/build-tcc.sh build/blit/blit-helper helper
tools/run-gates.sh all                                  # the test suites
packaging/build-deb.sh build/pkg                        # the .deb
```

Use `BLIT_FREETYPE_INCLUDE=/usr/include/freetype2` to build against system
headers. Suites and their requirements are listed by `tools/run-gates.sh
--help`; the [development workflow](docs/development-workflow.md) has the
release procedure.

## How it fits together

```text
 API clients, Hibr's Control Panel          keyboards, mice, touchpads
          │  HTTP + WebSocket                          │ evdev
          ▼                                            ▼
 ┌──────────────────────────── blit ─────────────────────────────┐
 │ settings · sessions · panes per monitor · input routing        │
 │ terminal emulator (PTY) · Kitty/Sixel images · glyph stack     │
 │ compositor: GPU (OpenGL ES) or CPU · effects                   │
 └────────────────────────────┬───────────────────────────────────┘
                              │ DRM/KMS
                              ▼
                          monitors
```

Blit owns the screens, input, terminals and the API. Hibr, or any other
program, runs inside sessions and owns its own desktop. Blit never edits
Hibr; it starts Hibr's installed commands and talks to them through the
API.
