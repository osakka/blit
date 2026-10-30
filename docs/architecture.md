# Architecture

How the Blit server is put together today. The reasons behind each choice
are in the [decision records](adr/README.md); what it can do is in
[features](features.md).

## Shape

```text
 API clients            frame socket:              evdev: keyboards, mice,
 (HTTP + WebSocket)      pictures by memfd          touchpads, gamepads
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
  uploaded through the API (ADR 0008).

## The parts

| Part | Where | What it owns |
| --- | --- | --- |
| API gateway | `src/blit_server.c` | HTTP and WebSocket parsing, bearer tokens, the OpenAPI routes, errors as `application/problem+json`, the event stream with replay and back-pressure |
| Settings | `src/blit_server.c` | one revisioned document (`/v1/settings`), applied at start and on every change, written to `/var/lib/blit/settings.json` |
| Sessions | `src/blit_server.c` | PTYs, child processes started with `execve` (no shell in between), their user, environment and lifecycle |
| Terminal emulator | `src/blit_server.c`, `src/blit_graphics.h` | the byte parser, cells and attributes, scrollback, alternate screen, Kitty and Sixel images; see the [terminal reference](terminal.md) |
| Frame socket | `src/blit_frames.h` | pictures by descriptor from programs on this machine: a sealed memfd per frame into a session's Kitty image, checked by session token and sender uid (ADR 0202) |
| Topology | `src/blit_display_layout.h` | monitors, panes per monitor (up to 8×8), which session is in which pane |
| Input | `src/blit_server.c`, `src/blit_touchpad.h`, `src/blit_input_*.h` | evdev discovery and identity, keyboard and pointer settings, routing to sessions, pointer lock |
| Glyphs | `src/blit_font.c`, `src/blit_raster.c`, `src/blit_cell_geometry.h` | text fonts, the glyph stack, HarfBuzz shaping, colour glyphs, cell-exact line drawing, the glyph cache |
| KMS | `src/blit_kms.c`, `src/blit_kms_hotplug.h` | connectors, modes, buffers, page flips, refresh scheduling, hotplug |
| GPU compositor | `src/blit_gles.c`, `src/blit_kms_gles.c` | composing panes, glyphs, images and effects with OpenGL ES; Mesa is loaded at run time |
| Effects | `src/blit_effects.h` | the one list of effects, their parameters and order, shared by both compositors |
| Console | `src/blit_vt.c` | the virtual terminal Blit holds, console switching, the watchdog |
| Sound | `src/blit_audio_control.h` | sound cards' mixers |

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
(mode 2026).

## Decisions that shape everything

- **One way to present** (ADR 0184): native KMS. Frames are composed on
  the GPU (ADR 0181) unless the CPU is chosen; when the GPU is unavailable,
  the CPU draws the same pixels instead, and a GPU start that crashes forces
  the CPU next time.
- **One glyph stack** (ADR 0195): every font gets the same icons, symbols,
  emoji and fallbacks, in one order.
- **The API is the only control** (ADR 0002): the Control Panel, scripts
  and tests all use the same OpenAPI routes; settings saved through it
  replace startup values.
- **Blit does not own the desktop**: Hibr owns its desktop and runs inside
  Blit sessions; Blit starts Hibr's installed commands and never edits
  Hibr ([Hibr integration](integration-hibr.md)).
- **Built with TCC** (ADR 0001): a small, fast compiler, pinned, with
  FreeType and HarfBuzz from the distribution.

## Security

Blit runs as root by default, because it opens the display, input devices
and the console, and starts sessions as other users. The API is protected
by a bearer token; without one it listens on loopback only. The frame socket
is local only and takes frames from a session's own user (or root) holding
that session's token; it never opens a name it is given (ADR 0066, 0202).
Sessions can be
limited to an allow-list of commands. An unprivileged profile runs it as
user `blit` with the device groups it needs. Package installation is a
separate root helper that only accepts a package named `blit` with the
checksum the server sends it. See `man blit` and ADR 0065.
