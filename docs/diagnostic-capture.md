# Live diagnostic capture

The real-host capture harness records Blit's API state without depending on a
local Hibr shell or a physical keyboard. It collects the hardware/topology
inventory, runtime counters, bounded event trace, and synchronized framebuffer
screenshots.

Run it from a machine that can reach Blit:

```sh
tools/diagnostics/capture-live.sh http://192.168.10.54:8080 20
```

The output directory contains:

- `trace-*.json`: input events, surface presentation boundaries, and renderer
  path decisions;
- `runtime-*.json`: frame timing, input counters, stream health, and session
  resources;
- `screenshots/*.bmp` and `screenshot-sha256.txt`: framebuffer evidence;
- the start/end hardware and topology snapshots.

Interpretation is causal: a physical click must appear as `pointer_button`,
the associated Hibr surface activity must follow a `commit`, and the renderer
must report the path used for the resulting presentation. Missing events or a
hash change without a corresponding commit are distinct, actionable failures.
