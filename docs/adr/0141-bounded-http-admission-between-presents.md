# ADR 0141: Admit bounded HTTP bursts between presentation turns

- Status: implemented; bounded physical improvement verified, residual pressure remains
- Date: 2026-10-19
- Issues: [Blit #105](https://git.home.arpa/itdlabs/blit/issues/105), #104

## Evidence

The SDL physical baseline has approximately 135ms request p95 under an
eight-worker pointer workload while submitting roughly 58 frames/s. The
main loop accepts only one connection per turn. A controlled fixture keeps
the actual gateway and SDL renderer, delays public `SDL_RenderPresent` by
16ms, and queues eight complete requests during a blocked present. Responses
arrive at 17.57, 34.63, 52.62, 70.26, 87.03, 104.21, 121.37 and 139.39ms.
This reproduces admission being paced one refresh per queued connection.
The fixture is not physical scanout evidence.

## Decision

Use a nonblocking listening socket and accept at most eight connections per
main-loop turn. Stop on `EAGAIN`/`EWOULDBLOCK`, bound interrupted attempts,
and retain the existing request parser, authentication, 32-client capacity,
response handling and disconnect ownership. Resume input, PTY, API and output
deadline servicing after the bounded turn. Existing clients are still serviced
through the normal readiness set; no new request or rendering thread is added.

Unbounded accept draining could starve presentation/input under connection
flood and is rejected. Increasing queues alone only hides waiting. Arbitrary
SDL workers violate its ownership boundary and do not fix HTTP admission.
No terminal/renderer policy, API schema or display refresh setting changes.

## Verification

The actual-server fixture is red before the fix: the last queued response
takes 139.39ms, exceeding its deliberately broad 80ms regression budget.
Require all eight responses within that budget after the change, then check
that a single subsequent request does not block an under-filled accept turn.
Run network/lifecycle/renderer gates and the broader suite. Re-run the same
physical 240-slot/s workload, record actual delivered/skipped requests and
latencies separately from presentation calls/completions, and retain all
pixel/protocol checks. Restore the native baseline. This narrow regression
budget is not a substitute for the physical performance acceptance plan.

After the bounded-accept change, the same fixture returns all eight responses
within 17.83–18.29ms in the focused runs. A production-code socket fixture
also checks exactly eight admissions in the first turn, finite under-filled
draining, unchanged pending/render-deadline state, close-on-exec descriptors,
32-client capacity and five overflow503 responses.
The same delayed-present fixture is red on offscreen GLES2 using the
manifest-matched0.1.558 staging binary (142.53ms), and green on the0.1.559
canonical binary (18.21ms). This preserves one oracle across SDL profiles.
The full run passes107 gates, with7 explicit skips and no failures; the
three pinned-header adapter gates pass separately.
Physical559 revalidation delivers approximately221–223 SDL requests/s versus
67–71 before, and p95 improves from135ms to51–52ms. All bounded pixel/protocol
checks pass and native/login/pointer restore. Client queue skipping remains
at240 requested slots/s, so issue105 and full latency acceptance stay open.
