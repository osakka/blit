# ADR 0067: settings are Blit's own state, written through the API

- Status: accepted
- Date: 2026-10-11
- Scope: device and presentation configuration, its persistence, and the
  boundary between configuration and the settings that gate the API

## Context

ADR 0064 gave Blit one device table, ids derived from what a device reports
about itself, and a per-device `configuration` object written by `PATCH`. It
deferred one thing in as many words:

> **Configuration is held in memory.** A Blit restart resets every device to
> the defaults and a controller re-applies what it wants; the collection
> revision dropping back toward 1 is the signal that it has to. Persisting
> configuration across a restart is a separate decision, not taken here.

That deferral has since been costed. Issue #63 measured what a supervised
update does: the unit stops and starts, every session is gone, and the server
comes up with a fresh table. So a pointer speed, a keyboard layout, a
natural-scroll choice and a default audio device are lost on **every release**
— and the recovery assumes a control panel exists, is running, is
authenticated, and gets there before a person touches the machine. At boot
none of that is true. "A controller re-applies what it wants" is a reasonable
contract between two live processes and not a reasonable answer to "what is
this machine's mouse speed".

The configurable surface is also thinner than the model implies, which is only
visible when you ask the live host. On 192.168.10.54 the writable device
properties are `enabled`, `sensitivity_percent` and `natural_scroll`, plus the
per-device keymap and route:

```
28 input devices: 4 keyboard, 3 pointer, 21 other
configuration keys seen: ['enabled', 'natural_scroll', 'sensitivity_percent']
```

The four keyboards report `ev=0x120013` — bit 20, `EV_REP` — so the kernel
autorepeats for them and the rate and delay are per-device kernel settings
(`EVIOCGREP`/`EVIOCSREP`) that Blit does not touch. The three pointers have no
button map, and `sensitivity_percent` is the only curve there is. The twelve
audio PCMs expose `default` and nothing else: no volume, no mute. A display's
`configuration` is empty, which ADR 0064 recorded as a decision rather than an
omission because applying a mode needs a resize protocol with the surface
owner (issue #60).

And a third class of setting is not in the API at all. These are read from the
environment once, at boot, and a control panel cannot see or change them:
`BLIT_RENDER_INTERVAL_MS`, `BLIT_PANE_LAYOUT`, `BLIT_RENDERER`,
`BLIT_KMS_ASYNC_PAGEFLIP`, `BLIT_EVDEV_MOUSE`, `BLIT_EXPECT_DESKTOP`.

So there are three questions, and they are one question: what is a setting,
where does it live, and who may change it.

## Options considered

1. **Leave it controller-persisted** — ADR 0064's deferral, made permanent.
   Benefit: no state in Blit, nothing to corrupt, nothing to migrate, and the
   control panel already has to hold a layout it sent because the device item
   does not echo the levels back. Costs: the machine has no settings of its
   own. Boot and the window after every update show defaults. Two controllers
   disagree with no tiebreak. An operator with no control panel has no way to
   set a mouse speed at all, which makes "API-driven" mean "unusable without
   our client".
2. **An operator-edited file under `/etc/blit/`, read at boot, API read-only.**
   Benefit: the familiar Unix shape, and `/etc/blit/blit.env` already exists.
   Costs: it makes the API a reporting surface for the one thing a control
   panel exists to do. And the moment the API can also write it, Blit and an
   operator's editor are fighting over the same bytes — comments and ordering
   are destroyed on rewrite, and an edit made while the server is running is
   silently overwritten. A file that is both authored and generated is a file
   with no owner.
3. **A Blit-owned state document, written by the API, exposed as a resource.**
   Blit holds the settings, writes them when it accepts a write, reads them at
   boot, and publishes the whole document on a path so that backup, restore
   and provisioning are API operations. Costs: Blit now has persistent state
   it can corrupt, a bad setting survives a restart, and there is a schema to
   version. Benefit: the machine has settings, and where they are kept is an
   implementation detail rather than a contract.

## Decision

Option 3.

**The document.** `/var/lib/blit/settings.json`: daemon-written state, so
`/var/lib` and not `/etc`. The unit gains `StateDirectory=blit`, which is the
only correct way to get a directory owned by whoever the unit runs as —
measured, because today `/var/lib/blit` is `root:root drwxr-xr-x` and the
`blit` user of the unprivileged profile (ADR 0065) is in group `blit` alone
and could not write there. It carries a `version`, and unknown keys are
preserved across a rewrite so that running an older release does not eat a
newer one's settings.

**Keyed by stable id, never by node.** ADR 0064 already says "Configuration
should be saved against this, never against `node`", and this is where that
sentence acquires teeth. The corollary is new and load-bearing: configuration
against an `input-node-<basename>` id — the form ADR 0064 gives a device that
reports no identity — is **never persisted**, and the device document says so.
That id does not survive a restart, so restoring against it would apply one
device's settings to whatever took its place.

**A route is not persisted.** A device's `route` names a target as
`session-N`, and a session id does not survive a restart — it is as unstable
as the `input-node-<basename>` ids above, and for the same reason. Restoring a
route would either point a device at a session that no longer exists or, worse,
at a different session that happens to have been given the same number. ADR
0064 already requires an enabled route to have a live target, so a restored
route could never be enabled at boot in any case. Routing is a live
arrangement between a device and a running session, and it stays the
controller's to make; the device item says the route is not persisted so that
its absence after a restart is documented rather than discovered.

**An absent device keeps its stored configuration**, consistent with ADR
0064's in-memory rule: unplugging a keyboard and plugging it back in restores
its layout. Stored configuration that contradicts the device's *present* kinds
lies dormant rather than being dropped — a pointer property stored for a
device the kernel now reports as a keyboard is not applied and not deleted,
because the contradiction is far more likely to be a kernel re-probe than a
controller mistake.

**Precedence, stated once and in one direction.** A stored value wins. An
environment variable is the boot default **only where the document is silent**.
An accepted API write stores. The trap this creates has to be named rather than
discovered: once a setting has been written through the API, editing
`blit.env` for that setting changes nothing, and the way back is
`DELETE /v1/settings` or the per-device reset. Anything else — env winning, or
last-writer-wins between two sources — produces a machine whose behaviour
depends on which of two files was touched more recently.

**With one qualification, for a setting that is applied once at startup.** A
device property is validated against a device that is present, and a refusal
costs nothing. A setting like the renderer choice or async pageflip is
different: it is consumed while the server is coming up, and its failure mode
is that the server does not come up. "A stored value wins" would then mean a
document can prevent a boot, and `BLIT_SETTINGS=ignore` — an operator action
at the machine — would be the only lever for a setting that worked yesterday.

So a setting may be declared **startup-applied**, and for those the stored
value is tried first and the environment default is used if applying it fails,
with the fallback reported as a warning and in the setting's item. The
distinction belongs here rather than in the stage that needs it, because a
rule that is relaxed later for one setting is a rule nobody can rely on. No
setting in the first stage is startup-applied; the loader carries the category
from the beginning so that the stage which does need it (the renderer and
pageflip settings migrated off the environment) does not have to redefine the
precedence rule to get it.

**The document is a resource.**

    GET    /v1/settings     the whole document, with a revision
    PUT    /v1/settings     If-Match, replaces it
    DELETE /v1/settings     reset to defaults

A `PUT` that replaces the document bumps the revision of every affected device
and broadcasts the change, because a control panel watching one device must
learn that something else rewrote it. This is what makes the file an
implementation detail: an operator backs up a machine's settings, clones them
to another, or resets them, without touching the filesystem.

**Writes are coalesced, and flushed at teardown.** A slider dragging
`sensitivity_percent` must not be one `fsync` per pixel. The document is
marked dirty and flushed from the event loop at a bounded interval, and
unconditionally on the way out — which is one place now rather than three,
because issue #63 just made the exit paths converge: the API restart's exec,
the update install's exit, and `SIGTERM`.

**Recovery, for both failure shapes.** They are different and both are real.

- A **valid** setting that locks the machine out — `enabled: false` on the only
  keyboard, a sensitivity of 1% — is recoverable through the API with
  `DELETE /v1/settings`, and recoverable *without* the API by a boot-only
  escape, `BLIT_SETTINGS=ignore`, which starts from defaults and leaves the
  file alone. An API-only deployment whose only input device is disabled in a
  file it cannot stop reading has no way back in, and that is not an
  acceptable state to be able to reach.
- An **unparseable** document must never stop a boot. It is renamed to
  `settings.json.broken`, the server starts from defaults, and
  `/v1/diagnostics` carries a `settings_document_unreadable` warning — in the
  enumerated `warnings` list, so it surfaces where issue #55 taught this host
  to look rather than in a startup line nobody reads.

**Every setting is reachable through the API, with one exception, and the
exception is a decision.** The settings that gate the API itself stay
boot-only: `BLIT_AUTH_TOKEN`, `BLIT_BIND_ADDRESS`,
`BLIT_ALLOW_UNAUTHENTICATED_NETWORK`, `BLIT_COMMAND_ALLOWLIST`,
`BLIT_UPDATE_TOKEN` and `BLIT_UPDATE_TOKEN_FILE`. An authenticated client that
can rewrite authentication is precisely what ADR 0065 exists to prevent, and a
*persisted* setting that can disable authentication is one bug away from a
permanently open server that survives every restart. These are the operator's,
set with access to the machine, and `GET /v1/server`'s `listener` object
already reports the posture they produce so a control panel can show it
without being able to change it. Everything else that is a setting — device,
input, audio, display, presentation — gets an API path, and its environment
variable becomes its boot default.

## Consequences

The machine has settings. A keyboard layout, a pointer speed and a default
audio device survive a release, which is the whole point, and a bad one
survives a release too, which is the cost. That is why both recovery paths are
part of this decision rather than a follow-up.

`/var/lib/blit/settings.json` becomes something a deployment backs up. Because
the document is also a resource, the supported way to do that is
`GET /v1/settings`, and the file's format is free to change behind the
`version` field.

Two access facts are now dependencies rather than surprises, both measured on
the live host:

- Audio volume and mute mean the ALSA control device. `/dev/snd/controlC*` is
  `root:audio`, and the unit grants `video render input`. The unprivileged
  profile will need `audio`, and the root profile works only because root
  bypasses the group. Blit does not link libasound and should not start now:
  the control interface is reachable with raw `SNDRV_CTL_IOCTL_*` ioctls, the
  same way this codebase already speaks to evdev and DRM.
- Display power is not simply the connector's `dpms`. The live host runs the
  SDL KMSDRM backend (`direct_kms: false`), and SDL holds DRM master, so
  setting connector properties behind it is not available. Display power and
  rotation therefore depend on the direct-KMS path or on cooperation with SDL,
  and that is recorded as a dependency rather than promised. Mode selection
  remains issue #60 and is not re-decided here.

  *Measured since, in #69 (0.1.456), and it resolves this dependency in both
  directions.* Rotation turned out not to need DRM master at all: it is a
  render transform over a scene Blit already composes into a texture, so it is
  implemented on the SDL path and persisted here. Power is the other way round.
  `/sys/class/drm/card0-DP-1/dpms` is mode 444 and a root write returns
  `EACCES` on kernel 6.12, so sysfs is not an avenue on any backend — that is
  the kernel, not SDL — and `/sys/class/graphics/fb0/blank` is one device for
  the whole card and could not express a single display. What does exist is the
  cooperation branch this paragraph named: SDL's KMSDRM backend publishes its
  own master fd per window as `SDL_PROP_WINDOW_KMSDRM_DRM_FD_NUMBER`. 0.1.456
  reads the connector's DPMS state through it and reports it; setting it is the
  remainder of #69, and it carries a hazard worth writing down here, because
  it is the reason the read shipped first: SDL waits for its own pageflip
  events on that fd with `poll(..., -1)`, so a display turned off while a flip
  is outstanding is a hang of the only thread that serves the API.

Key repeat becomes a kernel setting Blit owns per device, via `EVIOCGREP` and
`EVIOCSREP` on the device's own descriptor. This is a real change in posture:
until now Blit has only ever *read* from an evdev descriptor. It is bounded —
the ioctl affects only the device Blit already holds open, and the autorepeat
events it produces are the `value == 2` events the key path already handles —
but it is the first write, and a device Blit has disabled must not be carrying
a rate Blit set.

`GET /v1/settings` is a new path in the OpenAPI contract and therefore in the
schema gate's count, and the performance and warning enumerations grow by one
entry.

## Evidence

This record was `proposed` while the model was reviewed, and is `accepted` on
`tools/tests/settings_persistence_gate.rb`, which asserts the decision rather
than the plumbing — eight scenarios, each one of the things above:

```
PASS settings-document-starts-empty
PASS a-write-is-stored-against-the-stable-id-only
PASS a-setting-survives-a-restart
PASS a-write-immediately-before-a-restart-is-not-lost
PASS the-document-is-a-resource
PASS an-unreadable-document-never-stops-a-boot
PASS a-locked-out-device-is-recoverable
PASS a-stored-property-the-device-contradicts-stays-dormant
SETTINGS_PERSISTENCE_PASS
```

A fabricated node is a regular file, so every identity ioctl fails and the
device reports nothing at all — which is exactly the unstable-id case this
record refuses to persist. The gate could therefore not have had a device with
a *stable* id, and so could not have tested the distinction that matters, so
`<node>.identity` beside a fabricated node now supplies what the ioctls would
have. It is read only when `BLIT_INPUT_ROOT` is set, so the seam cannot exist
in a deployment: a real `/dev/input` has no such files and it is not looked
for.

The dormant-entry scenario holds a refused entry **and** an accepted one, for
a reason worth recording: with only the refused entry, "the refused value was
not applied" is also true of a server that applies nothing at all, and the
scenario would have passed against the very build it exists to catch.

Teeth: against a binary built from this tree with the loading, saving and the
resource removed, all eight scenarios fail with 28 individual failures, and
the headline is the loss this record exists to stop —
`the stable device kept its setting, got 100`.

Suite: 53 pass, 5 skip, 0 fail. Host-accepted on 192.168.10.54 in the release
that carries this record.

Two decisions were taken during implementation and are above rather than
appended, because a record that grows a postscript is a record nobody reads to
the end: the startup-applied category, and that a route is not persisted. One
more is worth noting here because it refines a path this section describes:
under `BLIT_SETTINGS=ignore` a `PUT` is refused but a `DELETE` is allowed. An
operator who booted that way to get back in is exactly the person who needs to
clear the document that locked them out, and sending them to a shell for it
would leave the recovery half-finished.
