# Toolchain baseline

## Platform

Blit builds on any current Linux distribution (Debian, Ubuntu, Fedora,
Arch and the like) from the system's own development packages. Every
dependency is found through `pkg-config` and must be at least the version
in the table below; no distribution is named in the build, no dependency is
built by hand into a private prefix, and no library is linked by a
distribution-specific path. `PKG_CONFIG_PATH` is the one way to point the
build at other copies of the dependencies (ADR 0230).

`tools/build-flags.sh` is the one place that asks: `tools/build-tcc.sh` and
every gate, tool and probe that compiles C take their flags from it.

```sh
tools/build-flags.sh --check               # every module, its version, its minimum
tools/build-flags.sh --cflags server       # compiler flags for a group
tools/build-flags.sh --libs server         # linker flags for a group
tools/build-flags.sh --protocols           # wayland-protocols' XML directory
tools/build-flags.sh --scanner             # the wayland-scanner program
```

The groups are `server`, `drm`, `font`, `zlib`, `gles`, `compositor`,
`xkbcommon`, `wayland-client` and `xcb` (the script's header says what each
holds). A module that is missing or older than its minimum stops the build
with a message naming the `pkg-config` module and the minimum.

Packages are `.deb` today (`packaging/build-deb.sh`); rpm and Arch packages
are later work.

## Dependencies

The minimums are the lowest versions Blit is verified to build with and pass
its suites with (Debian 12's); a newer version is expected to work and must
pass the same gates. Stated once, in `tools/build-flags.sh`.

| `pkg-config` module | Minimum | Debian, Ubuntu | Fedora | Arch | Used by |
|---|---|---|---|---|---|
| `libdrm` | 2.4.114 | `libdrm-dev` | `libdrm-devel` | `libdrm` | blit-server |
| `freetype2` | 24.3.18 (FreeType 2.12.1) | `libfreetype-dev` | `freetype-devel` | `freetype2` | blit-server |
| `harfbuzz` | 6.0.0 | `libharfbuzz-dev` | `harfbuzz-devel` | `harfbuzz` | blit-server (linked only, see below) |
| `zlib` | 1.2.13 | `zlib1g-dev` | `zlib-devel` | `zlib` | blit-server |
| `egl`, `glesv2` | any (headers only) | `libegl-dev`, `libgles-dev` | `libglvnd-devel` | `libglvnd` | blit-server (GLES compositor) |
| `gbm` | any (headers only) | `libgbm-dev` | `mesa-libgbm-devel` | `mesa` | blit-server (GLES compositor) |
| `wayland-server`, `wayland-client` | 1.21.0 | `libwayland-dev` | `wayland-devel` | `wayland` | blit-compositor; the stand-in client (gates) |
| `wayland-scanner` | 1.21.0 | `libwayland-bin` | `wayland-devel` | `wayland` | blit-compositor (generates its protocol code) |
| `xkbcommon` | 1.5.0 | `libxkbcommon-dev` | `libxkbcommon-devel` | `libxkbcommon` | blit-server (its keyboards, ADR 0231), blit-compositor |
| `wayland-protocols` | 1.32 (cursor-shape-v1) | `wayland-protocols` | `wayland-protocols-devel` | `wayland-protocols` | blit-compositor (protocol XML) |
| plasma-wayland-protocols (no pkg-config file: found beside wayland-protocols, `build-flags.sh --plasma-protocols`) | any with `appmenu.xml` | `plasma-wayland-protocols` | `plasma-wayland-protocols-devel` | `plasma-wayland-protocols` | blit-compositor (the global menu, ADR 0229) |
| `xcb`, `xcb-composite`, `xcb-xfixes`, `xcb-res`, `xcb-randr` | 1.15 | `libxcb1-dev`, `libxcb-composite0-dev`, `libxcb-xfixes0-dev`, `libxcb-res0-dev`, `libxcb-randr0-dev` | `libxcb-devel` | `libxcb` | blit-compositor (the X window manager, ADR 0228 phase 2; `xcb-randr` sets X's primary display); `xcb`, `xcb-xfixes` and `xcb-randr` the X stand-in client (gates) |

At run time blit-compositor runs `Xwayland` from `PATH` for X programs,
23.1 or later (`xwayland_shell_v1`; Debian 13 has 24.1): a `Recommends` of
its package (`xwayland` on Debian and Ubuntu, `xorg-x11-server-Xwayland`
on Fedora, `xorg-xwayland` on Arch). Without it, or with an older one, the
compositor serves Wayland programs only and says so in its log; the
`xwayland_gate` skips with the reason.

Besides the modules: `pkg-config` itself (`pkgconf` or `pkg-config` on
Debian and Ubuntu, `pkgconf-pkg-config` on Fedora, `pkgconf` on Arch),
`tcc` (`tcc` on each), and the system's C toolchain for linking: `cc`
(gcc or clang) with binutils' `ld` and `objcopy` (`gcc`/`build-essential`
and `binutils` on Debian and Ubuntu, `gcc` and `binutils` on Fedora and
Arch). The gates also use Ruby, Python 3 and, for the sanitized compositor
build, the same `cc`.

The GLES compositor (ADR 0181) does not link EGL, GLES or GBM at build time:
only their headers are used. Unless the CPU is chosen
(`BLIT_KMS_COMPOSITOR=cpu`, or the saved setting `server.kms_compositor`),
`src/blit_gles.c` opens `libEGL.so.1`, `libGLESv2.so.2` and `libgbm.so.1`
with `dlopen` at start-up; if they are missing, Blit logs why and composes on
the CPU instead.

## Compiler and build

Blit is built with TCC by `tools/build-tcc.sh [OUTPUT [TARGET]]`. OUTPUT
defaults to `build/blit/blit-server`; TARGET is `server` (the default),
`helper` (`blit-update-helper`, libc only), `adapter` (the Hibr console
adapter, which needs `BLIT_HIBR_ROOT`) or `compositor` (`blit-compositor`,
ADR 0228, linked against libwayland-server, libxkbcommon and libxcb with
its composite, xfixes, res and randr extensions, its protocol
code generated by the system's `wayland-scanner` from the system's
wayland-protocols XML into `BLIT_PROTOCOL_DIR`, default `build/protocol`).
The server links `src/blit_server.c`, `src/blit_kms.c`, `src/blit_vt.c`,
`src/blit_font.c` and `src/blit_raster.c` against libdrm, FreeType,
HarfBuzz, libutil and zlib.

The server and the compositor build in the input methods of **hibr-ime**
(ADR 0233), from a checkout of the tag named in `packaging/IME_VERSION`.
It is at `build/hibr-ime-<version>` (inside the checkout, so the build
environment sees it) or wherever `BLIT_IME_ROOT` names. Its `VERSION` must
match, or the build stops. Blit calls `include/ime.h` only. To get it:
`git clone --branch v<version> https://git.home.arpa/itdlabs/hibr-ime.git build/hibr-ime-<version>`.

**tcc compiles, the system linker links** (from 0.3.87): each source is
compiled by `tcc -c` and every output -- the programs and the Hibr adapter
-- is linked by `tools/tcc-link.sh`, through `cc`, the one link step. tcc
0.9.27 writes no `PT_GNU_STACK` (the kernel then gave a program's threads
executable stacks, measured: `blit-server`'s two thread stacks were
`rwxp`; and glibc 2.41 will not load such a shared object into a process
whose stack is not executable, so a gcc-built Hibr could not load the
adapter) and no RELRO; the system linker writes both (`-z noexecstack`,
`-z relro -z now`). tcc 0.9.28rc does not write the header either, and
patchelf 0.18 cannot rewrite tcc's programs. tcc's debug information
(stabs, `-g`), which tcc allocates for its own `-bt` runtime and which the
system linker cannot lay out that way, is made ordinary debug sections
first, kept in the file. tcc's `-bt` crash backtraces are replaced by
`src/blit_crash.h`: on a crash `blit-server` and `blit-compositor` write
the frames to standard error and, where binutils is installed, run
`addr2line` on them for function, file and line (`tcc_fault_trace_gate`
checks it); `package_artifact_gate` checks every shipped ELF file's stack
header and the programs' RELRO.

| Component | Status |
|---|---|
| Compiler | `tcc` |
| Version | `0.9.27` |
| Linker | the system's `cc` and `ld` (`tools/tcc-link.sh`) |
| Target | x86-64 Linux |
| HarfBuzz headers | `vendor/harfbuzz`: pinned unmodified 6.0.0 public headers only |
| KMS runtime | Not testable here: no `/dev/dri` device node exposed |

## Build environment

Releases are built and checked in one environment on any host with podman:
`tools/build-env/`, Debian 13 with the packages in
`tools/build-env/packages` -- every module above at or past its minimum,
TCC and the host compiler, Xwayland 24.1 for `xwayland_gate`, and what the
gates and packaging run. `tools/build-env/run.sh COMMAND...` runs a command
there in the checkout, mounted at `/blit` whichever checkout it is (so
`blit-server`'s debug information, which records source paths, and with
it every package, is the same bytes from any checkout on any host), as
the calling user;
the image is built on first use and again when `Containerfile` or
`packages` change (its tag is their hash). Where rootless podman cannot run
a container (an unprivileged LXC host cannot mount `/proc` for it), it runs
podman through `sudo -n`.

`tools/release/build-release.sh OUTDIR`, with `BLIT_HIBR_ROOT` naming the
Hibr tree of `packaging/HIBR_VERSION` (both inside the checkout), builds a
release there: the server, the update helper and the compositor;
`compositor_gate` and `xwayland_gate`, where a skip is a failure; the
packages into OUTDIR; `releases/<version>.yaml`; and
`package_artifact_gate`. The host's own Debian 12 cannot build
`blit-compositor` (wayland-protocols 1.31, Xwayland 22.1); the environment
is what lets this host release without another machine.

## Font dependency

TCC compiles the shared raster with FreeType and HarfBuzz's public C API.
`vendor/harfbuzz/source.json` pins unmodified 6.0.0 headers and licence, not a
runtime binary, and `src/blit_raster.c` includes those headers by path: the
build never asks `pkg-config` for HarfBuzz's include flags, only for its
library, so no second set of HarfBuzz headers is on the include path. The
runtime is the system's `libharfbuzz.so.0` >= 6.0.0 (Debian package
`libharfbuzz0b`); the package declares this dependency. A requested colour
context fails if the runtime is too old; there is no silent replacement
shaper. Header identity, corpus/raster and font/backend gates run in the
font suite. See ADR 0159.

## Rules

- Invoke the compiler as `tcc` through the configured toolchain, not by
  silently substituting GCC or Clang.
- Record compiler version and target changes in the TCC ADR (ADR 0001).
- Find every dependency through `pkg-config` in `tools/build-flags.sh`, with
  its minimum version stated there and in the table above; no
  distribution-specific path, no hand-built prefix.
- A missing dependency is a blocked capability gate, not a reason to add a
  compatibility implementation.
