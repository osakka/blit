# Backend comparison on the physical host

> **Superseded by [ADR 0184](../adr/0184-sdl-is-removed.md).** SDL is removed and native KMS is the only presentation path, so there is no backend parity to track. Kept as a historical record; tools it names may no longer exist.

Use `tools/probes/host_backend_comparison.rb` after installing0.1.556 or later.
This is an operator comparison that restarts Blit and ends its current
terminal sessions. Run it during a dedicated test window without another
configuration writer. It requires a ready direct-KMS baseline and a working
authenticated API. It never uses SSH or modifies Hibr code.

```sh
BLIT_LIVE=http://192.168.10.54:8080 \
BLIT_AUTH_TOKEN="$BLIT_AUTH_TOKEN" \
BLIT_EVIDENCE_DIR=/tmp/blit-backend-comparison \
  ruby tools/probes/host_backend_comparison.rb
```

The tool records the original settings and observation, temporarily disables
startup login, and selects direct KMS, SDL/KMSDRM software and SDL/KMSDRM
OpenGLES2 through the saved settings API. It waits for a new process-start
signature, readiness and exact backend/video-driver/renderer identity before
running the existing revision-correlated motion and Kitty composition probes.
Before each profile's probes it parks the pointer outside the checked image
region and verifies its position/dirty state. The original pointer position
is restored with the original configuration at the end. Pixel checks are not
masked to accept an overlapping cursor.
Motion captures check blank cells for debris; Kitty captures check quadrants,
untouched background and deletion. The probes use actual BMP offset, stride,
depth and orientation. First/last motion and Kitty captures are retained with
revision metadata. A third probe checks complete edge cells and geometric
glyphs against the canonical8x16 font raster, including the bold diamond;
this comparison profile requires that font at scale1. Auth tokens are supplied through the environment and are
not written into evidence.

The same comparison also runs `host_sixel.rb`. Its decoder scenarios retain
their independent surfaces; its pixel scenario explicitly selects a free
pane and samples relative to that pane's actual cell-grid origin. Captures
must match the surface revision and presentation state. Palette, HLS, filled
background, cursor modes and 4096-pixel decoder-bound assertions remain
shared between local and physical runs. Evidence includes the pixel capture
and revision/geometry metadata. `BLIT_SIXEL_PANE_ID` optionally selects a
specific free pane. An occupied pane is never overwritten.

`sixel_composition_gate.rb` starts an isolated SDL profile, splits the output
1x2, occupies the upper pane and runs that same operator probe on the lower
pane. This checks a nonzero vertical origin on software and offscreen GLES2;
it does not establish physical hotplug or performance. No new runtime or
second sixel implementation is introduced.

An `ensure` restores only the backend, renderer and login-enabled settings
changed by the comparison, preserving other settings, then restarts and
verifies the original backend. `BACKEND_BASELINE_RESTORED` confirms that
boundary. The cleanup gate injects a failed trial and verifies restoration
and retention of unrelated settings. If the API becomes unreachable, the
tool cannot promise recovery through that same API; its error remains a
failed comparison rather than a successful acceptance claim.

`BACKEND_COMPARISON_PASS` accepts these bounded correctness probes on the
reported hardware/configuration only. It does not establish60/120/240Hz
performance, physical button/keyboard delivery, hotplug or the complete parity
matrix. The next measurement stage must use the common workload and per-output
cadence/latency requirements in the [parity plan](backend-parity-plan.md).

For exploratory pointer cadence, set `BLIT_COMPARISON_POINTER_CADENCE=1`.
After each profile's pixel probes, the existing pointer workload offers
10 seconds of 12-pixel sweeps at that output's advertised refresh, using
concurrent HTTP requests; `BLIT_POINTER_SECONDS` changes the bounded duration.
`BLIT_POINTER_HZ` explicitly overrides the offered rate (1..240 requests/s),
allowing an above-refresh producer to distinguish sparse/bunched input from
the output's ability to service continuously pending pointer work. This never
changes the display mode or Blit's configured presentation target.
The tool retains raw observations and per-output submission rates separately
from available native flip completions. This is diagnostic output, not a
cadence pass/fail gate: request timing/order, the final restoration motion
and unavailable SDL completion evidence remain explicit. It neither claims
physical-input delivery nor replaces the longer acceptance workloads.

## Dependency boundaries checked

Issue105's admission regression is
`tools/tests/http_accept_budget_gate.rb`. It uses the actual server/selected
SDL profile and injects a16ms delay only at public `SDL_RenderPresent`.
Eight complete requests are queued while present is held, then released;
the test distinguishes bounded admission from one-refresh-per-connection
waiting and checks a subsequent under-filled turn. Software and offscreen
GLES2 use the same assertions. This is an event-loop regression, not a
physical refresh or GPU latency measurement. The production-code socket
fixture separately checks the accept limit, capacity and descriptor lifetime.

The bundled SDL3.4.16 event pump handles input but does not reconcile runtime
connector changes. The inspected [upstream event pump](https://github.com/libsdl-org/SDL/blob/main/src/video/kmsdrm/SDL_kmsdrmevents.c)
also only polls input; upgrading alone is not established as a hotplug fix.
SDL's [presentation documentation](https://wiki.libsdl.org/SDL3/SDL_RenderPresent)
requires main-thread use and treats the window backbuffer as invalid after
presentation. Output independence must therefore be measured and implemented
within SDL's ownership rules; moving its renderer calls to arbitrary workers
is not the proposed fix. ADR0129 tracks the connector lifecycle dependency.
