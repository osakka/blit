# Development and verification tools

The files in this directory are not the Blit runtime and are not required to
run an installed server. They are reproducible developer tooling:

- `tools/probes/` contains QEMU init scripts, staging helpers, input emitters,
  and small protocol probes used by the acceptance gates.
- `tools/tests/` contains repeatable contract and network tests.
- `packaging/build-deb.sh` is a build-time packaging script.
- `tools/diagnostics/http.sh` is the shared authenticated read-only transport
  for live captures. `capture-live.sh` is the canonical evidence collector;
  the experience, troubleshooting, pointer, and input tools use its transport
  rather than carrying separate token handling.
- `tools/diagnostics/validate-capture.py` is the completeness check for the
  canonical bundle; it verifies JSON, completion metadata, and screenshot
  capture headers.

The installed runtime is the TCC-built `blit-server` binary, exposed as
`/usr/bin/blit`. Hibr owns desktop lifecycle and is started with its own
installed `desktop` command. Blit does not install, require, or invoke a shell
launcher for Hibr.

The optional `hibr/console-blit.so` provider is a C module for Hibr programs
that explicitly load the provider. It is separate from desktop orchestration.

- `wake-and-capture.sh` is the bounded live login-manager diagnostic. It
  records before/after state and screenshots, injects one one-pixel pointer
  motion by default or one complete pointer click when explicitly passed
  `click`, and reports whether a surface or screenshot changed. It does not
  create sessions, send keys, or modify Hibr.

`host_motion_agreement.rb` is the live counterpart to the local
motion-trail gate. It creates a bounded static API surface, moves a known
block through it, and compares only coherent capture revisions. It cleans
up its own session and is not a Hibr desktop test.

`host_kitty_composition.rb` is the live direct-KMS graphics boundary probe. It
creates a bounded API surface, paints a known cell background, emits a known
2x2 RGB Kitty image from the child PTY, captures exact image and surrounding
cell pixels, emits the Kitty image delete, and verifies that the old image
pixels return to the cell background. It is not a Hibr desktop or physical
input test. Both live probes discover the output and pane geometry from
`/v1/topology`; direct-KMS screenshots are decoded from their advertised BMP
bit depth.

`host_render_agreement.rb` now compares revision-matched blank and solid/half
block cell colours against scanout pixels. It refuses retained-graphics
surfaces and excludes unsupported styles, visible terminal cursor and a
bounded software-pointer neighbourhood. Those exclusions are not acceptance
claims. Set `BLIT_AUTH_TOKEN`; pass host:port and optional surface/attempts.
The legacy positional token remains supported. A moving revision or changing
pointer state across capture discards the sample; it is not reported as a
renderer failure. Pointer exclusions are refreshed for every attempt.

`host_utf8_stream.rb` reproduces issue #94 using a real PTY: a split UTF-8
lead followed by a shell write larger than 4 KiB must retain its half block,
4000 text cells and final ANSI-positioned marker. It requires a free pane,
restores original focus, and removes only its own temporary session. Run with
`BLIT_AUTH_TOKEN` set; optionally set `BLIT_LIVE` and `BLIT_EVIDENCE_DIR`.

`host_damage_union.rb` reproduces issue #89 through the live API, writing the
bottom-right cell before the top-left cell and verifying both reached scanout.
It also checks exact upper/lower half-block foreground/background colors.

`host_pty_pointer_stress.rb` runs a genuine PTY animation alongside pointer
traffic, waits for its final frame, checks every visible blank cell pixel, and
compares completed flips with DRM vblank sequences during the active workload.
It restores focus/pointer and deletes only its temporary shell session. A free
pane is required; it does not start Hibr or change backend/topology.

```sh
export BLIT_LIVE=http://192.168.10.54:8080
export BLIT_AUTH_TOKEN='<API token>'
ruby tools/probes/host_damage_union.rb
BLIT_REQUIRE_REFRESH=1 BLIT_STRESS_FRAMES=6000 \
  BLIT_EVIDENCE_DIR=diagnostics/pty-pointer-soak \
  ruby tools/probes/host_pty_pointer_stress.rb
```

For direct KMS, refresh acceptance requires two distinct active clock samples
and **zero skipped vblanks**, independently of nominal mode rounding or clock
variation. Evidence includes the final BMP/state, runtime samples, render and
vblank traces, and a machine-readable scanout verdict. Pixel and refresh checks
are separate. These are renderer tests, not authenticated Hibr desktop tests.
