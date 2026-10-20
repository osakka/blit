# Toolchain baseline

## Current font dependency (candidate576)

TCC compiles the shared raster with FreeType and HarfBuzz's public C API.
`vendor/harfbuzz/source.json` pins unmodified6.0.0 headers/licence, not a
runtime binary. Use OS-maintained `libharfbuzz.so.0`>=6.0.0 (Debian package
`libharfbuzz0b`); the package declares this dependency. A requested colour
context fails if the runtime is too old; no silent replacement shaper.
`BLIT_HARFBUZZ_LIBRARY` selects the explicit build link path, defaulting to
`/lib/x86_64-linux-gnu/libharfbuzz.so.0`. Header identity, corpus/raster and
existing font/backend gates run in the font suite. See ADR0159.

## Current workspace

| Component | Status |
|---|---|
| Compiler | `/usr/bin/tcc` |
| Version | `0.9.27` |
| Target | x86-64 Linux |
| SDL3 source | `vendor/SDL3`, release `3.4.16` |
| SDL3 archive SHA-256 | `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68` |
| libdrm source | `vendor/deps/libdrm`, release `2.4.134` |
| Mesa source | `vendor/deps/mesa`, release `26.2.3` |
| Mesa build | Minimal GBM/EGL/GLES2 + software Gallium build successful |
| SDL3 build | Successful, shared library with KMSDRM enabled |
| SDL3 video backends | KMSDRM, dummy, offscreen |
| KMSDRM runtime | Not testable here: no `/dev/dri` device node exposed |

## Rules

- Invoke the compiler as `tcc` through the configured toolchain, not by
  silently substituting GCC or Clang.
- Record compiler version and target changes in the TCC ADR.
- Keep external dependency discovery explicit and reproducible.
- Do not begin direct-display implementation until a minimal KMSDRM compile,
  link, initialization, and output-enumeration probe succeeds with this TCC
  baseline.
- A missing dependency is a blocked capability gate, not a reason to add a
  compatibility implementation.

## Baseline gate record

On a host exposing a DRM device, validate with TCC:

1. inclusion of the SDL3 public headers;
2. linking a minimal SDL3 application;
3. SDL video-driver selection for the direct-display path;
4. KMSDRM initialization and output enumeration;
5. a first presentation on one physical output.
