# Vision and scope

## One-sentence definition

blit is a network-programmable terminal and display service that owns direct
display outputs, runs PTY-backed terminal sessions, routes input and output,
and composes text surfaces with optional graphic overlays and programmable
effects across one or more physical displays.

## Mission

Give a client complete, observable, policy-controlled access to a text-first
display environment over the network without requiring X11, Wayland, or a
desktop compositor. A client can discover the machine, create and control
terminal sessions, place them across displays, route keyboard and pointer
input, observe diagnostics, and add bounded graphic canvases when text alone
is insufficient.

## Goals

1. Provide a text-first terminal/display environment rather than a
   general-purpose desktop compositor.
2. Run directly against a native display path, with no dependency on X11,
   Wayland, or another window-system server.
3. Make server, graphics, displays, audio, input devices, terminal sessions,
   surfaces, overlays, timing, effects, and diagnostics API-visible resources.
4. Run PTY-backed terminal sessions as first-class managed resources, while
   keeping terminal state canonical in blit rather than in a shell or GUI.
5. Support a logical canvas that can span multiple monitors and preserve an
   explicit mapping from logical coordinates to physical outputs.
6. Provide explicit input routing for keyboards, pointers, focus, selection,
   and terminal mouse reporting.
7. Make retro-terminal effects reproducible, configurable, and composable.
8. Allow clients to control the server over a network without depending on
   backend types or implementation details.
9. Keep the compositor behind the public API: native DRM/KMS is the one
   presentation backend, composed by the GLES compositor or the CPU
   compositor, and neither leaks into the API.
10. Support interactive Hibr workloads, including terminal-native games and
    pixel-oriented arcade/emulation sessions, with refresh-aware presentation
    and observable input latency.

## Non-goals for the first release

- A full X11 or Wayland compatibility layer.
- A general-purpose desktop window manager or compositor.
- Arbitrary pixel-oriented application windows with unrestricted stacking,
  focus, and lifecycle semantics.
- A promise that blit provides a complete remote desktop protocol.
- Unbounded graphic overlays that displace the text model as canonical state.
- Unbounded visual effects that can make frame timing or output behavior
  nondeterministic.
- A claim that Kitty or sixel alone is a high-rate emulator frame transport;
  graphical emulation requires the bounded API-owned graphic canvas path.

## Initial vocabulary

- **Server**: the long-lived blit process and its public API.
- **Terminal session**: a managed PTY-backed process/session with input,
  output, lifecycle, identity, and policy state.
- **Surface**: a logical text plane with a grid, style state, and content.
- **Canvas**: the logical coordinate space containing one or more surfaces and
  outputs.
- **Output**: a physical display target, including its mode and placement.
- **Overlay canvas**: a bounded optional graphic plane composed with text
  surfaces under an explicit lifecycle and capability policy.
- **Input route**: the policy binding an input device, seat, or event source
  to a terminal session, surface, overlay, or API stream.
- **Effect**: a named, parameterized transformation applied during rendering.
- **Scene**: the ordered set of surfaces, overlays, effects, and output
  mappings rendered for a frame.
- **Schema**: the versioned machine-readable definition of API resources,
  commands, events, and errors.

## Design principles

### Text is the primary data model

The server should preserve cells, graphemes, attributes, cursor state, and
damage information as long as possible. Rasterized pixels and overlay buffers
are renderer concerns, not the canonical terminal state.

### Terminal sessions are managed resources

PTY processes, terminal emulation state, output flow control, input routing,
and lifecycle are explicit resources. A shell is one possible session payload,
not the definition of the server.

### Hardware capabilities are discoverable

Graphics, displays, audio, pointer devices, keyboard devices, modes, formats,
and acceleration capabilities must be queryable and must gate operations.
Unsupported behavior produces a typed capability error.

### Control is explicit

Clients should be able to inspect state, issue mutations, subscribe to events,
and correlate a change with the resulting frame or acknowledgement.

### Effects do not own application state

An effect can read a declared render input and its parameters, but it should
not silently alter terminal content, input routing, or layout.

### Hardware boundaries are visible

Monitor enumeration, modes, placement, hotplug, and presentation failures
belong in the API model rather than being hidden behind best-effort
behavior.
