# Architecture

How the Blit server is put together today. The reasons behind each choice
are in the [decision records](adr/README.md); what it can do is in
[features](features.md).

## Shape

```text
 API clients            frame socket:              evdev: keyboards, mice,
 (HTTP + WebSocket)      pictures by memfd/dmabuf   touchpads, gamepads
         │                       │                           │
         ▼                       ▼                           ▼
 ┌─────────────────────────── main loop (select) ───────────────────────────┐
 │  API gateway ── settings document ── input routing ── PTY sessions        │
 │        │                                   │               │              │
 │        ▼                                   ▼               ▼              │
 │  topology: monitors → panes ──────── terminal emulator: cells, scrollback,│
 │                                       Kitty/Sixel images, damage          │
 │                                              │                            │
 │                                              ▼                            │
 │                 compositor: GLES on the GPU, or the CPU; effects          │
 └──────────────────────────────────────────────┬────────────────────────────┘
                                                │ DRM/KMS page flips
                                                ▼
                                            monitors
```

Beside the main loop:

- a **settings writer** thread that saves the settings document without
  blocking the loop (ADR 0189);
- an **input opener** thread, because opening some evdev nodes is slow;
- a **hotplug monitor** thread watching for monitors (ADR 0130);
- a **console watchdog** process that restores the console and stops the
  server if the loop hangs for 10 seconds (ADR 0170);
- the **update helper**, a separate root service that installs packages
  uploaded through the API, only a release signed with a Blit release key
  (ADR 0008, ADR 0232, security review SR-2).

## The parts

| Part | Where | What it owns |
| --- | --- | --- |
| API gateway | `src/blit_server.c` | HTTP and WebSocket parsing, bearer tokens, the OpenAPI routes, errors as `application/problem+json`, the event stream with replay and back-pressure |
| Settings | `src/blit_server.c` | one revisioned document (`/v1/settings`), applied at start and on every change, written to `/var/lib/blit/settings.json` |
| Sessions | `src/blit_server.c` | PTYs, child processes started with `execve` (no shell in between), their user, environment and lifecycle |
| Terminal emulator | `src/blit_server.c`, `src/blit_graphics.h` | the byte parser, cells and attributes, scrollback, alternate screen, Kitty and Sixel images; see the [terminal reference](terminal.md) |
| Frame socket | `src/blit_frames.h`, `src/blit_frames_input.h` | pictures by descriptor from programs on this machine: a sealed memfd (FRM1) or dmabuf (FRM3) per frame into a session's Kitty image, checked by session token and sender uid; the frame channel's version 5 grammar; input routes into a producer's own socket, with the keymap passed by memfd (ADR 0202, 0223, 0228) |
| Wayland compositor | `src/blit_compositor.c`, `src/blit_compositor_wire.h` (its own program and package, `blit-compositor`) | programs in Hibr windows: each toplevel, popup and subsurface a picture on the frame channel, `WINDOW` lines to the desktop (geometry, states, move and resize), Blit's input on a socket per window, the clipboard and drag and drop bridged to Hibr's (`CLIP`), text input through hibr-ime; and the X window manager of its rootless Xwayland (X windows as pictures, X selections and XDND onto `CLIP`); it composites nothing and owns nothing either side owns (ADR 0228, ADR 0229) |
| Topology | `src/blit_display_layout.h` | monitors, panes per monitor (up to 8×8), which session is in which pane |
| Input | `src/blit_server.c`, `src/blit_touchpad.h`, `src/blit_input_*.h`, `src/blit_keyboard_xkb.h`, `src/blit_ime_root.h`, `src/blit_pointer_*.h` | evdev discovery and identity, keyboards from the system's xkb (ADR 0231), input methods from hibr-ime (Arabizi, ADR 0233), keyboard and pointer settings, the pointer's art, shake and busy states, routing to sessions, pointer lock |
| Glyphs | `src/blit_font.c`, `src/blit_raster.c`, `src/blit_font_registry.h` (generated from `assets/fonts/fonts.source.json`), `src/blit_cell_word.h`, `src/blit_cell_geometry.h`, `src/blit_window_glyphs.h`, `src/blit_control_glyphs.h`, `src/blit_player_glyphs.h`, `src/blit_slider_glyphs.h`, `src/blit_scrollbar_glyphs.h`, `src/blit_icon_codepoints.h`, `src/blit_glyph_sets.h` | the font registry (every face Blit ships, ADR 0243), text fonts, the glyph stack, HarfBuzz shaping, Arabic shaped as words (ADR 0241), colour glyphs, cell-exact line drawing, the icons at U+101000 with the old range mapped onto them, glyph sets by set and name, the glyph cache; the Glyph Protocol's answers are in the terminal emulator |
| KMS | `src/blit_kms.c`, `src/blit_kms_hotplug.h` | connectors, modes, buffers, page flips, refresh scheduling, hotplug |
| GPU compositor | `src/blit_gles.c`, `src/blit_kms_gles.c` | composing panes, glyphs, images and effects with OpenGL ES; Mesa is loaded at run time |
| Effects | `src/blit_effects.h` | the one list of effects, their parameters and order, shared by both compositors |
| Console | `src/blit_vt.c` | the virtual terminal Blit holds, console switching, the watchdog |
| Sound | `src/blit_audio_control.h` | sound cards' mixers |
| Update helper | `src/blit_update_helper.c`, `src/blit_release_signature.h`, `vendor/monocypher` (its own program, `blit-update-helper`) | the root half of an update: its own copy of each package, the checksums, the pair at one version, the release signature (Ed25519) under `/usr/lib/blit/release-keys`, one bounded `dpkg --install` |

## A frame

1. Something changes: a program writes to its PTY, a client calls the API,
   a key is pressed, a monitor appears.
2. The terminal emulator updates cells and images and records the damaged
   rectangles.
3. When a monitor is due for its next refresh, the compositor redraws only
   the damage for the panes on it, applies the pane's effects, and uploads
   only what changed.
4. KMS flips the new frame onto the monitor. Each monitor runs at its own
   refresh rate; an idle monitor is not redrawn at all.
5. Counters (frames, flips, missed refreshes, GPU time per phase) are
   updated and reported in `/v1/diagnostics/runtime`.

A program can hold its pane still while it draws with synchronized output
(mode 2026). A picture it deletes inside the update stays drawn until the
update ends, so a whole-output redraw cannot show a hole
([ADR 0213](adr/0213-graphics-ledger-blink-counters-and-held-pictures.md)).
What the compositors drew is measured afterwards: the graphics ledger and the
blink counters in `/v1/diagnostics`.

## Decisions that shape everything

- **One way to present** (ADR 0184): native KMS. Frames are composed on
  the GPU (ADR 0181) unless the CPU is chosen; when the GPU is unavailable,
  the CPU draws the same pixels instead, and a GPU start that crashes forces
  the CPU next time.
- **One glyph stack** (ADR 0195): every font gets the same icons, symbols,
  emoji and fallbacks, in one order, every face declared once in the font
  registry (ADR 0243).
- **The API is the only control** (ADR 0002): the Control Panel, scripts
  and tests all use the same OpenAPI routes; settings saved through it
  replace startup values.
- **Blit does not own the desktop**: Hibr owns its desktop and runs inside
  Blit sessions; Blit starts Hibr's installed commands and never edits
  Hibr ([Hibr integration](integration-hibr.md)).
- **Compiled by TCC, linked by the system linker** (ADR 0001, ADR 0238):
  every dependency comes from the system through `pkg-config` (ADR 0230,
  `tools/build-flags.sh`), plus the pinned hibr-ime checkout.

## Security

Blit runs as root by default, because it opens the display, input devices
and the console, and starts sessions as other users. The API is protected
by a bearer token, compared in constant time; without one it listens on
loopback only and refuses a foreign `Origin` or `Host`. A session child run
as another user gets none of the server's tokens (ADR 0055, security review
SR-8). The frame socket
is local only and takes frames from a session's own user (or root) holding
that session's token; it never opens a name it is given (ADR 0066, 0202).
Sessions can be
limited to an allow-list of commands. An unprivileged profile runs it as
user `blit` with the device groups it needs. Package installation is a
separate root helper that only accepts `blit` and, when it is installed,
`blit-compositor` at the same version, each with the checksum the server
sends it, signed by a Blit release key, and no `blit` older than 0.3.133
(ADR 0008, ADR 0232); an upload needs the update token and, when the
server has one, the bearer token. See `man blit`, ADR 0065 and the
[November 2026 security review](security-review-2026-11.md), whose findings
are fixed except SR-3 (group `blit` reaches the helper's socket and the API
token) and SR-9 (the API on the LAN over plain HTTP, the accepted
LAN-testing exception).
