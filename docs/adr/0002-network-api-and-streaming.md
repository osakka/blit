# ADR 0002: OpenAPI control plane and network streaming plane

- Status: accepted
- Date: 2026-10-02

## Context

blit is a network-programmable terminal and display service. Clients must be
able to inspect the host, discover graphics/displays/audio/input capabilities,
create and control PTY-backed terminal sessions, configure topology, and
observe input, output, presentation, and diagnostic events.

The API must work over a network. A request/response interface alone is not
enough: terminal output, input, hotplug, frame acknowledgements, and
diagnostics need a long-lived streaming path.

## Decision

1. The canonical public contract is OpenAPI 3.1 in `openapi/blit-0.1.yaml`.
2. The control plane uses HTTP over TCP. HTTPS is required for non-local
   deployments; plain HTTP is a development-only mode.
3. Control resources use JSON request and response bodies. Errors use the
   `application/problem+json` shape defined in the OpenAPI components.
4. The streaming plane uses a WebSocket upgrade on the same HTTP service. Its
   messages reuse the schemas in the OpenAPI document through the
   `x-blit-stream` declaration.
5. The stream is bidirectional. The server sends terminal output, surface
   deltas, frame acknowledgements, hotplug, input, and diagnostics; clients
   may send stream-scoped input acknowledgements, subscriptions, and session
   control messages.
6. The primary stream carries semantic text, input, state, and diagnostics,
   not an unrestricted remote framebuffer. Graphic overlays are explicit API
   resources and may opt into capability-bounded binary assets later.
7. Every resource has an opaque identifier and revision. Mutations expose
   idempotency and conditional-update semantics where retries or conflicts
   matter.

## Rationale

OpenAPI provides an inspectable contract, generated client support, and a
stable place to describe the full inventory/control surface. HTTP over TCP is
widely routable and debuggable. WebSocket supplies the required bidirectional
stream without inventing a second transport protocol.

OpenAPI does not standardize WebSocket channel descriptions. The small
`x-blit-stream` extension identifies the endpoint and references the same
component schemas; it does not create a second schema or duplicate message
definitions.

## Consequences

- The API remains usable by ordinary HTTP clients and generated SDKs.
- Streaming clients need WebSocket support in addition to HTTP support.
- Authentication, authorization, leases, rate limits, and TLS are mandatory
  before exposing display or input control beyond a trusted local network.
- A future binary asset or high-rate media path must be added as an explicit
  capability, not silently mixed into the semantic event stream.
- OpenAPI tooling and WebSocket tooling are external contract tools; blit
  production behavior remains implemented once in the TCC server core.

## Rejected alternatives

- A bespoke raw TCP protocol: it would duplicate schema, client tooling, and
  error semantics.
- HTTP polling for terminal output: it adds latency and loses natural event
  ordering under sustained output.
- Treating OpenAPI as a pixel-stream protocol: it would make text state and
  hardware capabilities secondary to an opaque framebuffer.
