#!/bin/sh
# The one place Blit's C build finds its dependencies (docs/toolchain.md,
# ADR 0230). Every dependency comes from the system's own development
# packages, found through pkg-config, on any current Linux distribution;
# PKG_CONFIG_PATH is the one way to point it elsewhere. tools/build-tcc.sh
# and every gate, tool and probe that compiles C ask here.
#
#   build-flags.sh --cflags GROUP...   compiler flags for the groups
#   build-flags.sh --libs GROUP...     linker flags for the groups
#   build-flags.sh --check [GROUP...]  check the groups' modules (all of
#                                      them without a group), print each
#   build-flags.sh --protocols         wayland-protocols' XML directory
#   build-flags.sh --plasma-protocols  plasma-wayland-protocols' XML directory
#                                      (the global menu's appmenu.xml; it
#                                      ships no pkg-config file, so it is
#                                      found in the same share directory)
#   build-flags.sh --scanner           the wayland-scanner program
#
# Groups:
#   server      blit-server: libdrm, FreeType, HarfBuzz (link only), zlib,
#               the EGL/GLES/GBM headers, libutil
#   drm         libdrm and the EGL/GLES/GBM headers (blit_kms.c includes
#               blit_gles.c), for probes of the KMS path
#   font        FreeType and HarfBuzz (link only), for raster probes
#   zlib        zlib
#   gles        the EGL/GLES/GBM headers
#   compositor  blit-compositor: wayland-server, xkbcommon, and libxcb with
#               its composite, xfixes, res and randr extensions (the X
#               window manager, ADR 0228 phase 2; randr sets X's primary
#               display)
#   xkbcommon   xkbcommon
#   wayland-client  the in-repo stand-in Wayland client (gates)
#   xcb         libxcb and its xfixes and randr extensions: the in-repo
#               stand-in X client (gates), which names its cursor and reads
#               X's displays
#
# A module missing or older than its minimum stops the build here, naming
# the module and the minimum: a blocked capability gate, never a fallback.
#
# HarfBuzz is linked only. Its headers are the pinned, unmodified 6.0.0
# public headers in vendor/harfbuzz (ADR 0159), included by path from
# src/blit_raster.c; `pkg-config --cflags harfbuzz` is never asked, so no
# second set of HarfBuzz headers is on the include path. EGL, GLES and GBM
# are headers only: src/blit_gles.c opens them with dlopen at run time.
set -eu

# Each module and the lowest version Blit is verified to build and pass
# its suites with. "-" checks presence only.
MODULES='libdrm freetype2 harfbuzz zlib egl glesv2 gbm wayland-server
wayland-client xkbcommon wayland-protocols wayland-scanner xcb xcb-composite
xcb-xfixes xcb-res xcb-randr'
minimum() {
    case $1 in
    libdrm) echo 2.4.114 ;;
    freetype2) echo 24.3.18 ;;          # FreeType 2.12.1
    harfbuzz) echo 6.0.0 ;;             # ADR 0159, vendor/harfbuzz
    zlib) echo 1.2.13 ;;
    egl|glesv2|gbm) echo - ;;           # headers only, dlopen'd
    wayland-server|wayland-client|wayland-scanner) echo 1.21.0 ;;
    xkbcommon) echo 1.5.0 ;;
    wayland-protocols) echo 1.32 ;;  # staging/cursor-shape (ADR 0229)
    xcb|xcb-composite|xcb-xfixes|xcb-res|xcb-randr) echo 1.15 ;;  # libxcb 1.15 (Debian 12)
    *) return 1 ;;
    esac
}

die() {
    echo "blit: $*" >&2
    exit 1
}

# group NAME: the modules whose compiler flags it takes, the modules whose
# linker flags it takes, and linker flags of its own.
group() {
    case $1 in
    server) cmods='libdrm freetype2 zlib egl glesv2 gbm'
            lmods='libdrm freetype2 harfbuzz zlib'; extra=-lutil ;;
    drm) cmods='libdrm egl glesv2 gbm'; lmods=libdrm; extra= ;;
    font) cmods=freetype2; lmods='freetype2 harfbuzz'; extra= ;;
    zlib) cmods=zlib; lmods=zlib; extra= ;;
    gles) cmods='egl glesv2 gbm'; lmods=; extra= ;;
    compositor) cmods='wayland-server xkbcommon xcb xcb-composite xcb-xfixes xcb-res xcb-randr'
                lmods='wayland-server xkbcommon xcb xcb-composite xcb-xfixes xcb-res xcb-randr'; extra= ;;
    xkbcommon) cmods=xkbcommon; lmods=xkbcommon; extra= ;;
    wayland-client) cmods=wayland-client; lmods=wayland-client; extra= ;;
    xcb) cmods='xcb xcb-xfixes xcb-randr'; lmods='xcb xcb-xfixes xcb-randr'; extra= ;;
    *) die "build-flags: unknown group '$1' (see tools/build-flags.sh)" ;;
    esac
}

require() {
    command -v pkg-config >/dev/null 2>&1 ||
        die "pkg-config not found: install pkg-config or pkgconf (docs/toolchain.md)"
    for module do
        min=$(minimum "$module") || die "build-flags: unknown module '$module'"
        if [ "$min" = - ]; then
            pkg-config --exists "$module" ||
                die "pkg-config module '$module' not found; Blit needs it (docs/toolchain.md)"
        elif ! pkg-config --exists "$module"; then
            die "pkg-config module '$module' not found; Blit needs $module >= $min (docs/toolchain.md)"
        elif ! pkg-config --atleast-version="$min" "$module"; then
            die "pkg-config module '$module' is $(pkg-config --modversion "$module"); Blit needs $module >= $min (docs/toolchain.md)"
        fi
    done
}

# flags --cflags|--libs GROUP...
flags() {
    kind=$1
    shift
    [ $# -gt 0 ] || die "build-flags: $kind needs a group"
    modules=
    extras=
    for name do
        group "$name"
        if [ "$kind" = --cflags ]; then
            modules="$modules $cmods"
        else
            modules="$modules $lmods"
            extras="$extras $extra"
        fi
    done
    # shellcheck disable=SC2086
    require $modules
    out=
    if [ -n "$(echo $modules)" ]; then
        # shellcheck disable=SC2086
        out=$(pkg-config "$kind" $modules) || die "pkg-config $kind$modules failed"
    fi
    # shellcheck disable=SC2086
    echo $out $extras
}

# variable MODULE VARIABLE: a path pkg-config gives, with the leading //
# some pkgconf builds print made one /.
variable() {
    require "$1"
    value=$(pkg-config --variable="$2" "$1")
    [ -n "$value" ] || die "pkg-config module '$1' has no $2 variable"
    printf '%s\n' "$value" | sed 's#^//*#/#'
}

[ $# -gt 0 ] || die "usage: build-flags.sh --cflags|--libs GROUP... | --check [GROUP...] | --protocols | --scanner"
case $1 in
--cflags|--libs)
    flags "$@"
    ;;
--check)
    shift
    if [ $# -eq 0 ]; then
        # shellcheck disable=SC2086
        set -- $MODULES
    else
        list=
        for name do
            group "$name"
            list="$list $cmods $lmods"
        done
        # shellcheck disable=SC2086
        set -- $(printf '%s\n' $list | sort -u)
    fi
    require "$@"
    for module do
        min=$(minimum "$module")
        [ "$min" != - ] || min='any (headers only)'
        printf '%-18s %-10s needs %s\n' "$module" "$(pkg-config --modversion "$module")" "$min"
    done
    ;;
--protocols)
    dir=$(variable wayland-protocols pkgdatadir)
    [ -f "$dir/stable/xdg-shell/xdg-shell.xml" ] ||
        die "wayland-protocols' XML not found under $dir"
    echo "$dir"
    ;;
--plasma-protocols)
    # plasma-wayland-protocols installs CMake files only, and its XML in
    # <datarootdir>/plasma-wayland-protocols on every distribution: the
    # directory beside wayland-protocols'.
    dir=$(variable wayland-protocols pkgdatadir)
    dir=${dir%/}
    dir=${dir%/*}/plasma-wayland-protocols
    [ -f "$dir/appmenu.xml" ] ||
        die "plasma-wayland-protocols' appmenu.xml not found under $dir (docs/toolchain.md)"
    echo "$dir"
    ;;
--scanner)
    scanner=$(variable wayland-scanner wayland_scanner)
    [ -x "$scanner" ] || die "wayland-scanner not found at $scanner (pkg-config wayland-scanner)"
    echo "$scanner"
    ;;
*)
    die "usage: build-flags.sh --cflags|--libs GROUP... | --check [GROUP...] | --protocols | --scanner"
    ;;
esac
