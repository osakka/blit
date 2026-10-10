# ADR 0064: One device table, stable ids, and per-device configuration

- Status: accepted
- Date: 2026-10-10
- Scope: the `/v1` device contract — input devices, input routing, audio and
  display inventories; the evdev scan; what a control-panel application may
  rely on

## Context

Blit's device surface was built as four independent read-only inventories
(`/v1/input/devices`, `/v1/audio`, `/v1/displays`, `/v1/graphics`) plus one
write path that is not per-device at all. A Hibr control-panel application
cannot be written against it. Six specific reasons, each measured in the code
or on the live host:

1. **There is no stable device identity.** An item's `id` is `event-N`, derived
   from the node number. Node numbers are assigned in discovery order, so a
   replug or a reboot renumbers them. Any configuration a control panel saves
   against `event-5` can land on a different device after a restart.

2. **There are two device models and two classifiers, and they disagree.**
   `input_json` opens all 32 `/dev/input/event*` nodes on every GET and
   classifies with its own heuristics (`KEY_A || KEY_ENTER` for a keyboard,
   `BTN_LEFT || BTN_RIGHT || REL_X || REL_Y` for a pointer). The runtime
   separately holds the fds it actually polls in per-kind arrays keyed by
   path, populated by `evdev_register_path` using different predicates
   (`evdev_has_keyboard`, `evdev_has_rel_xy`, `evdev_has_gamepad`). The
   inventory can therefore list a `pointer` the runtime never opened, and it
   cannot say whether any listed device is live.

3. **The inventory GET is the stall path.** Issue #16 measured a single evdev
   rescan blocking the whole server for 3.46 seconds. `input_json` performs
   exactly that scan — 32 synchronous `open()` calls and four `EVIOCGBIT`
   ioctls each — inside the request handler, on the event-loop thread. A
   control panel that polls the inventory is a control panel that periodically
   freezes the server it is controlling.

4. **Routing is per-kind, not per-device.** `InputRoute.device_id` is an enum
   of three aggregate pseudo-devices, `evdev-keyboard`, `evdev-pointer`,
   `evdev-gamepad`; `input_route_target()` matches on that string. Every
   discovered keyboard feeds the one keyboard route. The live host has four
   keyboards and three pointers — three of which are the two interfaces plus
   the consumer-control node of a single remote — and no way to treat them
   differently.

5. **Nothing is configurable.** There is no per-device enable, no pointer
   sensitivity, no keyboard layout (#35), no audio volume or default sink. The
   only writes that reach a device are `POST /v1/input/inject` and
   `PUT /v1/input/routes/{id}`.

6. **The revisions are fake.** `input_json`, `audio_json`, `graphics_json` and
   `empty_inventory_json` all emit a literal `"revision":1`. Only
   `/v1/input/routes` carries a real counter. A client cannot detect a hotplug
   or a configuration change, and conditional requests are impossible.

Per-device *counters* already exist — `diagnostic_evdev_mouse_device_*` and
`diagnostic_evdev_keyboard_device_*`, reported by
`/v1/diagnostics/runtime` as `evdev_mouse_devices` / `evdev_keyboard_devices`
arrays keyed by path. They are the right data in the wrong place: a client has
to join a diagnostics array against an inventory array on a path the inventory
does not publish (#17).

## Options considered

1. **Add configuration fields to each existing inventory, keep the rescan.**
   Cheapest to write. Keeps the disagreement between the two classifiers,
   keeps the stall on GET, and bakes `event-N` into saved configuration — so
   the first replug silently reassigns settings. Rejected.

2. **One cross-class `/v1/devices` collection replacing the four inventories.**
   Uniform and small, but it breaks every existing client and gate for no gain
   the common envelope does not already give, and it forces unrelated classes
   (a DRM card and an ALSA PCM) into one schema. Rejected.

3. **A runtime-owned device table per class, served from memory, with a common
   resource envelope, item endpoints and PATCH-able configuration.** The
   collections keep their paths and their names. Chosen.

## Decision

**The runtime owns one input device table. The API is a view of it, never a
scan.**

`BlitRuntime` gains `BlitInputDevice input_devices[BLIT_MAX_INPUT_DEVICES]`,
populated by `evdev_register_path` during the scan that already runs, and
retired by the same code that already detects a lost device. The per-kind fd
arrays remain the poll set and each slot carries a `device_index` into the
table; the table is the identity, the arrays are the plumbing.
`GET /v1/input/devices` serialises the table and performs no `open()`.

**Device identity is derived from the device, not from its node.** The id is

    input-<bustype>-<vendor>-<product>[-<slug>][-<ordinal>]

with `bustype`, `vendor` and `product` as four lowercase hex digits from
`EVIOCGID`, and `slug` a sanitised `EVIOCGUNIQ` (the device's own serial) if it
has one, else `EVIOCGPHYS` (its topological position, e.g.
`usb-0000:00:14.0-1/input0`), sanitised to `[a-z0-9._-]` and truncated. The
physical path is the disambiguator that matters in practice: the live host's
remote exposes three event nodes that share one `input_id` and differ only by
their `phys` interface suffix. When the identity block is all zeros and both
strings are empty — a virtual device — the id falls back to the node basename.
`node` ("/dev/input/event4") is reported as a separate, explicitly unstable
field.

**On a collision the device's own name is the discriminator, and a scan-order
ordinal only the last resort.** This was corrected against live data rather
than designed: deployed with a bare ordinal, 192.168.10.54 reported 28 input
nodes and only 14 distinct `(identity, phys)` pairs, because one HID interface
commonly presents several event nodes — a keyboard, a consumer-control and a
system-control collection — that report the same bus id and the same `phys` and
differ only in their name. Fourteen of the 28 ids were therefore pure scan
order, and one of them, `2.4G Dongle Keyboard`, is a device the server
actually reads. With the name as the first discriminator, 27 of the 28 are
stable. The last pair reports an identical bus id, an identical `phys` and an
identical name; nothing in evdev distinguishes them, so they take the ordinal
and the contract says so. The composed id is bounded at 112 bytes, inside
`ResourceId`'s 128.

This is as stable as udev's own `by-id` naming and no more: moving a device to
a different USB port changes its `phys` and therefore its id, unless the device
reports a serial. That is a property of the hardware, not of the choice, and
the contract states it.

**Every device item, in every class, carries the same envelope:**

```
{ "id", "class", "kinds", "name", "node", "identity",
  "state", "capabilities", "configuration", "revision" }
```

`class` is `input` | `audio` | `display`. `capabilities` is read-only fact
about the hardware. `state` is read-only live truth — whether the device is
open, when it was last seen, and its own event counters. `configuration` is the
writable part and nothing else is writable.

**An absent device keeps its configuration.** A device that is unplugged stays
in the table with `state.present: false` for as long as it carries
configuration; only an unconfigured absent device is retired. A stable id is
worthless if what is attached to it evaporates while the device is out of the
machine, which is exactly when a control panel most wants to still show it.

**Configuration is held in memory.** A Blit restart resets every device to the
defaults and a controller re-applies what it wants; the collection revision
dropping back toward 1 is the signal that it has to. Persisting configuration
across a restart is a separate decision, not taken here — and worth weighing
against issue #55, which records that a restart is a real operational event
that already loses the desktop.

**Configuration is written per device, by PATCH, with the concurrency rule the
rest of Blit already uses:**

    PATCH /v1/input/devices/{id}      If-Match: <revision>
    PATCH /v1/audio/{id}              If-Match: <revision>
    PATCH /v1/displays/{id}           If-Match: <revision>

A mismatched revision is `409`; an unknown configuration property is `400`
with the property named, as ADR 0058's write discipline and the cell-update
refusal already do. `GET /v1/<class>/{id}` returns one item.

**Per-device routing supersedes the aggregate routes without removing them.**
A device's `configuration.route` names a `target_session_id` and an `enabled`
flag. Resolution order is the device's own route, then the aggregate
`evdev-keyboard` / `evdev-pointer` / `evdev-gamepad` route, then the caller's
own fallback — the focused session, or the surface under the pointer. A route
that exists but is disabled **falls through** rather than blocking, so
disabling it restores the behaviour that applied before it instead of dropping
the device's input. A route whose target session ends is disabled and keeps its
target, so a controller can still see what it pointed at.
A route object carrying `enabled: false` and no `target_session_id` removes
the route, which is how a controller puts a device back the way it found it;
without that a device could be routed and un-enabled but never un-routed.
`configuration.enabled: false` releases the device's file descriptors
entirely.

**One rule decides where a property applies.** A device of no known kind — one
Blit probed and reads under no kind at all — accepts any property and any
injected event, because Blit has made no claim about what it is. A device of a
known kind refuses what contradicts its kinds, with a 422. The same rule
governs `POST /v1/input/inject`, so an injection can deliver only what that
device could have delivered. Before this there were two rules: `keymap` was
accepted anywhere while `sensitivity_percent` was refused on a non-pointer —
and a `pointer_relative` injection through a keyboard was then scaled by a
sensitivity no one could set. A controller can set exactly what it can see.

The decision is carried by `runtime->input_source`, a field set at the top of
each `service_evdev_*` drain and cleared at the bottom, rather than by
threading a device through `keyboard_target` and the seven
`input_route_target` call sites — functions SDL also calls, which would all
pass nothing. A field is honest here because the input path is single-threaded
and synchronous: a device's events are fully dispatched before the next
device's drain begins. The discipline that makes it safe is that no route
resolution may run while the field holds a device from a previous drain, which
is why it is also set and cleared around the `POST /v1/input/inject` body,
including on every refusal path inside it.

**A keyboard's layout and its modifier state are its own.** Both were one
global set. The layout being global made every keyboard US, which is issue
#35; the modifier state being global meant Shift held on a keyboard routed to
one session modified another keyboard's keys on their way to a different
session — a defect that existed before routing and that routing made
reachable.

`configuration.keymap` is a **sparse override** on a built-in table, by evdev
code, with four levels: plain, shift, altgr, shift+altgr. Sparse because a
request is bounded at `BLIT_REQUEST_SIZE` and a complete table does not fit —
and a parser that accepted the prefix of one that did not fit would hand a
controller a keyboard it believed it had configured; the whole of a layout
difference is a few dozen keys. An empty level means "not defined here", so
AltGr on a key with no third level falls back to the unmodified one, which is
what AltGr does on a US keyboard where it selects nothing; the built-in table
has no third level at all and behaves the same way, so there is one rule
rather than one per table. The override is the only way to reach the third
level, which is what makes `evdev_altgr` — tracked since 0.1.42x and doing
nothing — finally mean something.

There is deliberately **no named-keymap resource**. A `PUT /v1/input/keymaps/
{name}` collection would be a second resource with its own identity,
lifecycle and persistence question, and the per-device object already fits the
request bound. Configuration is controller-persisted (above), so holding the
layout is the control panel's job; two keyboards wanting one layout is two
PATCHes.

Not decided here, and named so it is not mistaken for an oversight: reading
the **kernel console keymap** through `KDGKBENT` on `/dev/tty0` would give
every layout the operator has configured with `loadkeys`, with no new
dependency, at the cost of decoding the `K_*` types. It is deferred because
there is no tty in the environment the gates run in, so it could only be
exercised on the live host — and a host-only path is exactly what let a gate
spend seven releases testing a stale binary (#59). **Dead keys and compose
sequences are out of scope**: a table of characters cannot express them, and a
stateful translator is a different feature.

Modifier state moved into the device with the layout, except for one thing:
the modifiers a **mouse report** carries are OR'd across every present
keyboard. Per-device state is right for a key, because a key belongs to the
keyboard that produced it; it is wrong for a click, because there is one
cursor, so a shift-click means "shift held anywhere" — which is what it meant
when the state was global.

Two things routing does **not** change, stated because a control-panel author
would otherwise assume otherwise:

- **The cursor is global.** Routing a pointer sends that device's buttons and
  reports to its target regardless of where the cursor is, and two routed
  pointers still move one cursor. That is what the aggregate routes already
  do; a cursor per device is a different feature. The cell coordinates in a
  routed report are therefore those of the surface *under the cursor*, clamped
  to the first cell when the cursor is over no surface — so a routed pointer
  reports its target's buttons with the geometry of wherever the cursor
  happens to be. Also pre-existing, also stated rather than changed: changing
  it would change the aggregate routes' meaning too.
- **Modifier state was global across keyboards.** Shift held on a keyboard
  routed to session 1 modified another keyboard's keys on their way to
  session 2. It predated routing — `evdev_shift` and `evdev_key_held` were one
  set for all keyboards — and routing only made it observable. Fixed in stage
  4, above.

**Collection revisions become real.** `input_devices_revision` increments on
every discovery, loss and accepted PATCH, and the change is announced on
`/v1/stream` as a `device_changed` event alongside the existing
`input_device_reconnected` / `input_device_lost` diagnostics.

**An audio device is one PCM in one direction, not one card.** The inventory
this replaced listed a card with a direction of `duplex` when it had both. The
live host has six cards and nine PCMs, four of them HDMI outputs on one card —
and choosing between "HDMI 0" and "HDMI 3" is the whole of choosing an output,
so per-card granularity cannot express the thing a control panel exists to do.
The id is `audio-<card-id>-pcm<device>-<p|c>`, with the card component from
`/proc/asound/card<N>/id`, which ALSA keeps stable across reboots — unlike the
card *number*, which is assignment order. Formats, channels and rates come
from the card's stream description, read **per direction**: a USB card's
`stream0` has separate `Playback:` and `Capture:` blocks whose formats differ,
so taking the first match in the file reports the playback format for a
capture device. A card that publishes no stream description reports none,
rather than the `s16le` the old inventory asserted for everything. Rates are a
comma-separated line — the live host's QuadCast publishes seven, from 8 kHz to
48 kHz — so every one of them is reported; reading the first integer said the
device supported 8 kHz and nothing else, which is a claim about the hardware
rather than a shortcut, and it was the deployed inventory that showed it
rather than the gate, whose first fixture had a single rate.

**Audio configuration is the choice of default, and nothing more.** Blit has
no audio engine: it cannot route a stream, mix, or change a level.
`configuration.default` marks a PCM as the default for its direction —
playback and capture independently — and is honoured by exporting
`ALSA_CARD`, `ALSA_PCM_CARD`, `ALSA_PCM_DEVICE` and
`BLIT_AUDIO_PLAYBACK`/`BLIT_AUDIO_CAPTURE` into the environment of sessions
launched **after** the write. `execve` means it cannot reach a session already
running, so a controller that wants a change to apply relaunches the session.
All three ALSA variables are exported, because libasound reads `ALSA_CARD` as
a card and `ALSA_PCM_CARD`/`ALSA_PCM_DEVICE` as the override for its `default`
PCM — an application that honours only `ALSA_CARD` lands on device 0 of the
card, which for a chosen HDMI output other than the first is not the device
that was chosen. That is a property of the applications, not of the choice,
and the schema says it.

The audio inventory watches its device-node root with inotify, the way the
input scan does, so a card plugged in while Blit runs appears and one unplugged
stops reporting itself present. The watch is on the device nodes rather than
procfs because inotify does not report changes under `/proc` — and for the
same reason **a PCM's device node, not its procfs entry, is the authority on
whether it exists**. Testing procfs made presence and the watch disagree: a
rescan triggered by the node disappearing would still find the `/proc` tree
and keep the device present, and no later event could correct it, because the
only thing left to change was under `/proc`. The nodes under `/dev/snd` on the
live host are exactly the twelve PCMs `/proc` describes, so nothing is lost by
making the node authoritative. A node the server cannot open is still listed,
with `state.accessible` false, so a permission problem reads as one rather
than as a missing device. A device chosen
as a default keeps its entry while absent, for the same reason a configured
input device does.
Volume, mute and any other mixer control are **not** here and are not implied:
they need libasound, which changes `tools/build-tcc.sh` and the
`packaging/build-deb.sh` dependencies and is a toolchain decision with its own
ADR.

**A display has no writable property, and that is the decision.** Selecting a
mode changes the render output size and therefore every surface on the
display, and a Hibr desktop's cells come from its owner
([ADR 0063](0063-a-desktop-cells-come-from-its-owner.md)), so Blit cannot
repaint one at a new size by itself — it needs a resize protocol with the
owner, which does not exist. The modes are reported so a controller can show
them; the write that applies one is its own ticket. A `mode` property that
accepted a value and did nothing would be worse than its absence. The KMS
backend cannot be validated at all, and a mode change on an unattended screen
that fails to come back is unrecoverable over the API — two independent
reasons, either sufficient.

## Consequences

- A control panel can enumerate, inspect and configure one device at a time,
  and can cache configuration against an id that survives a server restart.
- `GET /v1/input/devices` stops opening 32 nodes per request, which removes
  the #16 stall from the request path. The stall remains possible in the scan
  itself; moving the scan off the event-loop thread stays #16's job.
- Per-device counters move from a diagnostics array keyed by path to the
  device's own `state`, closing #17. The diagnostics arrays stay for one
  release for compatibility and are then removed.
- The `InputRoute.device_id` enum stays valid; its three values become
  *aggregate* routes, documented as the fallback.
- Serialised output per device grows by roughly a factor of four. Every
  `*_json(body, sizeof(body))` helper in the server breaks out of its loop on
  overflow and emits a truncated-but-closed object, which silently drops
  devices. The device collections therefore get an explicit capacity check and
  the gate asserts the serialised device count equals
  `resources.keyboards + resources.pointers + resources.gamepads`.
- Risk accepted: the gate environment has no `/dev/input` and no `/dev/uinput`,
  so neither hotplug nor id derivation can be exercised against real hardware
  locally. Id derivation is therefore written as a pure function over
  `struct input_id` plus the two strings, in a header the server and a C probe
  both include, and is gated as a pure function. Identity *stability* is
  evidenced on the live host by comparing ids across a server restart.

## Staging

1. **Identity and the table.** `BlitInputDevice`, derivation, serve the
   inventory from memory, per-device `state`, `GET /v1/input/devices/{id}`,
   real `input_devices_revision`. *Done, 0.1.432/0.1.433.*
2. **Input configuration.** `PATCH /v1/input/devices/{id}`: `enabled`,
   pointer `sensitivity_percent` and `natural_scroll`. *Done, 0.1.434.*
3. **Per-device routing.** `configuration.route`. *Done, 0.1.435.*
4. **Keyboard layout and per-keyboard modifier state** (#35).
   *Done, 0.1.441.*
5. **Audio.** Real per-PCM capabilities, device items, default selection.
   *Done, 0.1.437.*
6. **Displays.** Item endpoint and the envelope; no writable property.
   *Done, 0.1.437.* Mode selection is its own ticket, because it needs a
   resize protocol with the surface owner.

## Evidence

- `tools/tests/input_identity_gate.c` / `.sh` — derivation as a pure function:
  three nodes sharing one `input_id` get three ids, a serial wins over a
  physical path, an empty identity block falls back to the node, five nodes
  sharing one `phys` are separated by name, and two nodes identical in every
  reported field take an ordinal. Every fixture is a device the live host
  actually presents. Teeth: with the `phys` disambiguator removed the
  three-node case collapses to one id; with the name discriminator removed the
  shared-`phys` case collapses and the ordinal becomes the first resort.
- `tools/tests/input_device_api_gate.rb` — the envelope, the item endpoint,
  the real collection revision, the serialised count against
  `/v1/diagnostics/runtime`, and that a `GET /v1/input/devices` adds no
  `evdev_scan_opens`. Its devices are fabricated: regular files named
  `event0..` under `BLIT_INPUT_ROOT`, on which every evdev ioctl fails, which
  is exactly the "device with no identity" path. Teeth: (a) a build that
  rescans before serving the collection fails the open-count assertion with
  32 opens; (b) a build that emits the pre-change item shape fails the
  envelope, the id and the hardcoded-revision assertions.
- `tools/tests/input_device_config_gate.rb` — stage 2. The PATCH write and
  both revision bumps; `409` on a stale `If-Match` and on a stale body
  `revision`; `400` naming an unknown property; `422` for a pointer-only
  property on a device that is not read as a pointer; that `configuration` is
  a bounded object and a property outside it does not leak in through a
  forward search; `409` for an injection through a disabled device; hotplug
  through inotify under `BLIT_INPUT_ROOT`, including that a configured device
  survives being unplugged as `present: false` and comes back still
  configured while an unconfigured one is retired; and the `device_changed`
  stream event. Teeth, each shown: retiring a configured absent device loses
  the configuration and the replug comes back enabled; reading
  `configuration` by forward search lets `enabled` outside the object
  re-enable the device; and accepting an unknown property answers 200.

- `tools/tests/input_device_route_gate.rb` — stage 3, and the first input
  test that runs without hardware at all: two shells, two devices, each routed
  to one shell, a letter injected through each with its `device_id` and read
  back from that shell's own surface. Also the 400 for a route to a session
  that does not exist, the 400 naming an unknown route property, that a
  disabled route falls through without delivering, and that a routed device
  survives being unplugged. Teeth: with the per-device resolution removed from
  `input_route_target`, both letters land in the same session.

- `tools/tests/audio_device_api_gate.rb` — three PCMs on one card, two of them
  playback, which is the case per-card granularity could not express; the
  envelope; the item endpoint; per-direction formats, where the fixture's
  `stream0` gives Playback and Capture different ones so a parser that takes
  the first match in the file is caught; one default per direction, chosen
  independently; the 400 naming an unknown property; the 409 on a stale
  revision; and that the chosen default reaches a session launched after the
  write, read out of that session's own surface. Its devices are fabricated
  under `BLIT_ASOUND_ROOT`/`BLIT_SND_ROOT`, because this environment has
  neither `/proc/asound` nor `/dev/snd`. Teeth: reading the format from the
  Playback section regardless of direction gives the capture device the
  playback format; a single default across both directions makes choosing a
  microphone clear the chosen output.
- `tools/tests/display_device_api_gate.rb` — the envelope, the item endpoint
  agreeing with the collection, that `configuration` is empty and a PATCH is
  refused rather than quietly accepted, and that the reported bounds are the
  bounds. Teeth: the pre-change item shape fails the envelope.

- `tools/tests/key_encoding_gate.rb` — stage 4, on top of its 119 encoding
  cases. Two fabricated keyboards: a QWERTZ override on one (Z and Y
  exchanged, ß where the hyphen is, AltGr and shift+AltGr on Q) types its
  layout, while the same codes on the other keyboard type US — which is the
  assertion that a layout belongs to a device and not to the server. AltGr on
  a key with no third level falls back to the unmodified one. Shift held on
  one keyboard does not modify the other's key, and does modify its own;
  CapsLock likewise. Teeth: with the translator reading one global state, the
  layout assertions fail and AltGr types the plain character; with the held
  set pooled across devices but the layout per device, the cross-keyboard
  Shift and CapsLock assertions fail and nothing else does — the two halves of
  the stage are caught separately.

- **Unexercised, and recorded rather than implied:** the full-table fallback,
  `default_keyboard_state`, which a key uses when its device has no table
  entry. Reaching it needs 33 devices under `BLIT_INPUT_ROOT` and nothing
  does. Audio's `state.accessible` is likewise always true, here and on the
  host, because Blit runs as root and the fixture's nodes are ordinary files;
  the field exists so a device Blit cannot open reads as a permission problem
  rather than as a missing device, and that case has never been produced.

  **Not gated locally, and not claimed to be:** that disabling a device stops
  it being polled, and that `sensitivity_percent` scales motion. Both need a
  device the server classifies as a pointer, which needs real evdev ioctls;
  the gate environment has no `/dev/input` and no `/dev/uinput`, so its
  fabricated nodes are devices of no kind. Both are measured on
  192.168.10.54, which has three real pointers, through
  `POST /v1/input/inject` with a `device_id`.
- Live: ids recorded from 192.168.10.54 before and after a server restart,
  reported in the stage-1 ticket.
