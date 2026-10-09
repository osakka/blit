# ADR 0056: Decompress graphics payloads with zlib rather than a bundled inflate

- Status: accepted
- Date: 2026-10-09
- Scope: dependency policy, Kitty graphics protocol, package dependencies

## Context

Two accepted parts of the Kitty graphics protocol need DEFLATE
decompression and are currently refused:

- `f=100`, PNG, the protocol's only self-describing format and the one a
  client sends when it does not want to decode an image itself. Measured:
  `a=T,f=100` with a valid PNG produces no image.
- `o=z`, zlib-compressed pixel data, which is the protocol's answer to a
  transport whose writes are bounded — and Blit's graphics arrive over a PTY.

Both reduce to the same missing capability. The project otherwise keeps its
runtime dependencies deliberately small: `libc6` and `libdrm2`, with SDL3
vendored and built in-tree.

## Options considered

1. **Implement DEFLATE in the tree.** No new dependency and consistent with
   vendoring SDL3. But it means writing a decompressor for untrusted,
   network-supplied data in C, compiled with TCC, and maintaining it. The
   failure mode of a subtly wrong inflate is a heap overflow reachable from
   any client that can write to a PTY, which is the single worst place in
   this codebase to put new hand-written parsing. Rejected on that basis
   rather than on effort.

2. **Shell out to a decompressor.** Avoids linking, but puts a process spawn
   on the terminal output path and contradicts the project's rule that Blit
   starts only API-requested commands.

3. **Vendor zlib into `build/deps` as SDL3 and libdrm already are.** Keeps
   the dependency list literally unchanged and pins the version. It also
   makes the package carry a second copy of a library the base system
   already ships, and makes a zlib security update a Blit rebuild rather
   than a distribution update.

4. **Link the system zlib.** Chosen.

## Decision

Link `-lz` and add `zlib1g` to the package `Depends`. Graphics
decompression — PNG's IDAT stream and Kitty's `o=z` payloads — uses zlib's
`uncompress`/`inflate` rather than code in this repository.

Decompression stays bounded by the existing graphics resource limits: the
output size is capped before inflation and a payload that exceeds it is a
typed `graphics_reject`, not a larger allocation.

PNG parsing itself — chunk walking, unfiltering, colour-type conversion —
remains in the tree, because it is simple, bounded, and does not involve
compressed-stream state. Only the DEFLATE step is delegated.

## Consequences

`f=100` and `o=z` become implementable, which closes two protocol gaps and
removes the most likely reason a Hibr desktop sends no bitmap at all.

The runtime dependency list grows from `libc6, libdrm2` to
`libc6, libdrm2, zlib1g`. This is a smaller change than it appears: zlib1g is
`Priority: required` on Debian and is already present on any host that can
run the existing package, including the live host. Operators who pin
dependencies should note the addition.

A zlib security advisory now applies to Blit's attack surface. That is the
intended trade: the alternative was owning the same surface with code that
has not been fuzzed.

The choice is reversible. If the dependency becomes unacceptable, option 3
(vendoring) uses the same call sites and only changes the link line.

## Evidence

- `tools/tests/terminal_conformance.rb` transmits the same image as a PNG
  (`f=100`) and as uncompressed RGBA (`f=32`) and asserts both produce an
  image of identical dimensions.
- A malformed PNG and a payload whose inflated size exceeds the graphics
  bound each produce a typed rejection and no image.
