# ADR 0043: Define multi-monitor frame atomicity at the scene boundary

## Context

Blit owns one logical scene and may present it on several independent DRM
CRTCs. The terminal/Hibr commit boundary must not expose partial cell state,
but ordinary `drmModePageFlip` submits one page flip per CRTC. Those flips are
asynchronous and do not provide a cross-CRTC hardware transaction.

## Decision

Blit guarantees atomicity at the logical scene boundary:

1. A render pass reads one immutable set of surface revisions, pointer state,
   overlays, and effect state.
2. Every output selected by that pass is composed from that same set.
3. A surface's deferred Hibr cell writes become eligible only at its commit;
   no output composes the half-written cell batch.
4. The API exposes per-output presentation counters and timing so a consumer
   can identify an output that has not yet completed its page flip.

The ordinary page-flip backend does not claim pixel-synchronous scanout across
independent physical monitors. One CRTC may display the new logical scene for
up to one refresh interval before another CRTC completes its flip. A future
DRM atomic-commit backend may strengthen this to a driver-supported
cross-CRTC commit, but it must not be represented as available on the current
backend without hardware evidence.

## Consequences

This preserves the required terminal/Hibr frame semantics and prevents partial
surface corruption without inventing a synchronization guarantee that DRM
page flips do not provide. Multi-display acceptance must therefore verify
logical scene revisions and per-output completion separately. Visual reports
of a one-refresh inter-monitor skew are presentation timing evidence, not
evidence that Hibr or the terminal parser emitted a partial frame.

## Verification

The QEMU two-output and four-output fixtures verify independent output
composition, Hibr primary/join surfaces, and API-visible presentation state.
The physical cross-monitor scanout synchronization claim remains open until a
real multi-CRTC host and a driver-supported atomic commit path are tested.
