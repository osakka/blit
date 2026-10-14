# ADR 0069: the Hibr adapter preserves Hibr's provider semantics

- Status: accepted
- Date: 2026-10-14
- Scope: Hibr display-provider composition and event wakeups

## Context

The packaged Hibr entrypoint explicitly loads `console-blit` and then sources
Hibr's unchanged desktop session. That makes the adapter Hibr's `display`
provider and its `console` command implementation for this process. Treating
the provider as a list of drawing aliases is therefore insufficient: the
desktop depends on stateful semantics which are public in released Hibr.

Read-only comparison with Hibr 0.99.191 found two omissions that directly
match the physical-host failures.

First, a Hibr pane owns a rectangle of cells. While `console behind on` is in
effect, absolute wallpaper writes skip every pane-owned cell; writes through a
pane still land. `console-blit` ignored `behind`, treated panes as coordinate
offsets, and made raise/lower no-ops. A wallpaper or other full-screen pass
could therefore erase an unchanged window or the separately owned menu bar.

Second, `console watch FD` adds an application's PTY to the same wait as
console input. Readiness wakes the desktop so the application can drain and
draw immediately. The adapter ignored watch/unwatch and waited only for the
network input stream. Application output could sit ready until Hibr's idle
timeout, which is 60 seconds by default, while the renderer itself remained
healthy.

The same audit found related semantic gaps: pane clipping and stack order,
drop ownership, wide and combining UTF-8 advancement, `drawn`/`consumed`
change counters, idempotent shadow darkening, and resize notification. Hibr's
`console resizing` is a cheap SIGWINCH flag check performed after every input
wait; the adapter implemented it as a synchronous HTTP geometry request.
Moving the mouse therefore added one round trip per event before the next
frame. These are one provider contract, not independent rendering
implementations. Hibr source is external and out of scope.

## Options considered

1. Reimplement Hibr windows as Blit topology panes. This confuses local
   desktop composition with physical monitor subdivision and creates two
   owners for the same layout.
2. Change Hibr to avoid pane ownership and watched descriptors on Blit. This
   forks the external desktop and makes Blit-specific behavior part of Hibr.
3. Keep the adapter stateless and force every Hibr component to repaint every
   cell continuously. This still does not reproduce stack/hit semantics and
   turns a correctness omission into sustained work.
4. Implement Hibr's provider semantics at the existing adapter boundary,
   while retaining Blit as the only owner of accepted surface cells and
   presentation. This keeps each abstraction in its own namespace.

## Decision

Option 4.

`console-blit` maintains bounded, adapter-local provider state:

- a bottom-to-top list of Hibr-local named panes;
- an ownership mask derived from those panes for `behind` writes;
- clipping for pane-relative put/fill operations, plus raise, lower, drop,
  list, rectangle lookup, and topmost hit testing;
- a bounded set of watched application descriptors multiplexed with the one
  Blit input WebSocket; quiet waits deliberately exclude those descriptors;
- a SIGWINCH pending flag for resize detection, with geometry fetched only
  after an actual edge rather than on every key or pointer event;
- a pending/shadow cell model that advances by Hibr's canonical `u8dec` and
  `u8w`, attaches combining codepoints to their base cell, and records
  idempotent shadow state;
- the `drawn` and `consumed` change counters used by the desktop's redraw
  decisions.

This state does not become a second surface. `console flush` remains the only
frame boundary and sends the resulting cell delta to Blit's revisioned
surface API. Blit remains authoritative for accepted cells, physical display
topology, routing, graphics objects, damage, and presentation. Hibr remains
authoritative for desktop windows and held-session state.

Hibr-local pane names and Blit topology-pane IDs are separate namespaces.
Image placement asks the local pane table first and falls back to Blit
topology only when no local pane has that name.

The adapter is installed outside Hibr's automatic module search path, as ADR
0038 requires, and the packaged Blit entrypoint loads it explicitly. This ADR
amends ADR 0038's obsolete statement that the packaged held path does not use
the adapter; it does not change the module-location decision.

## Consequences

An unchanged Hibr pane survives wallpaper repaint, and a terminal/application
write wakes the desktop when the bytes become readable instead of waiting for
an unrelated key or idle timer. Ordinary input performs no resize HTTP poll,
removing a serial request from every mouse-motion iteration. Pane operations
cost bounded local metadata; they do not create Blit topology resources or
duplicate surface state.

Future Hibr upgrades are compatibility work, not assumed compatibility. The
package pins the released Hibr version used to compile and gate the adapter.
Any newly used provider command must either have equivalent semantics at this
boundary or fail an executable compatibility gate; silently returning success
for behavior the desktop relies on is not acceptable.

## Evidence

`tools/tests/adapter_provider_gate.sh` compiles the shipped adapter against the
exact package-pinned Hibr headers and proves ownership/behind behavior, pane
drop and clipping, stack order/hit testing, raw command parsing, wide and
combining UTF-8, idempotent shadows, watched-descriptor wakeup, and quiet-wait
exclusion. It also proves that steady input performs no resize HTTP poll.
Against Hibr 0.99.191 it reports:

```text
ADAPTER_PROVIDER_PASS pane_ownership=1 watched_wakeup=1 utf8_width=1 resize_poll=0
```

The complete pre-deployment suite reports 69 passed, 0 failed, and four
environment-explicit skips. Issue #78 remains open until the exact package is
accepted on the physical host; a harness pass is not a claim that the live
menu, applications, or pointer are correct.
