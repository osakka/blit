# Toolchain baseline

## Compiler and build

Blit is built with TCC by `tools/build-tcc.sh [OUTPUT [TARGET]]`. OUTPUT
defaults to `build/blit/blit-server`; TARGET is `server` (the default),
`helper` (`blit-update-helper`), `adapter` (the Hibr console adapter, which
needs `BLIT_HIBR_ROOT`) or `gamescope` (`blit-gamescope`, ADR 0225, built by
`cc` because the PipeWire headers need GNU C, linked against
`libpipewire-0.3.so.0`). The server links `src/blit_server.c`, `src/blit_kms.c`,
`src/blit_vt.c`, `src/blit_font.c` and `src/blit_raster.c` against libdrm,
FreeType, HarfBuzz, libutil and zlib.

## Workspace

| Component | Status |
|---|---|
| Compiler | `/usr/bin/tcc` |
| Version | `0.9.27` |
| Target | x86-64 Linux |
| libdrm source | `vendor/deps/libdrm`, release `2.4.134` |
| Mesa source | `vendor/deps/mesa`, release `26.2.3` |
| Mesa build | Minimal GBM/EGL/GLES2 + software Gallium build, installed under `build/deps/prefix` |
| HarfBuzz | `vendor/harfbuzz`: pinned unmodified 6.0.0 public headers only |
| PipeWire/SPA headers | `build/deps/prefix/include/pipewire-0.3` and `.../spa-0.2`: unmodified headers from Debian 13's `libpipewire-0.3-dev` and `libspa-0.2-dev` `1.4.2-1` (upstream PipeWire 1.4.2), for `blit-gamescope` only |
| KMS runtime | Not testable here: no `/dev/dri` device node exposed |

The GLES compositor (ADR 0181) does not link EGL, GLES or GBM at build time.
Unless the CPU is chosen (`BLIT_KMS_COMPOSITOR=cpu`, or the saved setting
`server.kms_compositor`), `src/blit_gles.c` opens `libEGL.so.1`,
`libGLESv2.so.2` and `libgbm.so.1` with `dlopen` at start-up; if they are
missing, Blit logs why and composes on the CPU instead.

## Font dependency

TCC compiles the shared raster with FreeType and HarfBuzz's public C API.
`vendor/harfbuzz/source.json` pins unmodified 6.0.0 headers and licence, not a
runtime binary. Use the OS-maintained `libharfbuzz.so.0` >= 6.0.0 (Debian
package `libharfbuzz0b`); the package declares this dependency. A requested
colour context fails if the runtime is too old; there is no silent replacement
shaper. `BLIT_HARFBUZZ_LIBRARY` selects the explicit link path, defaulting to
`/lib/x86_64-linux-gnu/libharfbuzz.so.0`; `BLIT_FREETYPE_LIBRARY` and
`BLIT_FREETYPE_INCLUDE` do the same for FreeType. Header identity,
corpus/raster and font/backend gates run in the font suite. See ADR 0159.

## PipeWire dependency (blit-gamescope)

The `gamescope` target compiles against PipeWire 1.4.2's headers in
`build/deps/prefix/include/pipewire-0.3` and `build/deps/prefix/include/spa-0.2`,
extracted unmodified from Debian 13's `libpipewire-0.3-dev_1.4.2-1_amd64.deb`
and `libspa-0.2-dev_1.4.2-1_amd64.deb` (`usr/include/pipewire-0.3` and
`usr/include/spa-0.2`). `BLIT_PIPEWIRE_INCLUDE` and `BLIT_SPA_INCLUDE` select
other header trees, `BLIT_PIPEWIRE_LIBRARY` the link path (default
`/lib/x86_64-linux-gnu/libpipewire-0.3.so.0`, linked by SONAME). Only the
bridge needs them; `blit` itself does not link PipeWire, and libei is loaded
by Blit at run time.

## Rules

- Invoke the compiler as `tcc` through the configured toolchain, not by
  silently substituting GCC or Clang.
- Record compiler version and target changes in the TCC ADR (ADR 0001).
- Keep external dependency discovery explicit and reproducible.
- A missing dependency is a blocked capability gate, not a reason to add a
  compatibility implementation.
