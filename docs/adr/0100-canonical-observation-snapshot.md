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
- Per-output KMS diagnostics distinguish submitted frames from completed
  page flips and report kernel vblank timing. The global composition-rate
  counter is not a substitute for per-output scanout evidence.
- Per-output pointer diagnostics distinguish pointer-intersecting attempts,
  page-flip deferrals, and submitted pointer frames. They describe the
  presentation path, not raw input-event counts.
- `pointer_position_changes` counts accepted logical position/output changes
  after input processing. It bridges raw input-event counters to per-output
  presentation counters; it is neither an input event count nor a frame count.
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

## Per-output presentation evidence

Each render output exposes a `kms_scanout` object. `present_calls` counts
submissions, `flip_completions` counts DRM page-flip completion events, and
the vblank sequence/timestamp span reports the output's kernel timing. The
observed vblank rate is not a claim that Blit submitted changed content at
that rate. Non-KMS renderers report `available: false` and zero KMS counters.
This extends the existing observation contract rather than adding another
endpoint, preserving its single-event-loop capture semantics.
