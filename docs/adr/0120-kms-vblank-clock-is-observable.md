# ADR 0120: KMS vblank clock is observable

- Status: accepted
- Date: 2026-10-17
- Issue: [Blit #93](https://git.home.arpa/itdlabs/blit/issues/93)

The corrected nominal mode is 59.997 Hz, but the 0.1.533 long workload records
59.902 submissions/s. Its complete-pane pixel check passes. Mode rounding alone
does not explain this. The DRM callback previously discarded sequence/time.

Capture sequence and timestamp in canonical per-output KMS state. Accumulate
wrap-safe sequence deltas and timestamp spans to calculate DRM-reported refresh,
independent of how many flips Blit submits. Emit bounded `kms_vblank` events
through the existing trace API on the first and every sixtieth completion.
Report nominal/observed rates, sequence, timestamp, span count/time, and flip
completions together. Presentation and scheduling are unchanged in this cycle.

The actual callback test uses known timing, skipped sequences, a deliberately
different nominal rate, and uint32 wrap. Existing ownership and pixel tests
must still pass. Capture the same physical soak's render and vblank traces
before interpreting the rate discrepancy or changing its acceptance rule.
