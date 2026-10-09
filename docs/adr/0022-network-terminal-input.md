# ADR-0022: Versioned network terminal input

- Status: accepted
- Date: 2026-10-08

## Context

Blit already exposes PTY sessions and evdev input routing, but an API client
could not send deterministic keyboard or paste data without opening a separate
device stream. That prevented fully network-driven automation and made testing
dependent on host hardware.

## Decision

`POST /v1/terminal-sessions/{session_id}/input` accepts a bounded `TerminalInput`
request. `mode: raw` writes the UTF-8/escape data unchanged. `mode: paste`
adds DEC bracketed-paste delimiters only when the target session has enabled
mode 2004; otherwise it writes the data unchanged and reports
`paste-fallback`. The PTY remains the sole input destination and the existing
terminal state engine remains the sole interpreter of resulting output.

The endpoint accepts at most 768 characters per request, returns a receipt with
requested and written byte counts, and rejects stopped sessions or malformed
requests with typed API problems. Evdev and WebSocket input remain adapters to
the same PTY write boundary; this endpoint is not a second terminal engine.

## Consequences

Network clients can drive shells, Hibr control sessions, terminal games, and
automation without synthetic evdev hardware. Authentication and network
policy continue to govern access to the endpoint. Larger transfers should use
multiple bounded requests or a future authenticated stream protocol.

## Verification

The terminal conformance gate verifies raw input, bracketed-paste framing and
the fallback behavior against a PTY sink; the API schema and version identity
gates cover the public contract.
