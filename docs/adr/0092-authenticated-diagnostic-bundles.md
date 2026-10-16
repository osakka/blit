# ADR 0092: Authenticated, correlated diagnostic bundles

## Status

Accepted for the live-render troubleshooting boundary.

## Context

Blit's API is commonly authenticated, but the older diagnostic scripts used
raw `curl` calls. They therefore failed against a secured live host or led an
operator to disable authentication to collect evidence. The scripts also
discarded screenshot response headers, omitted capability/settings documents,
and did not record whether a capture was authenticated. That made a screenshot
hard to correlate with the runtime frame and surface revisions that produced
it.

## Decision

`tools/diagnostics/http.sh` is the one transport boundary for diagnostic
scripts. It reads either `BLIT_AUTH_TOKEN` or `BLIT_AUTH_TOKEN_FILE`, sends a
Bearer header without printing the secret, and rejects ambiguous or empty
credentials.

`capture-live.sh` remains the canonical read-only collector. It records the
server, graphics, audio, displays, topology, input, fonts, settings,
diagnostics, runtime, filtered traces, surface summaries, session probes, and
per-display screenshots. Screenshot response headers are retained beside the
BMP, including the frame and surface-revision identity. The bundle records
capture metadata and completion state, including authentication mode, duration,
trace limit, and poll count.

The experience, troubleshooting, pointer, and live-input tools use the same
transport helper. They do not create a second authentication or screenshot
implementation. Secrets never appear in bundle metadata or command output.

## Consequences

The same capture works against authenticated and deliberately unauthenticated
development fixtures. A support bundle can be interpreted without guessing
which API state or framebuffer identity was observed. Captures remain
read-only; screenshot reads are still kept outside timed phases where the
caller is measuring latency.

## Rejected alternatives

- **Disable authentication for diagnostics:** rejected because it changes the
  security boundary to collect evidence.
- **Put the token in the URL:** rejected because URLs leak into shell history,
  proxy logs, and capture metadata.
- **Add a second support API:** rejected; the existing versioned diagnostics
  routes already expose the required state, so the missing piece was a sound
  client-side transaction.
