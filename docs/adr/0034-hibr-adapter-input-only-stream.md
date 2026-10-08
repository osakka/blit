# ADR 0034: Restrict the Hibr adapter stream to input events

Status: Accepted

## Context

The Blit gateway defaults a newly opened WebSocket stream to all event
classes.  The Hibr adapter only needs input events, but its bounded WebSocket
reader cannot consume large surface and frame messages.  Those messages can
desynchronise the adapter's stream and disconnect it, leaving pointer motion
handled by Blit while keyboard and button events never reach Hibr.

## Decision

Immediately after the adapter WebSocket handshake, the Blit-owned adapter sends
a masked WebSocket subscription for the `input` stream only.  Hibr source and
its terminal/input semantics are unchanged.

## Consequences

The adapter receives only bounded keyboard and pointer event messages, so its
reader remains synchronized and the input stream stays connected.  Surface
and frame data continue through the adapter's HTTP surface path and are not
duplicated over the input WebSocket.
