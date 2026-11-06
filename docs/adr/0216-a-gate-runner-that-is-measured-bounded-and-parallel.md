# ADR 0216: A gate runner that is measured, bounded and parallel

- Status: accepted
- Date: 2026-11-05
- Scope: `tools/run-gates.sh`, `tools/tests/gate_port.rb`,
  `tools/tests/gate_port.sh`, `tools/tests/gate_with_server.sh`,
  `tools/tests/run_gates_gate.rb`, the gates that owned a port or a file the
  others could also touch, `docs/development-workflow.md`
- Builds on: issue #28 (a gate waits for its port and names a conflict), ADR
  0184 (native KMS on the DRM mock is the one path)

## Context

The owner's complaint was that the build process needs serious optimisation.
The build takes seconds and the package about a minute. The cost is the release
test matrix: `tools/run-gates.sh` runs about 180 gates strictly one after
another, with no per-gate time, no per-gate bound, and ports that make a second
run impossible:

- every gate that starts a server owns a fixed port (18995..19900) and waits
  15 s for it, so two suites, two checkouts or two agents on one machine
  collide;
- a hung gate (`output_occupancy_gate` once ran for 1 h 13 m) had no timeout;
- nothing said where the time went, so "optimise" had no target;
- the release had been run as `font`, `all`, then the renderer on GLES, and
  `all` already contains `font`.

Parallelism must not buy speed with dishonesty. Several gates measure the
machine (a frame rate, a cadence, "saved within N ms") and fail when it is
busy. A retry hides that; it does not fix it.

## Decision

**Measured.** `run_gate` times every gate. The report line keeps its format and
gains the wall time at the end. The run ends with the 20 slowest gates, the
time per suite, and the wall time against the sum of the gate times, before the
unchanged `RUN_GATES_PASS`/`RUN_GATES_FAIL` line.

**Bounded.** A gate runs under `timeout -k 10 $BLIT_GATE_TIMEOUT` (default
300 s; `terminal_soak_gate` gets its duration plus 300, the package gate 1200,
and the three gates that wait on a production period 600). `timeout` puts the
gate in its own process group and signals the group, so the gate's
`blit-server` goes with it. A gate over its limit is a failure reported
`FAIL rc=124 TIMEOUT after Ns`; its whole output is kept as for any failure.
It is never retried. The gate runs in the background and the runner waits for
it, so an interrupt of the runner takes the gate down as well.

**Ports.** `BLIT_GATE_PORT_OFFSET` is added to every fixed gate port by the Ruby
and shell `gate_port` helpers. Every gate now chooses its port through them
(seven that read `BLIT_GATE_PORT` or a literal directly, the diagnostic auth
gate's pid-derived port and the frame descriptor gate did not). The fixed ports
now fit in 18995..19900, so the runner gives each parallel worker an offset
stride of 1000 and refuses an offset that would reach the ephemeral range
(32768). Agents and checkouts running gates at the same time take 0, 4000, 8000.
The shell helper also detected nothing under dash (`/dev/tcp` is a bash feature):
it now asks `ss`.

**Other shared state.** Two gates compiled `font-raster-kms` to the same path
and run twice in the font suite; each run now owns a temporary directory. Five
gates replaced each other's `build/blit/desktop` symlink (the server keys on
the basename `desktop`); each now fakes it in a private directory. Eight fixed
`/tmp/blit-*.log` files are per process. `terminal_conformance.rb` and the soak
used a server the runner started inline; `gate_with_server.sh` now owns that
server for one command, so each is a single bounded gate.

**Parallel, and honest about load.** `BLIT_GATE_JOBS` (default `min(4,
nproc/2)`) workers walk the suites. Each task is numbered in call order, claimed
with an atomic `mkdir`, and run by whichever worker gets it first; the
coordinator reports results in suite order as they arrive, so the report reads
like a serial one. Gates that need a quiet machine are marked `run_serial_gate`
and run in the timing lane: one worker, alone, after every parallel gate has
finished. They are `performance_baseline_gate`, `pointer_cadence_observation_gate`,
`frame_rate_gate`, `terminal_soak_gate`, `kms_frame_timing_gate`,
`presentation_effects_gate` and `graphics_blink_gate` (frame counts and rates,
failed when four gates ran beside them on GLES),
`evdev_open_latency_gate`, `settings_persistence_gate` ("auxiliary policy was not
durably saved" under load), `output_occupancy_gate` and
`restart_after_update_gate`. They stay in their own suites, so `renderer` on its
own still covers what it covered. `BLIT_GATE_JOBS=1` is the previous serial
runner: same order, same output apart from the durations.

**Deduplicated.** `all` is every suite; the release is `all` then the renderer
on GLES, with no separate `font`. `renderer-gles` runs the renderer suite on
GLES without 21 gates the compositor cannot change (their own C probes, the two
hotplug gates that pin the CPU path, gates that open no display). `all` and
`renderer` still run every gate.

**The runner has a gate.** `run_gates_gate.rb` runs the runner on a fake suite
(`BLIT_GATE_FAKE_SUITE`): offsets do not collide and the same offset still does;
a hung gate that had started a real `blit-server` is reported TIMEOUT and leaves
no process and no listener; `JOBS=1` and `JOBS=4` report the same gates, order,
results and verdict line; the timing lane overlaps nothing.

## Consequences

- `all` took 23 minutes serially (1382 s and 1416 s) and takes 10 to 11.5
  minutes with four jobs (608, 639, 696 s; the same 177 pass, 6 skip); the
  GLES renderer pass went from 564 s to 175..255 s, and `renderer-gles` takes
  about 120..190 s; see `docs/development-workflow.md` for how to read the table.
- The longest single gates bound the parallel part
  (`emoji_presentation_gate` is 170..235 s on its own); the serial lane is the
  other half (the 120 s soak, the update/restart gates). The next savings are
  there: a smaller soak, and test-mode periods for the gates that wait a whole
  production period to prove "not re-probed" (`hibr_identity_refresh_gate`
  sleeps 11 s) or "no restart in 15 s" (`restart_after_update_gate`).
- A gate that fails only when other gates run beside it is a bug in the gate
  or the server, or moves to the timing lane in the open. There is no retry.
- Gates and logs that other checkouts could overwrite in `/tmp` are per process
  now; the settings document of a gate that does not set `BLIT_SETTINGS_PATH` is
  still the host's `/var/lib/blit/settings.json` (read, not written, in
  practice) and is the next shared state to remove.

## Verification

`ruby tools/tests/run_gates_gate.rb` (schema suite), and `tools/run-gates.sh all`
with `BLIT_GATE_JOBS=4` and `=1` giving the same set of results.
