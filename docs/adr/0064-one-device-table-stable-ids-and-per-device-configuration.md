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
flag. `input_route_target()` resolves the device's own route first and falls
back to the aggregate `evdev-keyboard` / `evdev-pointer` / `evdev-gamepad`
route, so existing clients keep working and a control panel can override one
device at a time. `configuration.enabled: false` removes the device from the
poll set entirely.

**Collection revisions become real.** `input_devices_revision` increments on
every discovery, loss and accepted PATCH, and the change is announced on
`/v1/stream` as a `device_changed` event alongside the existing
`input_device_reconnected` / `input_device_lost` diagnostics.

**Audio and display configuration are staged, and the ADR does not promise
what Blit cannot do.** Blit has no audio engine; it advertises
`audio-inventory` and enumerates ALSA cards by `access()` on
`/dev/snd/*`, hardcoding `s16le`. Real mixer control means linking libasound,
which is a packaging and toolchain decision (`tools/build-tcc.sh` must link
it, `packaging/build-deb.sh` must depend on it) and is deliberately **not**
decided here. Stage 3 delivers honest capabilities read from
`/proc/asound/cardN` and a Blit-level default-sink selection exported to
launched sessions; a mixer requires its own ADR. Display mode-setting is
reachable only through SDL (`SDL_SetWindowFullscreenMode`); the KMS backend in
`src/blit_kms.c` is exercised by nothing today, so stage 4 configuration is
specified for the SDL path and left unimplemented for KMS.

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
3. **Per-device routing.** `configuration.route`, which threads the device
   through `keyboard_target` and the seven `input_route_target` call sites.
   Deferred from stage 2 deliberately: the aggregate `evdev-keyboard` /
   `evdev-pointer` / `evdev-gamepad` strings are also a stream contract, so
   the change is larger than the rest of the configuration put together.
4. **Keyboard layout** (#35). A keymap that is not hard-coded US.
5. **Audio.** Real capabilities, device items, default-sink selection.
6. **Displays.** Item endpoints and SDL mode-set configuration.

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

  **Not gated locally, and not claimed to be:** that disabling a device stops
  it being polled, and that `sensitivity_percent` scales motion. Both need a
  device the server classifies as a pointer, which needs real evdev ioctls;
  the gate environment has no `/dev/input` and no `/dev/uinput`, so its
  fabricated nodes are devices of no kind. Both are measured on
  192.168.10.54, which has three real pointers, through
  `POST /v1/input/inject` with a `device_id`.
- Live: ids recorded from 192.168.10.54 before and after a server restart,
  reported in the stage-1 ticket.
