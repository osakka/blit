# ADR 0238: TCC compiles, the system linker links, and Blit writes its own crash backtrace

- Status: accepted
- Note: Recorded on 2026-11-20 during the documentation review; the
  decision was made and shipped in 0.3.87 (commit 89592c5) without a record.
- Date: 2026-11-12
- Scope: the build (`tools/build-tcc.sh`, `tools/tcc-link.sh`), every
  shipped ELF file, crash reporting (`src/blit_crash.h`); amends ADR 0001,
  supersedes ADR 0154

## Context

TCC 0.9.27 writes no `PT_GNU_STACK` header and no RELRO. On a running
blit-server both thread stacks were mapped `rwxp`, and glibc 2.41 refuses
to load a shared object without the header into a process whose stack is
not executable: a gcc-built Hibr could not load `console-blit.so`. TCC
0.9.28rc writes neither; patchelf 0.18 cannot rewrite TCC's programs, and
execstack is not in Debian 13.

ADR 0154 had made TCC's `-g -bt8` runtime Blit's crash trace. It needs
TCC's own link step.

## Options considered

1. Keep TCC's linker and patch the headers afterwards: no tool in Debian
   13 does it for TCC's output.
2. Move to another compiler: reverses ADR 0001.
3. TCC compiles, the system linker links (the owner's choice).

## Decision

- TCC compiles every source to objects; `tools/tcc-link.sh` is the one
  link step, through `cc`, with `-z noexecstack`, and for programs
  `-no-pie -rdynamic -z relro -z now`. TCC's allocated stabs become
  ordinary debug sections, kept in the file for addr2line.
  `tools/build-tcc.sh` builds the server, the update helper, the Hibr
  adapter (shared) and the compositor this way.
- TCC's `-bt` runtime is replaced by `src/blit_crash.h`, installed by
  blit-server and blit-compositor. On SIGSEGV, SIGBUS, SIGILL, SIGFPE or
  SIGABRT it walks the frame pointers on its own stack, writes one line per
  frame (named where the address is inside an exported symbol), runs
  `/usr/bin/addr2line` on the program's frames for function, file and
  line, and lets the default action end the process.

## Consequences

- Every shipped ELF file has a non-executable stack, and the programs have
  full RELRO; Hibr built with gcc loads Blit's adapter.
- The system C toolchain (`cc`, `ld`) is a build dependency beside TCC
  (docs/toolchain.md).
- Crash traces name static functions with file and line where binutils is
  installed; without it the addresses are printed for addr2line later.

## Evidence

- `tcc_fault_trace_gate.rb` builds its fixture through `tcc-link.sh` and
  checks the stack header, RELRO, a SIGSEGV trace, and addr2line naming a
  static function and its caller with file and line.
- `package_artifact_gate.sh` checks every shipped ELF file's
  `PT_GNU_STACK` (RW) and the programs' RELRO.
