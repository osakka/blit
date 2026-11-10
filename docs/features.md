# Blit features

Everything Blit does, in one place. Each row says when it arrived, which
decision record explains it (`docs/adr/NNNN-*.md`), and how far it has been
proved:

- **Used** — running on the owner's machine and seen working there.
- **Tested** — covered by the automated suites (`tools/run-gates.sh`), not
  yet confirmed on real hardware.
- **Hardware pending** — tested, and waiting for a device we do not have
  to hand (stated in the row).
- **In development** — partly built and gated, not released or not yet
  complete (the row says what exists and what does not).

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
| Pixel zoom per monitor (100–400% in 25% steps, kept per connector): the whole display magnified, pointer and panel kept normal size | 0.3.26 | 0204 | Tested |
| Screenshots of what is on screen, read back from the GPU | 0.1; GPU 0.2.13 | 0105 | Used |
| Overlays placed by API clients | 0.1 | 0002 | Tested |

## Terminal

The complete list of escape sequences is in the [terminal reference](terminal.md).

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Terminal sessions started, placed, resized and stopped through the API, as any local user | 0.1 | 0002, 0010 | Used |
| xterm-compatible emulation: 256 colours and truecolour, attributes, cursor styles, scroll regions, margins | 0.1 | 0004 | Used |
| Terminal fixes: xterm's bright colours, underline styles, colon colours, `CSI 3 J`, margins, four character sets, background-colour erase, window size reports, DECRQM/DECRQSS/DA3 answers | 0.3.12 | — | Tested |
| 1,000 lines of scrollback on the wheel; alternate screen | 0.1 | 0004 | Used |
| Mouse reporting: normal, button and any-motion tracking; SGR and pixel coordinates | 0.1; pixels 0.2.6 | 0029 | Used |
| Bracketed paste, focus events, synchronized output | 0.1 | 0029 | Tested |
| Kitty keyboard protocol (flags 1, 2, 8), including Super | 0.1.585 | 0059, 0169 | Tested |
| Kitty keyboard flag 4, report alternate keys: `CSI key:shifted:base ; mods u`, the shifted key while Shift is held and the base-layout key (the US PC-101 key at the same physical position, from the evdev code), each left out when it equals the key; with a device keymap the key is the layout's and the base the US key | 0.3.61 | 0225 | Tested (`kitty_keyboard_gate`) |
| Focus reports on input hand-over: the focused session (mode 1004) gets `CSI O` when Blit releases physical input for a VT switch, after the synthetic key and button releases, and `CSI I` when it takes input back | 0.3.61 | 0225 | Tested through the `BLIT_TEST_VT_SIGNALS` hook (`kitty_keyboard_gate`); the real VT path only in the QEMU gate |
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
| Kitty replies: `OK`, and an error for every failure; quiet levels respected | 0.3.3; errors 0.3.12 | 0200 | Tested |
| Picture frames by descriptor: a sealed memfd over a local, token-checked socket instead of base64 through the terminal (for video and pages). Local only: a program over ssh or on another machine sends pictures through the terminal | 0.3.17; login sessions 0.3.19 | 0202 | Used (with Hibr 0.99.398) |
| Picture frames by dmabuf (frame channel version 3): a GPU producer's buffer imported by the GPU compositor as a texture, no CPU copy or upload; offered only while the GPU compositor composes every output and can import (`OK dmabuf=1`, with the formats and modifiers); RGB (`XRGB8888`, `ARGB8888`, `XBGR8888`, `ABGR8888`; the alpha formats premultiplied), an optional acquire fence waited on by the GPU, `RELEASE` once a newer frame is shown; `ENOTSUP` when any output is composed by the CPU. YUV not yet | 0.3.60 | 0223 | Tested; protocol, import, release, premultiplied alpha and a GPU-rendered frame with its fence checked on screen on an Intel HD 515 test host with a test producer (`frame_dmabuf_live_gate`); no real producer yet |
| Pictures bounded by memory, not by count: a session's pictures and placements in tables that grow, up to ceilings of 4096 pictures and 16384 placements (`ENOSPC`, the API 409); a picture budget per session (256 MiB) and across sessions (a quarter of physical memory, at most 2 GiB), settings in MiB, charged by one accounting function for CPU pixels, sixel and Kitty scratch and GPU textures; over a budget, unshown pictures freed least recently shown first, never a shown one (placed, with a held placement, or holding an input route), and nothing freed when that would not make room, else `ENOSPC`; the GPU's picture textures a cache of 512 MiB (a setting), recycled least recently drawn, so nothing on screen is uploaded again; the frame channel's input routes one per picture; `graphics.budget`, `graphics.used` and each session's `graphics_memory` (bytes, frees and refusals by reason) | 0.3.65 | 0226 | Tested (`graphics_memory_gate` on the CPU and GPU compositors: 1000 pictures with 500 placed all drawn and no re-upload, the reported bytes, a session budget and the server budget over two sessions freeing least recently shown first then `ENOSPC`, picture 4097 refused; `graphics_memory_frames_gate`: a routed picture and a held placement kept); not yet run on the owner's machine or 10.52 |
| Physical keyboard and mouse to a compositor's EIS socket (frame channel version 3, `INPUT eis <path> <image-id>`, then `INPUT on <image-id>`/`INPUT off <image-id>`, answered `OK input <image-id>` or `ERROR INPUT <image-id> <code>`): a route per image, so one connection carries several pictures' routes (a desktop multiplexes its windows over one connection per display); `EBUSY` only for the same image twice on one connection; at most one route on per session across its connections, `INPUT on` turning the others off after releasing what they held; libei loaded at run time; the socket must be the client's own (file owner and listening peer); live only while on, the session focused, the console Blit's and the image there; evdev keys press and release (no repeats), buttons, wheel notches, the absolute pointer over the image's drawn rectangle scaled to the compositor's pointer region (the frame size when it reports none), relative motion under `?7701`; every key and button released when it stops; a route ends with its image or its connection, and a compositor that refuses or drops it later ends only that route (`ERROR INPUT <image-id> ENOENT`); `frames.input` per session with `events`, `last_error` and `routes` (image, state, region). `blit-gamescope` speaks it and has `--no-place` (no Kitty placement or delete, for a desktop that places the picture) | 0.3.62; per image 0.3.64 | 0225 | Tested (`frame_input_gate`: the answers, the states, one route on per session on one connection and across two, and every input case against a recording stand-in libei on any host, including two routes on one connection, `INPUT on` releasing the other route first, a late `ENOENT` and an image deleted each ending only their route; `gamescope_bridge_gate`: the bridge's per-image answers and that `--no-place` writes no `a=p` or `a=d`); the same cases against a real EIS server (`frame_input_eis_gate`, needs libei1 and libeis1, skips without) are for the Debian 13 test host 10.52 and not yet run |
| Input route hold modes (`INPUT hold <image-id> relative|confine`, answered `OK input <image-id>` or `ERROR INPUT <image-id> EINVAL`; any time after `INPUT eis`; takes and gives back no lock): `relative`, the default, as before (under `?7701` the pointer hidden and still, raw relative motion to the route); `confine`, while the session holds `?7701`, the route is live and its picture is drawn on the pointer's output: Blit's pointer stays shown with its shape, moves as an unlocked pointer does, kept inside the picture's topmost drawn rectangle, and the route is sent that point as an absolute position through the usual mapping (no relative motion), so gamescope's cursor, which its stream does not carry, and the drawn one are one point; buttons, the wheel and the holder's `?7701` reports as under `relative`; the picture not drawn or the pointer on another output behaves as `relative`; when the lock ends the pointer stays where it was; one function (`frame_input_confined`) decides for both the pointer's visibility and its motion; each route's `hold` in `frames.input` | 0.3.70 | 0225, 0198 | Tested (`frame_input_gate`: `EINVAL` for an unknown or missing mode and for an image with no route, `OK input` and `hold` for both modes; against the recording stand-in libei, `relative` hiding a still pointer with the raw sum sent, and `confine` showing it, moving it, clamping it at both far corners, the compositor sent the same points as absolute positions and no relative motion, the holder its raw deltas, a click, the picture undrawn falling back to `relative` and drawn again confining, and after `?7701l` the pointer where it was and ordinary absolute motion); the same against a real EIS server (`frame_input_eis_gate`) and with gamescope not yet run |
| 16 images per session | 0.3.17 | 0202 | Tested |
| Sixel images, up to 8 MiB of data each | 0.1; large 0.3.12 | 0021 | Used |
| Pixel-exact image scaling | 0.1.557 | 0139 | Used |
| Binary WebSocket frame stream for emulators and games | 0.1 | 0013, 0166 | Used |

## Fonts and glyphs

| Feature | Since | ADR | Status |
| --- | --- | --- | --- |
| Text fonts: Hack, IBM 3270, JetBrains Mono NL, Fira Code, IBM Plex Mono | 0.2.36 | 0195 | Tested |
| Pixel font: Terminus 16 (Spleen 16 retired in 0.3.68 for `spleen`) | 0.1; renamed 0.2.35 | 0145, 0194, 0227 | Used |
| Spleen at any size, 8–64 px: one font of the five Spleen 2.2.0 masters (6x12, 8x16, 12x24, 16x32, 32x64), the largest whose height divides the cell height drawn pixel-exact, else the largest below it scaled; `GET /v1/fonts` reports `master` and `master_fit` | 0.3.68 | 0227 | Tested (`spleen_master_gate`: listed once with its masters, none as a font of its own; the master at 12, 16, 24, 32, 48, 64 px crisp and 20, 28, 40 px scaled; at every crisp size the text in the screenshot holds only its two colours, and has in-between coverage when scaled; a saved `blit-spleen16` falls back to the boot font; `outline_catalogue_gate`, `custom_icon_gate`: every Nerd icon and Blit glyph through the stack at 8–64 px) |
| Add your own monospaced TTF/OTF to `/usr/local/lib/blit/fonts/text`: it appears without a restart | 0.2.36 | 0195 | Tested |
| Font size by cell height, 8–64 px, changed live | 0.1.561 | 0144 | Used |
| One glyph stack under every font: Blit icons, Nerd Fonts symbols (10,386), Noto symbols and maths, colour emoji, Arabic, Unifont | 0.2.36 | 0195 | Used |
| Pixel fonts borrow missing glyphs from the stack | 0.2.43 | 0199 | Tested |
| Box drawing, blocks, Braille and Powerline drawn to fit the cell exactly | 0.2.32–0.2.36 | 0164, 0191 | Tested |
| Colour emoji (Noto Color Emoji, Emoji 18.0) | 0.1.574 | 0157–0160 | Used |
| Arabic with shaping, positioned marks and Arabic-Indic digits | 0.1.588 | 0174 | Tested |
| Blit's own colour icons: Wi-Fi, battery, volume, power, pen, telephones, the hui badge | 0.1.588–0.2.45 | 0172, 0196, 0197 | Used |
| File-manager icons: 63 kinds, folders in four colourways | 0.3.4–0.3.9 | 0201 | Used |
| Retro Mac-style window chrome glyph sets, Chalk (one colour) and Slate (the cell's own colours since 0.3.39), one per part (title bar, frame, dialog, buttons) so they mix; SGR 51 framed labels and titles | 0.3.36–0.3.39 | 0210, 0211 | Tested |
| Drop shadow under any cell (SGR 66/67), in the cell's underline colour or foreground | 0.3.39 | 0211 | Tested |
| Icon catalogue for applications (`GET /v1/fonts/icons`) | 0.1.588 | 0172 | Used |
| Window frame with its line on the outer edge: one frame set (five cells) and the title bar's end cells, Chalk and Slate, whose stripes run into them; the old frame glyphs and `stripe-first`/`-last` are deprecated in the catalogue (`deprecated`, `replaced_by`) and still drawn | 0.3.41 | 0214 | Tested |
| Pressed title-bar boxes and a grow box: close, shade and zoom pressed (also hover), and the window's resize corner, normal and pressed, Chalk and Slate (10 glyphs, U+1002BB-1002C4) | 0.3.47 | 0219 | Tested |
| Inactive title-bar end cells: four plain title-end glyphs (the outer line, top line and rule, Slate's bevel, no stripes), Chalk and Slate, for the inactive bar beside the plain `title` cell | 0.3.44 | 0214 | Tested |
| Controls glyph set: a checkbox, a compact two-cell switch and a three-cell capsule switch, in Chalk, Slate and the capsule's own pens | 0.3.40 | 0212 | Tested |
| Graphics ledger (`GET /v1/diagnostics/graphics-ledger`) and blink counters: a picture that vanishes from presented frames and returns is counted; pictures deleted inside a synchronized update stay until it ends, so a whole-output redraw shows no hole | 0.3.42 | 0213 | Tested |
| A fitted icon or symbol is never trimmed at any font size, 8 to 64 px; the Wi-Fi and volume icons are fitted by their drawn layers; a spanning icon's halves join to the pixel | 0.3.43 | 0215 | Tested |
| A catalogue a client can map: structured pen roles (part, cells, background, foreground, SGR 58) on every cell-geometry glyph; a revision, an ETag and `If-None-Match`; SGR capability flags (`sgr-framed`, `sgr-framed-bar`, `sgr-shadow`) in `GET /v1/server` | 0.3.40 | 0212 | Tested |

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
| Animated effects follow each display's refresh (`effects_animation_hz` `"auto"`, the default), or a ceiling of 10–240 Hz met by ticking every Nth vblank (50 on 60 Hz is 30), never above the display's refresh; ticks still only fill gaps between real frames | 0.3.67 | 0188 | Tested (`presentation_effects_gate`: auto at the DRM mock's 60 Hz, 30 and 10 as set, 50 as 30 and 25 as 20, 240 clamped to 60, measured rates, settings round trip and refusals, `fills_gaps_only` under auto; `tube_effects_gate`, `zoomed_effects_gate`, `burn_in_gate`: the clock at the display's rate; `api_schema_gate`: the schema); CPU and power cost of 60 against 30 Hz not yet measured on real hardware |
| Flicker, horizontal sync and jitter | 0.2.32 | 0190 | Tested |
| Every effect drawn on a zoomed display, the 100% picture enlarged | 0.3.30 | 0204 | Tested |
| Static noise, chroma with a phosphor colour, brightness and contrast | 0.3.32 | 0207 | Used (cost measured on the mini PC) |
| Burn-in: what was lit fades out over 0.16–1.6 s, frames only while a trail fades | 0.3.33 | 0208 | Used (cost measured on the mini PC) |
| CRT mask at the panel's own pixels: aperture grille, slot mask or shadow mask, brightness kept | 0.3.35 | 0209 | Tested |
| A pane says whether anything is shown in it (`has_view`), so effects set on an empty pane are explained | 0.3.34 | — | Used |
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
| Control Panel panes **Blit**, **Blit Mouse** and **Blit Effects** (shipped with Hibr's apps): zoom and effect sliders, the panel's switches, restart after update | pane 0.1.0–0.1.27 | — | Used |
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
| After an `apt upgrade`, Blit restarts itself at the login screen when nobody is signed in (or only reports it: `server.restart_after_update`) | 0.3.27 | 0205 | Used |
| The running Hibr's release and module ABI are checked again whenever Hibr is upgraded underneath Blit | 0.3.32 | 0094 | Used |
| Built against Hibr's module ABI 17 | 0.3.31 | 0094 | Used |
| Debian package on Hibr's apt repository, with a release receipt per version | 0.2.2 | 0091 | Used |
| Manual pages: `blit(8)`, `blit.env(5)`, `blit-update-helper(8)` | 0.3.10 | — | Tested |
| `blit-gamescope`, the bridge that shows games and Steam in a window through a headless gamescope, in its own package `blit-gamescope` (suggested by `blit`, not installed by default; needs gamescope from trixie-backports on Debian 13) | 0.3.66 | 0225 | Tested |
| `blit-compositor`, Blit's own Wayland compositor for programs (ADR 0228 phase 1, milestone A): a program beside blit-server, one per desktop session, that composites nothing; it says hello on the frame channel as version 4, listens on `$XDG_RUNTIME_DIR/blit-<n>`, says `WINDOW display <name>`, and turns each `xdg_toplevel` and `xdg_popup` into a picture: `WINDOW new`, `title`, `appid`, `limits`, `fullscreen`, `popup`, `decor` (server-side by `zxdg_decoration_manager_v1`) and `gone` out, the desktop's `WINDOW size` (configured exactly, `0 0` the program's choice) and `WINDOW close` in; every `wl_shm` buffer one sealed FRM1 frame, released and its frame callbacks answered when sent; `wl_compositor`, `wl_subcompositor` (accepted, not composited), `wl_shm`, `wl_output`, `wl_seat` (advertised; input is milestone B) and `xdg_wm_base`, at the ADR's versions bounded by the build host's protocol XML. blit-server answers a version 4 hello as version 3's plus ` window=0` and every `WINDOW` line `ERROR WINDOW <id> ENOTSUP`. Not yet: dmabuf frames, input and the keymap, presentation timing, packaging | 0.3.71 | 0228 | In development (`compositor_gate`: against a fake desktop with the in-repo stand-in client, the lines, frames of exactly 320x200 and 500x300 and the program's own size for `0 0`, pixel for pixel, a popup at its offset closed by `popup_done`, a killed client's window `gone`, the seat and output a client binds, close then `gone`; Blit's `window=0` and `ENOTSUP`; the same under AddressSanitizer and UBSan) |
| `blit-compositor` milestone C (ADR 0228 phase 1): `zwp_pointer_constraints_v1` asked of the desktop -- `lock_pointer`/`confine_pointer` send `WINDOW lock <id> relative|confine`, the desktop's `WINDOW lock <id> on|off` sends locked/confined and unlocked/unconfined, a constraint pending until `on`, a oneshot defunct after `off`, a persistent one waiting for the next `on`, its destroy or its window gone `WINDOW lock <id> off`, no absolute motion while locked; Blit's `INPUT member <popup> <top-level> <x> <y>` (a popup's picture in its top-level's route, the pointer over it mapped into the top-level's pixels, eis and sock, ending with its picture; `members` and `over` in `frames.input`); key repeat from Blit's keyboard settings (`X-Blit-Repeat-Rate`/`-Delay` on `GET /v1/input/keymap`, 200 and 304, sent as `wl_keyboard.repeat_info`); the keymap re-read only on Blit's `INPUT keymap <id> <revision>` notice (a keyboard configured, plugged or gone, or a key from another keyboard, before that key; bounded 250 ms read); the default keymap the keyboard whose key came last; the `blit-compositor` package (Depends `blit (= version)`, libc6, libwayland-server0, libxkbcommon0; Recommends xwayland; `blit` Suggests it; no build path, reproducible) | 0.3.72 | 0228 | In development (`compositor_gate`: the lock part with the stand-in's `--lock` against a fake desktop, and end to end with a real Blit: the pointer over a member picture reaching the popup surface, Blit's repeat, a PATCH while live reaching the client, a key from a second keyboard bringing its keymap and repeat before the key; `frame_input_gate` sock and stub: `INPUT member` answers and mapping, the notice at live; `keymap_gate`: repeat headers, the last key's keyboard; the package built twice to one sha256; not yet the owner's live check on the mini PC) |
| `blit-compositor`: programs survive a desktop restart (ADR 0228; `docs/api.md`, agreed with Hibr) -- when the frame channel closes it keeps every program, window, the Wayland and input sockets and each window's newest frame (wl_shm copied, dmabuf held, older buffers given back at once), answers frame callbacks at the output refresh, and dials the same socket and token again (50 ms doubling to 1 s, at most 60 s, never blocking) until a desktop answers `window=1`; then `WINDOW display`, every window again with its id and last state (`new`, hints, `INPUT sock`, `hide`, popups), the newest frames and `WINDOW lock`, configuring nothing until the new `WINDOW size`; no desktop back: status 1; `WINDOW quit 0`: ends at once, status 0; `--display <name>` (refused, status 2, when in use or invalid) | unreleased, next Blit | 0228 | In development (`compositor_gate` restart part against a fake desktop, also under ASan/UBSan: an shm and a dmabuf program drawing through the outage, a program started meanwhile, all announced again with their newest frames and a pending pointer lock on the same socket, then sized; quit; no desktop back after a shortened bound; `--display` refused; `compositor_wire_unit` `WINDOW quit`; not yet Hibr's side or a live restart) |
| One copy of the documentation, and FreeMono's source in its own small package `blit-font-sources` (suggested, not installed by default): the package is 17.2 MB, down from 24.6 MB | 0.3.45 | 0217 | Tested |
| The terminal answers XTGETTCAP for `sgr-framed`, `sgr-framed-bar` and `sgr-shadow` (booleans), so a client that holds only the terminal can detect Blit's SGR extensions | 0.3.46 | - | Tested |
| `fonts.hinting` (`full`, `light`, `none`; default `full`) sets the FreeType hinting of text glyphs through `PUT /v1/fonts` and the settings document; fitted icons ignore it | 0.3.47 | 0218 | Tested |
| Blit's colour icons can be drawn as a monochrome shape that says state (battery level, charging, mains, signal, volume) by shape, in black, white or the cell's foreground; 0.3.48 chose this with the global `fonts.icon_colour` setting, which 0.3.49 replaced with the per-cell icon pen | 0.3.48 | 0220, 0221 | Tested |
| The icon pen is per cell: SGR 68:v (1 auto, 2 black, 3 white), ended by SGR 69, so a menu bar draws its icons monochrome while the desktop and file manager beside it keep them in colour; a change is cell damage only (no glyph-cache rebuild, no reflow); `style.icon_pen` in the cells API, `sgr-icon-pen` in the server capabilities and XTGETTCAP, reported by DECRQSS | 0.3.49 | 0221 | Tested |
| The hui badge is a solid teal disc with white letters (its monochrome shape unchanged: the disc with the letters cut out); a new hui mark, the H-U-i monogram in teal without a disc, two columns (`hui-mark-left` + `-right`, U+1002C5-1002C6), its monochrome shape its own outline | 0.3.51 | 0197, 0220, 0221 | Tested |
| Two wiretap icons for Hibr's network monitor and firewall, a water tap in water blue (#2e8fe0), two columns each: on, with a drop under the spout (`wiretap-left` + `-right`, U+1002C7-1002C8), and off, without it (`wiretap-off-left` + `-right`, U+1002C9-1002CA); each monochrome shape is its own outline, and on and off differ by shape (`font_icon_shape_gate`) | 0.3.52 | 0197, 0220, 0221 | Tested |
| The wiretap icons in one column, a bolder tap in the same water blue: on, with the drop (`wiretap-1`, U+1002CB), and off, without it (`wiretap-1-off`, U+1002CC), from one drawing whose drop is a level (clear when off, so both states sit in one box and the tap does not move); the monochrome off shape has no drop, and on and off differ by shape (`font_icon_shape_gate`) | 0.3.53 | 0220, 0221 | Tested |
| A minimise box for the title bar, Chalk and Slate, normal and pressed (also hover): a short bar along the bottom of the box, apart from shade's bar across the middle and zoom's square (4 glyphs, U+1002CD-1002D0: `chalk-minimise`, `-pressed`, `slate-light-minimise`, `-pressed`) | 0.3.54 | 0222 | Tested |
| Wi-Fi as a fan, one column: `wifi-signal-1` to `-4` (U+1002D1-1002D4), a dot and three arcs lit in teal up to the level, the rest in the bars' inactive slate, and `wifi-off` (U+1002D5), the fan unlit with a red cross (monochrome: the cross alone); every level differs by shape (`font_icon_shape_gate`). The four bars are the mobile signal: `cellular-1` to `-4` (U+1002D6-1002D9), and `wifi-1` to `-4` are deprecated, `replaced_by` the cellular ids, still drawn | 0.3.55 | 0214, 0220 | Tested |
| Wi-Fi and volume in two columns, beside the one-column forms (unchanged): `wifi-signal-wide-1` to `-4` and `wifi-off-wide` (U+1002DA-1002E3) and `volume-wide-0` (muted) to `-3` (U+1002E4-1002EB), each `-left` + `-right`; the same drawings as `wifi-signal-N`, `wifi-off` and `volume-N`, about twice the size, every level in one place; each level differs by shape across the whole badge (`font_icon_shape_gate`) | 0.3.56 | 0197, 0220 | Tested |
| The classic desk telephone in two columns, beside the one-column form (unchanged): `phone-classic-wide-off`, `-wait`, `-on` and `-ring` (U+1002EC-1002F3), each `-left` + `-right`; the same drawings as `phone-classic-off` to `-ring`, about twice the size; each state differs by shape across the whole badge (`font_icon_shape_gate`) | 0.3.57 | 0197, 0220 | Tested |
| The battery lying on its side in two columns, beside the upright one-column battery (unchanged): `battery-wide-0` to `-4` (U+1002F4-1002FD), `battery-wide-0-charging` to `-4-charging` (U+1002FE-100307) and `battery-wide-ac` (U+100308-100309), each `-left` + `-right`; four segments left to right, the charging bolt with a dark edge (legible on light and dark bars and over the green); monochrome: the body and the lit segments, the bolt and the plug cut out; each state differs by shape across the whole badge (`font_icon_shape_gate`) | 0.3.58 | 0196, 0197, 0220 | Tested |
| A full-screen box and a sticky box for the title bar, Chalk and Slate: full screen normal and pressed (also hover), four corner brackets in the box; sticky normal, pressed and on (the window is sticky), a pin, bold when on; pressed as ADR 0219's boxes; each mark apart from close, minimise, shade and zoom (10 glyphs, U+10030A-100313: `chalk-fullscreen`, `-pressed`, `slate-light-fullscreen`, `-pressed`, `chalk-sticky`, `-pressed`, `-on`, `slate-light-sticky`, `-pressed`, `-on`) | 0.3.59 | 0224 | Tested |
| The status icons' greys are one, #7d8796, legible on light and dark bars (at least 3:1 on #ececf0 and #24242a): the battery, volume and Wi-Fi/cellular bodies (were #c9d1d9) and their unlit parts (were #3e526b); only the colour changed, no shape | 0.3.56 | 0220 | Tested |
| `blit --version` and `blit --help` | 0.3.20 | — | Tested |

## Diagnostics

| Feature | Since | Status |
| --- | --- | --- |
| Runtime counters: frames, flips, missed refreshes, GPU time, per-output phases, when after the vblank each flip was submitted | 0.1; GPU time 0.2.16; submit phase 0.3.14 | Used |
| Event log, correlated trace, observation snapshot, process view | 0.1; processes 0.2.17 | Used |
| Console watchdog: restores the screen and reports where the loop stalled | 0.1.587; report 0.2.26 | Used |
| Screenshots and session output capture | 0.1; capture 0.2.31 | Used |
| Performance panel: per display, what it drew, why, its times, pictures and a frame-time graph; Blit-wide loop, input, glyphs and errors once; Hibr's STA1 lines; translucent by `hud.opacity` (0.3.25); both compositors; `hud` setting, `BLIT_HUD` | 0.3.22 | Tested |
| Glyph cache hits and misses; desktop CPU over the desktop's whole process tree | 0.3.22 | Tested |

## Retired

Gone on purpose, so nothing should rely on them:

- **SDL renderer** (0.2.22, ADR 0184), and with it presentation trials,
  `server.renderer`, `BLIT_RENDERER` and display rotation (its last
  plumbing removed in 0.3.16; `rotation` other than 0 is refused).
- **Global scanlines** (`PUT /v1/effects`), replaced by per-pane effects (0.2.27).
- **Built-in Retro/Modern fonts** and the console profiles (0.2.35, 0.2.36),
  and Iosevka (0.2.37).
- **Large telephone icons** U+100023–100032 (0.3.5), and the blue, green and
  amber slots of non-folder file icons (0.3.9). Retired codepoints are
  never reused.
