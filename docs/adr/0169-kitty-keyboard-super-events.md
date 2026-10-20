# ADR0169: negotiated Kitty keyboard events and Super

- Date: 2026-10-20
- Status: implemented in candidate0.1.585; physical/Hibr integration pending
- Tracking: [Blit116](https://git.home.arpa/itdlabs/blit/issues/116),
  [Hibr326](https://git.home.arpa/itdlabs/hibr/issues/326)
- Supersedes: ADR0059's zero-enhancement decision, not its cursor isolation
- Reference: [Kitty keyboard specification](https://sw.kovidgoyal.net/kitty/keyboard-protocol/)

The old parser answered queries with zero and discarded keyboard mode state.
Lone Super therefore could not reach applications through the native PTY.
Implement flags1 (disambiguate),2 (repeat/release),8 (all-key reporting).
Flags4/16 (alternate keys/associated text) remain unsupported and are masked,
not advertised. This is not a claim of full Kitty keyboard implementation.

Maintain current flags and up to16 saved prior states independently for each
session's main and alternate buffer. Query reports actual active flags; set
supports replace/or/clear; push evicts the oldest saved entry when full;
pop restores prior state or resets to zero on underflow. RIS resets both
buffers. Parameterised query replies are ignored to prevent PTY echo loops.
These sequences must never restore/move the text cursor.

One negotiated encoder is called by production evdev and API-key injection,
independent of presentation backend. Preserve the legacy encoder at flags0;
ordinary text/recovery keys retain legacy behavior where enhancements allow.
Encode both physical Super keys, standard functional/keypad keys and the
unshifted Unicode key identity. Modifiers use1+bitmask, including Super8,
with lock bits under all-key reporting. A modifier edge reflects state after
that edge; two held Super keys remain independent. Repeat/release types are
sent only when requested. Device modifier remapping is resolved before
encoding: existing API `meta` role maps to Super; `none` stays disabled.
Modifier state stays per keyboard, not pooled across devices.

API session capabilities expose the supported mask, active flags and saved
stack depth. Input stream events expose `super`; diagnostics count negotiated
modifier presses when bytes are actually delivered. Owner-authored external
surfaces continue to get stream events, not duplicate PTY writes.

There is no browser frontend here. Standalone KMS reads physical evdev input;
an external browser/desktop client may lose Meta to its operating system.
The server cannot recover events it never receives. Such a client can offer
an explicit Send Super action using existing key injection rather than
claiming guaranteed browser capture or adding a competing input path.

Hibr must negotiate/decode and relay the state through hold/term (Hibr326).
Passing Blit's raw-byte tests does not establish Start-menu behavior through
that multiplexer. Keep116 open until a physical press and complete native
desktop/hold path are validated. Do not edit Hibr core or force its flags from
the server. Candidate development does not change the live584 desktop.
