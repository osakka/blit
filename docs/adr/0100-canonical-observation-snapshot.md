# ADR 0100: Canonical operational observation snapshot

- Status: accepted
- Date: 2026-10-16
- Scope: authenticated read-only API diagnostics

## Decision

Blit exposes `GET /v1/diagnostics/observation` as the canonical operational
state dump. The response is an explicit OpenAPI `Observation` envelope with a
schema version, monotonic capture timestamp, and `single_event_loop_turn`
consistency declaration. It contains the same serialized resources exposed by
the focused read-only endpoints: server/version, diagnostics/runtime,
graphics, displays, audio, input, topology, fonts, settings, login, sessions,
effects, and overlays.

The implementation composes the existing resource serializers. It does not
introduce a second state model or infer state from screenshots. Terminal cell
contents and framebuffer bytes remain separate bounded evidence resources.

## Consequences

- Incident tooling can save one authenticated JSON document before and after a
  reproduction, without racing a long sequence of independent requests.
- The snapshot is operationally coherent for the event-loop turn in which it
  was built, but it is not a historical transaction or a pixel capture.
- Adding a state resource requires updating its focused serializer and this
  aggregate contract together; the API schema gate and observation gate catch
  drift.
- The response is bounded by Blit's fixed HTTP/resource capacities. Large
  terminal cell or image evidence must use the dedicated surface/screenshot
  endpoints.

## Rejected alternatives

- A second hand-written diagnostic state structure was rejected because it
  would eventually disagree with the focused endpoints.
- A screenshot-only dump was rejected because pixels cannot explain input
  routing, PTY/session ownership, backend selection, counters, or errors.
- A client-side collection of every endpoint was rejected as the canonical
  mechanism because its members would be captured at different times and it
  would be difficult to reproduce exactly during a fault.
