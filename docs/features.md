# Blit features

Everything Blit does, in one place. Each row says when it arrived, which
decision record explains it (`docs/adr/NNNN-*.md`), and how far it has been
proved:

- **Used** — running on the owner's machine and seen working there.
- **Tested** — covered by the automated suites (`tools/run-gates.sh`), not
  yet confirmed on real hardware.
- **Hardware pending** — tested, and waiting for a device we do not have
  to hand (stated in the row).

The [verification ledger](verification-status.md) has the evidence for each
release. What is not here yet is in the [roadmap](roadmap.md).

## Displays

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Drives monitors directly through Linux DRM/KMS: no X11, Wayland or SDL | 0.1 (only path since 0.2.22) | 0004, 0184 | Used |
| Frames composed on the GPU with OpenGL ES (Mesa, loaded at run time); the default since 0.3.11 | 0.2.9 | 0181 | Used |
| The CPU compositor, when chosen | 0.2.9 | 0181 | Used |
| If the GPU cannot be used, the CPU composes instead, with the reason reported | 0.2.9 | 0181 | Used |
| A GPU start that crashes falls back to the CPU for the next start | 0.2.10 | 0181 | Used |
| Only changed areas are recomposed and uploaded | 0.2.11 | 0181 | Used |
| Each monitor refreshes at its own rate, fractional rates kept; nothing is drawn while idle | 0.1.533 | 0006, 0119 | Used |
| Frame timing: compose immediately, or late (just before refresh) for lower latency | 0.2.20 | 0183 | Used |
| Several monitors, each presented independently | 0.1 | 0043, 0131 | Used (two outputs) |
| Monitors plugged in or out while running are picked up | 0.1.544 | 0130 | Hardware pending (a third monitor) |
| Each monitor split into a grid of panes, 1–8 columns × 1–8 rows | 0.1.545 | 0131 | Used |
| A pane with a running program cannot be removed by a layout change | 0.1.545 | 0131 | Tested |
| Text grid centred in each pane, whole rows only | 0.1.536 | 0075, 0123 | Used |
| Display list with modes, size, refresh and connector | 0.1 | 0064 | Used |
| Backlight brightness (laptops with one panel) | 0.1.456 | 0064 | Hardware pending (a laptop panel) |
| Screenshots of what is on screen, read back from the GPU | 0.1; GPU 0.2.13 | 0105 | Used |
| Overlays placed by API clients | 0.1 | 0002 | Tested |

## Terminal

The complete list of escape sequences is in the [terminal reference](terminal.md).

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Terminal sessions started, placed, resized and stopped through the API, as any local user | 0.1 | 0002, 0010 | Used |
| xterm-compatible emulation: 256 colours and truecolour, attributes, cursor styles, scroll regions, margins | 0.1 | 0004 | Used |
| 1,000 lines of scrollback on the wheel; alternate screen | 0.1 | 0004 | Used |
| Mouse reporting: normal, button and any-motion tracking; SGR and pixel coordinates | 0.1; pixels 0.2.6 | 0029 | Used |
| Bracketed paste, focus events, synchronized output | 0.1 | 0029 | Tested |
| Kitty keyboard protocol (flags 1, 2, 8), including Super | 0.1.585 | 0059, 0169 | Tested |
| Hyperlinks, notifications, window titles, clipboard writes | 0.1 | — | Tested |
| Pointer shapes set by the program (OSC 22), or hidden | 0.2.8 | — | Tested |
| Pointer lock for games and emulators: raw mouse movement, released on focus loss | 0.2.42 | 0198 | Used |
| Rectangle copy (DECCRA), for solid window moves in Hibr | 0.1.589 | 0175 | Tested |
| Colour emoji sequences laid out as two-cell clusters (opt-in `text_layout: "emoji"`) | 0.1.578 | 0161–0163 | Tested |
| Default colours set per session | 0.1 | 0109 | Tested |
| Raw output capture of a session, for diagnosis | 0.2.31 | — | Tested |

## Images

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Kitty graphics protocol: chunked, compressed RGB/RGBA/PNG, placements, layers, deletion | 0.1 | 0005, 0056, 0060, 0084 | Used (Hibr's wallpaper and video) |
| Images scroll with their text | 0.1.491 | 0087 | Tested |
| Kitty images changed in place, redrawing only the changed rectangle | 0.3.1 | 0200 | Tested |
| Kitty success replies (`OK`), quiet levels respected | 0.3.3 | 0200 | Tested |
| Sixel images | 0.1 | 0021 | Used |
| Pixel-exact image scaling | 0.1.557 | 0139 | Used |
| Binary WebSocket frame stream for emulators and games | 0.1 | 0013, 0166 | Used |

## Fonts and glyphs

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Text fonts: Hack, IBM 3270, JetBrains Mono NL, Fira Code, IBM Plex Mono | 0.2.36 | 0195 | Tested |
| Pixel fonts: Terminus 16 and Spleen 16 | 0.1; renamed 0.2.35 | 0145, 0194 | Used |
| Add your own monospaced TTF/OTF to `/usr/local/lib/blit/fonts/text`: it appears without a restart | 0.2.36 | 0195 | Tested |
| Font size by cell height, 8–64 px, changed live | 0.1.561 | 0144 | Used |
| One glyph stack under every font: Blit icons, Nerd Fonts symbols (10,386), Noto symbols and maths, colour emoji, Arabic, Unifont | 0.2.36 | 0195 | Used |
| Pixel fonts borrow missing glyphs from the stack | 0.2.43 | 0199 | Tested |
| Box drawing, blocks, Braille and Powerline drawn to fit the cell exactly | 0.2.32–0.2.36 | 0164, 0191 | Tested |
| Colour emoji (Noto Color Emoji, Emoji 18.0) | 0.1.574 | 0157–0160 | Used |
| Arabic with shaping, positioned marks and Arabic-Indic digits | 0.1.588 | 0174 | Tested |
| Blit's own colour icons: Wi-Fi, battery, volume, power, pen, telephones, the hui badge | 0.1.588–0.2.45 | 0172, 0196, 0197 | Used |
| File-manager icons: 63 kinds, folders in four colourways | 0.3.4–0.3.9 | 0201 | Used |
| Icon catalogue for applications (`GET /v1/fonts/icons`) | 0.1.588 | 0172 | Used |

The [font reference](font-reference.md) lists every glyph and icon codepoint.

## Input

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Keyboards, mice and touchpads found automatically, including when plugged in | 0.1.239 | 0011, 0064 | Used |
| Which device types into which session, chosen per device | 0.1 | 0022 | Used |
| Keyboard repeat rate and delay, Caps/Num Lock at start, key remapping, layout per device | 0.1.453 | 0067 | Used |
| Pointer speed, acceleration, natural scrolling, left-handed buttons, scroll step | 0.1.454 | 0067 | Used |
| Middle-button emulation | 0.1.492 | 0088 | Tested |
| Touchpad: move, two-finger scroll, click | 0.2.2 | — | Used |
| Touchpad: tap to click, tap and drag, ignored while typing | 0.2.3, 0.2.34 | 0193 | Hardware pending (the HP Chromebook) |
| Pointer appearance: three shapes, four palettes, 24–64 px, shadow | 0.1.550 | 0133 | Used |
| Gamepads: discovery, dead zone, inverted axes | 0.1.454 | 0013 | Hardware pending (a gamepad) |
| Ctrl+Alt+Fn switches virtual console; the console is restored if Blit stops | 0.1.587 | 0170 | Used |
| Ctrl+Alt+Backspace stops Blit; can be changed or turned off | 0.1.461 | — | Used |
| Input injected through the API, including relative pointer movement | 0.1 | 0028 | Used |

## Sound and power

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Sound devices listed, default playback and capture device chosen | 0.1.451 | 0064 | Used |
| Each card's volume, mute and mixer controls | 0.1.455 | — | Used |
| Default sound device passed to every session (`ALSA_CARD` and friends) | 0.1.451 | — | Used |

## Effects

Per pane, saved with the settings, changed live from the Blit Effects pane.

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Rounded corners, scanlines | 0.2.27 | 0186 | Tested |
| Screen curvature and glow (GPU only) | 0.2.28 | 0187 | Used |
| RGB shift | 0.2.30 | 0187 | Tested |
| Hum bar (animated only while on) | 0.2.31 | 0188 | Used |
| Flicker, horizontal sync and jitter | 0.2.32 | 0190 | Tested |
| Master switch and per-pane switch | 0.2.28 | 0187 | Tested |
| The pointer still lands on the right cell through curvature | 0.2.28 | 0187 | Tested |

## Hibr desktop

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Hibr's login screen on a chosen pane | 0.1.485 | 0083, 0124 | Used |
| The login is a local seat session (`LG_SEAT`/`LG_VTNR`) | 0.2.7 | — | Tested |
| Signing in hands the session to the user, with their identity | 0.1.538 | 0125 | Used |
| One Hibr desktop, joined by other displays as independent viewports | 0.1.540 | 0127 | Used |
| Other displays started and placed from saved settings | 0.1.542 | 0128 | Used |
| Control Panel panes **Blit**, **Blit Mouse** and **Blit Effects** (shipped with Hibr's apps) | pane 0.1.0–0.1.19 | — | Used |
| Restart Blit and the Ctrl+Alt+Backspace switch from the Control Panel | pane 0.1.19 | — | Used |

[Hibr integration](integration-hibr.md) explains the contract.

## API

HTTP and WebSocket, described in full by the
[OpenAPI document](../openapi/blit-0.1.yaml) (49 paths) and the
[API guide](api.md).

| Group | Endpoints |
| --- | --- |
| Identity | `/v1/version`, `/v1/server` |
| Hardware | `/v1/graphics`, `/v1/displays`, `/v1/audio`, `/v1/audio/cards` |
| Layout and fonts | `/v1/topology`, `/v1/fonts`, `/v1/fonts/icons` |
| Settings | `/v1/settings` — one saved document, protected by revision |
| Sessions | `/v1/terminal-sessions` and their `input`, `graphics`, `output-capture` |
| Surfaces | `/v1/surfaces/{id}/cells`, `summary`, `scrollback`; `/v1/frames` |
| Input | `/v1/input/devices`, `/v1/input/routes`, `/v1/input/inject` |
| Hibr | `/v1/login`, `/v1/auxiliary-displays` |
| Effects and overlays | `/v1/effects`, `/v1/overlays` |
| Diagnostics | `/v1/diagnostics` and `runtime`, `console`, `processes`, `observation`, `trace`, `screenshots` |
| Control | `/v1/control/shutdown`, `restart`, `shutdown-key`, `update` |
| Events | `/v1/stream` — WebSocket: input, output, surface and frame events, pixel frames, with replay |

Errors are `application/problem+json` with stable codes.

## Security

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Bearer token on every request; generated at install | 0.1 | 0065 | Used |
| Without a token, listens on 127.0.0.1 only, and refuses the network unless told otherwise | 0.1 | 0065 | Used |
| Separate token for installing updates | 0.1 | 0008 | Used |
| Allow-list of the commands sessions may run | 0.1 | 0065 | Tested |
| Unprivileged profile: run as user `blit` with a systemd drop-in | 0.1 | 0065 | Tested |
| Session shells do not inherit the API token | 0.1.584 | 0168 | Used |

## Updates and packaging

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Install a new package through the API; a separate root helper checks and installs it | 0.1.232 | 0008 | Used |
| Settings, devices and the shutdown chord survive updates | 0.1.451 | 0067 | Used |
| Restart in place through the API, without a reboot | 0.1.451 | — | Used |
| Debian package on Hibr's apt repository, with a release receipt per version | 0.2.2 | 0091 | Used |
| Manual pages: `blit(8)`, `blit.env(5)`, `blit-update-helper(8)` | 0.3.10 | — | Tested |

## Diagnostics

| Feature | Since | Status |
| --- | --- | --- |
| Runtime counters: frames, flips, missed refreshes, GPU time, per-output phases | 0.1; GPU time 0.2.16 | Used |
| Event log, correlated trace, observation snapshot, process view | 0.1; processes 0.2.17 | Used |
| Console watchdog: restores the screen and reports where the loop stalled | 0.1.587; report 0.2.26 | Used |
| Screenshots and session output capture | 0.1; capture 0.2.31 | Used |

## Retired

Gone on purpose, so nothing should rely on them:

- **SDL renderer** (0.2.22, ADR 0184), and with it presentation trials,
  `server.renderer`, `BLIT_RENDERER` and display rotation.
- **Global scanlines** (`PUT /v1/effects`), replaced by per-pane effects (0.2.27).
- **Built-in Retro/Modern fonts** and the console profiles (0.2.35, 0.2.36),
  and Iosevka (0.2.37).
- **Large telephone icons** U+100023–100032 (0.3.5), and the blue, green and
  amber slots of non-folder file icons (0.3.9). Retired codepoints are
  never reused.
