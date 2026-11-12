#!/bin/sh
# tools/tcc-link.sh exe|shared OUTPUT OBJECT... [-- LINK-ARGUMENTS...]
#
# The one link step for everything Blit builds with tcc (docs/toolchain.md):
# tcc compiles (`tcc -c`), the system linker links, through `cc`. tcc 0.9.27
# writes no PT_GNU_STACK -- the kernel then gives a program's threads
# executable stacks, and glibc 2.41 will not load such a shared object into
# a process whose stack is not executable -- and has no RELRO; the system
# linker writes both: a stack that is not executable (-z noexecstack) and
# relocations read-only after start (-z relro, -z now, programs). tcc's
# runtime (libtcc1.a) and libm are linked as tcc links them.
#
# tcc's debug information (stabs: .stab, .stabstr, from `tcc -g`) is
# allocated -- loaded into memory for its own -bt backtrace runtime, which
# the system linker cannot lay out -- so each object's are turned into
# ordinary debug sections first: kept in the file, not loaded. A crash's
# addresses (src/blit_crash.h) map back to file and line with addr2line.
#
# exe: a program, not position-independent (tcc's objects are not), its
# symbols exported (-rdynamic) so a crash backtrace can name its functions.
# shared: a shared object from objects compiled with `tcc -c -fPIC`.
set -eu
[ "$#" -ge 3 ] || { echo "usage: tools/tcc-link.sh exe|shared OUTPUT OBJECT... [-- LINK-ARGUMENTS...]" >&2; exit 2; }
kind=$1
output=$2
shift 2
runtime=$(tcc -print-search-dirs | sed -n 's/^install: *//p')
[ -f "$runtime/libtcc1.a" ] || { echo "blit: tcc's libtcc1.a not found under '$runtime'" >&2; exit 1; }

objects=
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
    objcopy --set-section-flags .stab=contents,readonly,debug \
            --set-section-flags .stabstr=contents,readonly,debug "$1"
    objects="$objects $1"
    shift
done
[ "$#" -gt 0 ] && shift  # the --

# shellcheck disable=SC2086
case $kind in
    exe)
        cc -no-pie -rdynamic -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
            -o "$output" $objects "$runtime/libtcc1.a" "$@" -lm
        ;;
    shared)
        cc -shared -Wl,-z,noexecstack -Wl,-z,relro \
            -o "$output" $objects "$runtime/libtcc1.a" "$@"
        ;;
    *)
        echo "blit: tcc-link.sh: kind '$kind' is not exe or shared" >&2
        exit 2
        ;;
esac
