# ADR 0050: Treat Hibr joins as failing attachments when their client exits

Status: Accepted

## Context

The Hibr primary desktop is a held owner. Its Blit-launched child can exit
after handing control to Hibr's detached owner, so Blit must retain the
primary session while that owner remains available. A `desktop --join` client
has a different lifecycle: it is an attachment to the held owner. Keeping a
join in `running` after its PTY child has exited leaves a dead surface, stale
input routes, and prevents a replacement join from being represented
accurately.

## Decision

Blit distinguishes the Hibr desktop role in its authoritative session state:

- a primary remains `running` when its launcher child exits, subject to the
  existing owner discovery/reassertion contract;
- a join transitions to `failed` when its attach child exits or is signalled;
- join cleanup releases its input routes, streams, and surface through the
  normal session lifecycle;
- a new join may be created only while the named primary remains running.

The QEMU lifecycle fixture uses the real stock Hibr launcher, the registered
owner identity, a killed join, and a replacement join. Hibr source and its
desktop semantics remain outside Blit's ownership boundary.

## Consequences

API clients can distinguish a surviving primary from a dead auxiliary attach
and recover by creating a replacement join. A client must not expect a join
session ID to survive termination of its Hibr child. This is a public lifecycle
semantic and is covered by the terminal-session state and cleanup contract.
