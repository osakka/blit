# ADR 0038: Keep framebuffer screenshot delivery out of the render loop

- Status: accepted

## Context

The diagnostic screenshot is a framebuffer-sized response. Sending it through
a blocking socket from the SDL/KMS event loop can pause input processing and
presentation while a remote client reads slowly. That pause presents as mouse
lag, blinking, or a frozen desktop and makes the diagnostic tool itself
untrustworthy.

## Decision

Blit queues encoded screenshot bytes on the requesting HTTP client and sends
them only when that socket is writable. The response is bounded by the
existing framebuffer-size limit; a slow or disconnected client cannot block
the render, input, PTY, or Hibr service loop.

## Consequences

Screenshots remain available as pixel evidence, while timing and input remain
serviceable during download. The regression gate uses an intentionally slow
reader and requires the version endpoint to remain responsive during the
transfer.
