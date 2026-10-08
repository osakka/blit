# Initial architecture

## Runtime shape

```text
API clients
    |
    v
API transport and protocol adapter
    |
    v
Command validation -> authoritative scene state -> event journal
                              |
                              v
                     layout and damage planning
                              |
                              v
                    effect graph / frame assembly
                              |
                              v
                 renderer-neutral frame plan
                    /                 \
                   v                   v
          direct DRM/KMS text       SDL3 canvas/effects
             presentation              adapter
                   \                   /
                    v                 v
                     physical outputs
```

The public API must terminate at the protocol adapter. No client should need
to link against SDL3, access renderer objects, or understand a particular
display backend.

## Toolchain boundary

blit production code and first-party contract tooling are compiled with the
pinned TCC toolchain. The boundary to SDL3 and other dependencies is an
ordinary, narrow C ABI. TCC compatibility is a release gate for that boundary;
it is not acceptable to develop against one compiler and discover TCC failures
later.

The project may use a host compiler to build external dependencies or bootstrap
TCC, but that does not create a second blit implementation. The exact
exception and artifact provenance must be recorded in the build documentation.

## Proposed subsystems

### API gateway

Owns transport listeners, authentication/authorization hooks, request
decoding, schema validation, capability negotiation, error responses, and
subscriptions. The first transport should be chosen for local automation and
diagnostics; a remote transport can be added without changing resource
semantics.

### State engine

Maintains the authoritative model of outputs, canvases, surfaces, styles,
effects, input routes, and frame lifecycle. Mutations are applied in a defined
order and produce typed events.

### Terminal model

Represents a Unicode-aware grid, cell attributes, cursor/selection state,
scrolling, and damage regions. Managed PTY sessions feed terminal input and
output through this model; the PTY is a process boundary, not the canonical
display state.

### Session and input manager

Owns terminal-session lifecycle, PTY attachment, flow control, device/seat
identity, focus, pointer reporting, keyboard routing, selection, and input
permissions. It emits typed input and lifecycle events to the API stream and
feeds terminal output into the authoritative state engine.

### Layout and output manager

Discovers outputs, records their capabilities, and maps them into a logical
canvas. It must represent gaps, offsets, rotations, scaling, and unavailable
outputs explicitly. A canvas may span outputs without requiring one giant
physical framebuffer.

### Effect engine

Evaluates a versioned effect graph at frame time. Effects should declare their
inputs, parameters, supported color formats, and cost characteristics. The
engine needs a bounded policy for expensive effects and a clear behavior when
an effect cannot run.

The first bounded effect contract is exposed at `/v1/effects`: the SDL3 and
direct KMS paths support the deterministic `scanlines` preset with an explicit
0--100 intensity and revision. Effect state is downstream of terminal state
and resets only when explicitly changed by the API.

### Overlay compositor

Owns bounded graphic canvases that are explicitly attached to a topology or
surface. SDL3 and direct KMS composite the canonical overlay records after
terminal/effect composition. Overlays can be used for diagnostics, status,
images, or visual instrumentation, but may not silently replace terminal or
layout state.

### Direct display and SDL3 adapters

The direct DRM/KMS adapter is the correctness-critical terminal presentation
path. It owns connectors, modes, buffers, page flips, output recovery, and the
software/hardware cursor policy. It consumes the renderer-neutral terminal
frame plan and never parses PTY bytes.

The SDL3 adapter translates only graphic canvases and effects into SDL3
resources and presentation operations. SDL3 failures must be converted into
stable blit errors and events; they must not change terminal state.

Gamepad evdev devices are discovered independently of keyboards and pointers.
Their raw button/axis events are routed to a selected session and published on
the input stream with receipt timestamps. Blit does not invent a universal
keyboard escape-sequence mapping for gamepads; an emulator or API client owns
the controller mapping.

### Persistence and observability

Configuration, snapshots, structured logs, metrics, and diagnostic frame data
should be separate from the hot rendering path. A server restart must have a
documented policy for restoring or discarding scene state.

## Frame lifecycle

1. A client submits a validated scene mutation or content update.
2. The state engine applies it and marks affected regions as damaged.
3. The layout manager resolves logical regions against current outputs.
4. The effect engine builds a bounded render plan from the scene snapshot.
5. The SDL3 adapter renders and presents the plan to each active output.
6. The server emits acknowledgement, timing, presentation, and failure events.

The exact threading model is intentionally open. The contract must define
ordering and visibility first; implementation can then use one or more worker
threads without changing client behavior.

## Multi-monitor model

blit should model an output arrangement as a named topology, not infer a
single rectangular desktop. Each output has a stable server-local identity,
physical bounds, logical bounds, transform, and presentation status. The
canvas-to-output mapping should be queryable and mutable through the API.

Initial policy questions include whether a frame is committed atomically
across outputs, how refresh-rate differences are handled, and what happens
when an output disappears mid-frame.

## Security boundary

Binding a process directly to display hardware is privileged. The design must
separate the API's authorization model from SDL3/OS privileges and should
support a least-privilege deployment model. Authentication, client identity,
capabilities, and destructive operations need schema-level representation even
if the first local transport uses operating-system credentials.
