# ADR 0118: Retry deferred KMS frames at flip completion

- Status: accepted
- Date: 2026-10-17
- Issue: [Blit #91](https://git.home.arpa/itdlabs/blit/issues/91)

## Evidence

The live 0.1.531 whole-pane movement workload has zero stale pixels but submits
52.6–54.1 frames/s on a 60-Hz output. Its render trace shows a present deferred
because a flip is pending, followed by a retry only at the next software
deadline. Some consecutive submissions are approximately 32 ms apart. Render
attempt counters (~62.5 Hz) therefore overstate visible update cadence.

The same live workload with `BLIT_REQUIRE_REFRESH=1` fails at 54.123 submissions/s
against a 58.2-Hz threshold (97% of the physical refresh rate over the bounded
measurement interval). This is the before-fix acceptance failure.

## Decision

Remember which outputs actually deferred a present. On a DRM service event,
query a readiness mask derived from the presenter's canonical per-output flip
ownership and failure state. A deferred output whose buffer is now available
becomes eligible immediately; unrelated output deadlines are unchanged.

Clear retry bits on submission, readiness consumption, and schedule reset.
No back buffer can be touched while its flip remains pending. SDL and outputs
that did not defer retain their existing refresh scheduling. The retry is
bounded by actual DRM completion; it is not a free-running repaint loop.

Direct-KMS FPS excludes deferred/unchanged attempts, and the existing global
`present_calls` counter includes actual KMS submissions. The OpenAPI schema
distinguishes attempts, successful submissions, and physical scanout; immediate
retries must not inflate the reported FPS with rejected attempts.

## Verification

Test per-output readiness against the production KMS state, and check that
only ready deferred outputs have their deadlines advanced. Re-run the same
live PTY/pointer workload with refresh acceptance enabled and every visible
blank pixel checked after completion. Continue to distinguish render attempts,
page-flip submissions, and physical scanout; only submissions are measured by
this test.
The final refresh assertion allows only the one-event quantization uncertainty
of a finite observation window (`refresh_hz - 1 / window_seconds`), replacing
the initial 97% discriminator used to establish the before-fix failure.
