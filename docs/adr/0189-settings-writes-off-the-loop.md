# ADR 0189: Settings writes never block the main loop; the receipt is unchanged

- Status: accepted
- Date: 2026-10-26
- Scope: settings persistence, HTTP request lifecycle, main loop

## Context

On 2026-10-26 the console watchdog killed Blit on the owner's machine. Its
report, from the journal:

    blit: watchdog: main loop in "input, sessions and API" for 9070 ms
    blit: watchdog: task 126086 wchan: folio_wait_bit_common
    blit: watchdog: task 126086 syscall: 74 0x1c ...

Syscall 74 is `fsync`. `PUT /v1/settings` wrote and synced the settings
file inside the request handler, on the main loop that also draws and feeds
the watchdog. While `apt-get upgrade` filled the disk's writeback queue,
one sync took nine seconds. The live Blit Effects pane writes the settings
on every slider release, and the font auto-save and device settings go
through the same flush. So any of them could stall the display server
whenever the disk is busy.

## Options considered

1. **Keep syncing on the loop.** The stall is the bug.
2. **Drop the fsync.** That breaks the documented promise that a 200 means
   the document is on disk.
3. **Answer at once with `pending`, and let clients poll.** Every client of
   the receipt (the Control Panel, the gates, the documented contract)
   would change.
4. **A writer thread, with the reply held until the save is durable.**
   Chosen.

## Decision

- **The disk work moves to one writer thread.** Write, fsync, rename and
  directory sync for a save; unlink and directory sync for `DELETE`. The
  thread takes one job at a time from a mailbox, gets its own copy of the
  document, and touches nothing but the mailbox.
- **Every settings change is a generation.** A save carries the generation
  it was built from.
- **A `PUT` or `DELETE /v1/settings` is held** in the way a screenshot
  request already is, until its own generation is durable. It then gets the
  same receipt as before: 200 with a clean storage state, or 503
  `settings_write_failed`.
- **`DELETE` resets the live settings only after the removal is durable,**
  so a failure still leaves them unchanged.
- **A held request is answered within 30 s,** or told the save could not be
  confirmed (503). Ordinary idle clients still time out after 5 s.
- **The main loop collects results once per turn.** It already wakes every
  10 ms, so a reply follows the disk within about 10 ms. Saves still
  coalesce over 500 ms unless a request is waiting, and failures are still
  reported once and retried at that interval.
- `PUT /v1/topology` and `PUT /v1/fonts` hand their saves to the writer and
  answer as before.
- **At exit,** Blit waits up to 5 s for the writer, then writes anything
  still unsaved itself. A save still running is left to finish; the rename
  keeps the file whole.
- **Without the thread,** for example if `pthread_create` fails, saves run
  on the loop as before, and Blit says so at start.

## Consequences

- A busy or slow disk delays a settings receipt, but no longer drawing,
  input or the watchdog.
- While a save is in flight, `GET /v1/settings` shows the live document with
  storage `pending` and the old `saved_revision`, as coalesced device saves
  always have.
- #146 (a font change stalling on the owner's GPU machine): the font's
  auto-save went through the same flush. The 21→22 px sweep is to be rerun
  on 0.2.30 before #146 is closed.

## Evidence

`tools/tests/settings_writer_gate.rb`, with every save delayed 3 s by
`BLIT_SETTINGS_WRITE_DELAY_MS` (gate-only fault injection):
- the PUT is answered after its save is durable, with a clean receipt;
- `GET /v1/version` answers within 34 ms throughout;
- a drawing pane is presented 122 times during the save;
- storage reports `pending` with the old saved revision;
- two overlapping PUTs are both answered, and the later one wins on disk;
- `DELETE` resets only after the removal;
- a failing write answers 503 and recovers.

The settings, font, login, display and effects gates pass unchanged.
