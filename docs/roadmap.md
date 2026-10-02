# blit roadmap

The roadmap is ordered by dependency. A later milestone must not become a
second, parallel implementation of an earlier one.

## Current position

The platform capability gate is passing in the deterministic VKMS fixture:

- TCC `0.9.27` compiles and links the SDL3 probe.
- SDL3 initializes through KMSDRM without X11 or Wayland.
- One DRM card exposes two virtual outputs.
- A fullscreen SDL window is created successfully across that fixture.

The product itself has not started: there is no canonical schema, transport,
authoritative state engine, or long-lived blit server yet.

## Milestone 1: contract vertical slice

Resolve ADR 0002 before writing server behavior. It must select the canonical
machine-readable schema format, wire framing, compatibility policy, and the
code-generation/validation boundary.

The first contract should cover only:

- server identity and capabilities;
- output enumeration and topology;
- one text surface with dimensions and cell updates;
- a frame request and presentation acknowledgement;
- stable errors and revisions.

Effects, input, persistence, authentication, and PTY integration remain out of
this slice.

Exit criteria:

1. The schema is the only normative definition of these messages.
2. A generated or mechanically derived validation artifact is checked against
   the schema.
3. One TCC-built server accepts a request and returns a typed response.
4. The same request sequence works with the dummy backend and the VKMS backend.

## Milestone 2: authoritative state engine

Implement one state model behind all transports and renderers. Add revisions,
idempotency handling, deterministic ordering, damage tracking, and structured
diagnostics. The renderer receives immutable frame plans; it does not own API
state.

## Milestone 3: multi-output presentation

Map one logical canvas to the two-output VKMS fixture, then add output loss,
mode changes, gaps, offsets, and atomicity behavior. The topology and frame
acknowledgement semantics must be tested through the public API, not through
SDL-specific test calls.

## Milestone 4: text fidelity

Add grapheme policy, cell attributes, cursor state, scrolling, and incremental
updates. Preserve text and damage as canonical state until the renderer boundary.

## Milestone 5: effects

Add a versioned effect graph with bounded cost, explicit frame/time inputs, and
deterministic presets. CRT effects are downstream of correct text and output
semantics; they must not obscure presentation failures.

## Milestone 6: operational hardening

Add authentication, leases, persistence policy, hotplug behavior, observability,
resource limits, and a non-root deployment profile. Repeat the full contract
suite on VKMS and then on a physical DRM/KMS host.

## Immediate next action

Implement the TCC server gateway against the OpenAPI artifact: start with
`GET /v1/server`, `GET /v1/displays`, `GET /v1/topology`, and the WebSocket
stream handshake. No renderer feature or effect should precede that contract.
