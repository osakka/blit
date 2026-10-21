# ADR0178: linear chunk decoding and direct packed-RGBA submission

- Status: accepted for development, not deployed
- Date: 2026-10-21
- Scope: internal125/122 image ingestion; no protocol/API change

The production TCC decoder calls `strlen` in its per-byte loop condition.
A counted-call fixture observes4,097 full length scans for a4,096-byte
payload, including16,785,409 examined bytes when counting each terminator.
That byte count sums logical scan lengths, not measured cache/DRAM traffic.
This is deterministic repeated work, not a measured live rendering bottleneck.

Compute the immutable input length once. Preserve the same bounded output,
Base64 tail state, accepted whitespace, padding and error behaviour. This
does not expand protocol support or introduce a second decoder. Check binary
output and arbitrary split positions, capacity canaries and rejected inputs.

The SDL cache already reuses unchanged textures, but every changed image is
repacked into newly allocated RGBA memory, even when already packed RGBA.
Pass an existing32-bit RGBA buffer directly to synchronous SDL_UpdateTexture
with its original pitch. Preserve straight-alpha samples and texture policy;
retain RGB24-to-RGBA conversion for24-bit input. No asynchronous pointer
ownership, readback, PBO or threading change. The caller retains its image
buffer during the call, just as the previous staging buffer was retained
until return and immediately freed. Driver-internal copying is not eliminated
or claimed zero-copy.

Require counted-call regression evidence, exact bytes/pitch/alpha at the SDL
boundary, unchanged-generation texture reuse, changed-generation submission,
software/GL composition gates and memory checks. Keep graphics damage work
and hotplug acceptance separate. These changes are not a physical FPS claim.
