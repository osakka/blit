# ADR 0001: TCC is the project compiler

- Status: accepted
- Date: 2026-10-01
- Workspace baseline: `/usr/bin/tcc`, TCC `0.9.27`, x86-64 Linux

## Context

blit is intended to be a small, inspectable systems program. The project
will be written in C and compiled with the Tiny C Compiler (TCC), not merely
tested with it. This is a project constraint and must shape the dependency,
tooling, and portability decisions from the beginning.

The initial workspace baseline is the system-provided `/usr/bin/tcc`, version
`0.9.27`, targeting x86-64 Linux. Until the project deliberately changes this
decision, this is the compiler used for local conformance checks.

The upstream TCC project describes support for ANSI C and most ISO C99, with
code generation for i386, x86-64, ARM, AArch64, and RISC-V 64, plus the
`libtcc` embedding interface. These claims must still be verified against the
specific pinned TCC revision and target used by blit.

## Decision

1. All blit production C code is compiled by a pinned TCC revision.
2. First-party tools that affect the product contract—schema validators,
   generators, conformance tools, and build helpers—are also written in C and
   compiled by TCC unless an ADR grants an exception.
3. SDL3, libc, graphics drivers, DRM/KMS, font libraries, and other external
   dependencies remain external interfaces. We will not fork or duplicate
   their implementations to accommodate TCC.
4. TCC compatibility is tested continuously with a minimal compile/link
   probe, not assumed from the language standard alone.
5. Any use of a host compiler for bootstrapping TCC, building an external
   dependency, or producing analysis artifacts must be documented and must not
   become a second production implementation.

## Consequences

### Benefits

- A small and explicit compiler/toolchain requirement.
- Fast local iteration and a potentially compact deployment toolchain.
- Strong pressure to keep interfaces plain, stable C and avoid accidental
  compiler-specific complexity.

### Costs and risks

- TCC’s C99 coverage and extensions are not identical to GCC or Clang. A
  header or dependency that compiles elsewhere may fail under TCC.
- Optimization, debug information, sanitizer, static-analysis, and profiling
  facilities may be less capable or differently behaved than mainstream
  compilers. Correctness cannot rely on optimizer behavior.
- SDL3 itself is commonly built through platform toolchains and build systems;
  we must distinguish “blit links to SDL3” from “TCC builds SDL3”. The
  latter is not a requirement unless separately proven.
- External libraries may expose compiler attributes, intrinsics, ABI
  assumptions, or headers that require an adapter layer.
- Schema generation written in TCC is additional project scope and must not
  delay validating the runtime architecture.

## Required validation gates

Before committing to a production backend, the project must prove with the
pinned TCC toolchain that it can:

- compile the selected C language subset with warnings enabled;
- include the required SDL3 headers;
- link and call a minimal SDL3 program;
- initialize the selected no-window-system path;
- link the selected font, text, and graphics dependencies;
- produce useful symbols or diagnostics for the supported debug workflow;
- pass ABI and struct-layout checks for every external C interface.

The first implementation milestone is therefore a TCC/SDL3 capability probe,
not the full server.
