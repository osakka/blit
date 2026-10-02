# Vision and scope

## One-sentence definition

blit is an API-controlled, text-only display server that owns one or more
physical displays, renders terminal-like surfaces with programmable visual
effects, and exposes the whole system through a versioned schema.

## Goals

1. Provide a text-first display primitive rather than a general-purpose
   desktop compositor.
2. Run directly against a native display path, with no dependency on X11,
   Wayland, or another window-system server.
3. Treat terminal content, geometry, monitor topology, input, timing, and
   visual effects as API-visible resources.
4. Support a logical canvas that can span multiple monitors and preserve an
   explicit mapping from logical coordinates to physical outputs.
5. Make retro-terminal effects reproducible, configurable, and composable.
6. Allow clients to control the server locally or remotely without depending
   on SDL3 types or implementation details.
7. Keep the renderer replaceable: SDL3 is the initial platform/rendering
   integration, not the public API.

## Non-goals for the first release

- A full X11 or Wayland compatibility layer.
- Arbitrary pixel-oriented application windows.
- A shell, terminal emulator, or PTY manager bundled into the core server.
- A promise that every SDL3 backend can operate without a window-system
  integration. The deployment must select and validate a direct display path
  such as DRM/KMS where appropriate.
- Unbounded visual effects that can make frame timing or output behavior
  nondeterministic.

## Initial vocabulary

- **Server**: the long-lived blit process and its public API.
- **Surface**: a logical text plane with a grid, style state, and content.
- **Canvas**: the logical coordinate space containing one or more surfaces and
  outputs.
- **Output**: a physical display target, including its mode and placement.
- **Effect**: a named, parameterized transformation applied during rendering.
- **Scene**: the ordered set of surfaces, overlays, effects, and output
  mappings rendered for a frame.
- **Schema**: the versioned machine-readable definition of API resources,
  commands, events, and errors.

## Design principles

### Text is the primary data model

The server should preserve cells, graphemes, attributes, cursor state, and
damage information as long as possible. Rasterized pixels are a renderer
concern, not the canonical state.

### Control is explicit

Clients should be able to inspect state, issue mutations, subscribe to events,
and correlate a change with the resulting frame or acknowledgement.

### Effects do not own application state

An effect can read a declared render input and its parameters, but it should
not silently alter terminal content, input routing, or layout.

### Hardware boundaries are visible

Monitor enumeration, modes, rotation, placement, hotplug, and presentation
failures belong in the API model rather than being hidden behind best-effort
behavior.
