# Development and verification tools

The files in this directory are not the Blit runtime and are not required to
run an installed server. They are reproducible developer tooling:

- `tools/probes/` contains QEMU init scripts, staging helpers, input emitters,
  and small protocol probes used by the acceptance gates.
- `tools/tests/` contains repeatable contract and network tests.
- `packaging/build-deb.sh` is a build-time packaging script.

The installed runtime is the TCC-built `blit-server` binary, exposed as
`/usr/bin/blit`. Hibr owns desktop lifecycle and is started with its own
installed `desktop` command. Blit does not install, require, or invoke a shell
launcher for Hibr.

The optional `hibr/console-blit.so` provider is a C module for Hibr programs
that explicitly load the provider. It is separate from desktop orchestration.
