# HarfBuzz public C headers

Unmodified `.h` files from upstream6.0.0 `src/` and its `COPYING` notice.
Exact archive and file hashes: `source.json`. No HarfBuzz runtime source or
binary is bundled here. Blit remains C built with TCC and links the supported
public C API of the OS-maintained `libharfbuzz.so.0` (minimum6.0.0).
Distribution security updates are not replaced with a frozen upstream binary.

The pinned font/corpus was audited on6.0.0; newer compatible system runtimes
must pass the same gates. This is an explicit shaping dependency, not a second
renderer or an implementation of OpenType in Blit. See ADR0159.
