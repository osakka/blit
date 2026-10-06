# ADR 0005: Kitty graphics first, sixel compatibility second

## Status

Accepted

## Context

Hibr applications can use terminal graphics in addition to ordinary VT text.
Blit must keep terminal state canonical and renderer-independent, so graphics
escape sequences cannot be handled as an SDL-only side channel or silently
discarded. Kitty graphics and sixel are both relevant protocols, but enabling
both in the first graphics slice would expand parser, image lifetime,
placement, clipping, transport, and KMS/SDL verification at once.

## Decision

Blit will implement Kitty graphics protocol first. Kitty is the primary
graphics contract because it supports explicit image identifiers, placement,
z-order, and modern chunked transfer semantics suited to an API-driven
multi-display server.

Sixel remains a planned compatibility milestone after Kitty. It will be
implemented only through the same canonical graphics-object model; it will
not become a second renderer or a separate terminal state owner.

The initial sixel slice accepts bounded direct-color sixel raster data,
including color selection, repeated columns, carriage return, and sixel-row
advance. It creates an owned RGBA graphics object in the same store as Kitty.
Unsupported sixel extensions, malformed data, and bounds violations are
rejected diagnostically; the sixel capability remains unadvertised until the
full compatibility subset is explicitly accepted.

Neither protocol is advertised in `/v1/server` capabilities until its parser,
bounded image store, placement/clipping behavior, lifecycle semantics, and
both SDL3 and direct-KMS evidence gates pass. Until then, unsupported graphics
sequences are consumed without corrupting the text grid and the capability is
reported as unavailable.

## Scope of the Kitty milestone

- bounded chunked upload and explicit image/resource identifiers;
- RGB and RGBA image data with strict size and decompression limits;
- placement on a logical pane, including z-order and deletion;
- clipping and damage integration without changing terminal cells;
- deterministic SDL3 and direct-KMS presentation behavior;
- API inspection and diagnostics for rejected, evicted, or incomplete images.

## Consequences

Kitty clients are the first compatibility target and sixel clients wait for
the follow-up milestone. The canonical model must support bounded image
resources before either protocol is enabled. Text rendering remains
independent of image decoding and presentation.

## Verification gates

The Kitty milestone is incomplete until TCC-built dummy, VKMS QEMU, and
direct-KMS tests demonstrate deterministic chunked upload/retry behavior,
pixel-verified placement/clipping/deletion/z-order, unchanged text/input/
topology semantics, bounded rejection of oversized or incomplete input, and
equivalent SDL3/direct-KMS logical results. Hibr graphics fixtures must pass
without modifying Hibr. Sixel is a separate follow-up gate against the same
object model.
