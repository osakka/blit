# ADR 0230: Dependencies from the system, through pkg-config

- Status: accepted
- Date: 2026-11-10
- Scope: how Blit's C build (tools/build-tcc.sh and every gate, tool and
  probe that compiles C) finds its dependencies; the minimum versions; the
  platforms Blit builds on

## Context

The build found its dependencies in three ways at once. libdrm, and the
EGL, GLES and GBM headers, came from `build/deps/prefix`, a prefix built by
hand from untracked `vendor/deps/{libdrm,mesa}` sources by no script (ADR
0181 records its Mesa 26.2.3 headers). FreeType and HarfBuzz were linked by
Debian's soname paths (`/lib/x86_64-linux-gnu/libfreetype.so.6`), with
`BLIT_FREETYPE_INCLUDE`, `BLIT_FREETYPE_LIBRARY` and `BLIT_HARFBUZZ_LIBRARY`
to override them, and libwayland-server and libxkbcommon the same way. About
forty gates and tools repeated those paths, several with an
`LD_LIBRARY_PATH` into the prefix. A fresh checkout could not build without
the owner's prefix, and no other distribution could build at all.

The owner decided that Blit builds on any current Linux distribution from
the system's own development packages, with minimum versions stated rather
than a distribution named, nothing loose from the old layout, and one source
of truth.

## Options considered

1. Keep the prefix and script its construction — reproducible, but a second
   copy of libdrm and Mesa beside the system's, which is what the system's
   packages already are, and still a Debian layout.
2. A build system (Meson, CMake) — finds dependencies portably, but brings
   a second toolchain to a project whose build is one TCC command (ADR 0001).
3. `pkg-config`, asked from one POSIX shell script — the interface every
   distribution's development packages already ship, with version checks
   (`--atleast-version`) and the standard override (`PKG_CONFIG_PATH`).

## Decision

Option 3. `tools/build-flags.sh` is the one place that finds a dependency:
it asks `pkg-config` for libdrm, freetype2, harfbuzz, zlib, egl, glesv2, gbm,
wayland-server, wayland-client, xkbcommon, and for wayland-protocols' XML
directory (`pkgdatadir`) and the `wayland-scanner` program. It states each
module's minimum once: the lowest versions verified to build and pass the
suites (libdrm 2.4.114, freetype2 24.3.18 = FreeType 2.12.1, harfbuzz 6.0.0,
zlib 1.2.13, wayland 1.21.0, xkbcommon 1.5.0, wayland-protocols 1.31; egl,
glesv2 and gbm present, headers only). A module missing or too old stops
the build naming the module and the minimum: a blocked capability gate
(docs/toolchain.md), never a fallback. `PKG_CONFIG_PATH` is the one
override; the bespoke `BLIT_*_INCLUDE`/`_LIBRARY` variables are gone.

`tools/build-tcc.sh`, every shell gate and every Ruby gate
(`tools/tests/build_flags.rb`) take their flags from it by group (`server`,
`drm`, `font`, `zlib`, `gles`, `compositor`, `xkbcommon`,
`wayland-client`). Python tools load HarfBuzz by soname
(`libharfbuzz.so.0`), as the dynamic loader finds it on any distribution.

HarfBuzz stays as ADR 0159 decided: the pinned unmodified 6.0.0 headers in
`vendor/harfbuzz`, included by path; only its library comes from
`pkg-config` (`--libs`, never `--cflags`). EGL, GLES and GBM stay headers
only, opened with `dlopen` at run time (ADR 0181).

`build/deps/prefix` and `vendor/deps` are retired: nothing reads them.
Packages are `.deb` today; rpm and Arch packaging are later work.

## Consequences

- A checkout builds on a host with the development packages docs/toolchain.md
  lists for Debian/Ubuntu, Fedora and Arch, and nothing else.
- The headers are the system's: libdrm 2.4.114 rather than the prefix's
  2.4.134, the EGL/GLES headers of libglvnd and GBM's of Mesa 22.3.6 rather
  than Mesa 26.2.3's. Blit's sources compile unchanged against them.
- The gates' DRM mock is cached per source and libdrm flags, so a mock built
  against the old prefix's headers is not reused.
- The QEMU fixtures still copy the host's glibc to the guest at the host's
  loader paths; that is the guest's layout, not a build dependency.

## Evidence

`tools/build-flags.sh --check`; `tools/build-tcc.sh` (server, helper,
compositor) with no `build/deps` in the checkout; `tools/run-gates.sh all`,
`font` and `renderer`; `compositor_gate` with its sanitized build;
`packaging/build-deb.sh`; a missing module and a too-old one (a
`PKG_CONFIG_PATH` stub) each refused with the module and its minimum.
