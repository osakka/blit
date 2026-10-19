# Backend parity acceptance matrix

Tracking: [#104](https://git.home.arpa/itdlabs/blit/issues/104).
Checkpoint: annotated tag `v0.1.553`, source `86d28b2`, live identity verified
on10.54 on2026-10-19. The package manifest is `releases/0.1.553.yaml`.
The tag preserves the operator's current KMS checkpoint; issue#101's motion
performance acceptance remains open.

This matrix inventories the recent direct-KMS requirements and the shared
contract. Each row requires separate direct-KMS, SDL/software and SDL/GLES2
physical evidence. Existing local tests identify starting fixtures, not passes
for all three backends. Every row is pending full parity acceptance.

| Required behavior | Authoritative decisions / starting evidence |
| --- | --- |
| Complete front-to-back frame inheritance, atomic commit, damage retirement | ADRs0049,0099,0101,0116,0117 |
| Fractional font/image scaling, centered complete cells and pane clipping | ADRs0074,0102,0108,0123,0131,0132 |
| Cached scene, bounded partial repaint, cursor restoration and held-scene pointer | ADRs0103,0106,0112,0114,0136 |
| Kitty image layers, replacement/deletion, scrolling and default-background transparency | ADRs0060,0085,0087,0109,0110,0111; issues84,86,88 |
| Sixel capability negotiation, replacement and resource lifetime | ADR0021; issues85,87 |
| UTF-8, wide glyphs, box/half-block geometry, fonts and tenth-step scales | ADRs0007,0019,0020,0062,0074,0123 |
| Keyboard, modifiers, all mouse buttons/wheel, aggregate devices and KVM reconnection | ADRs0028,0039,0041,0064,0068,0069,0070 |
| Shared API-owned pointer appearance, scaling and cross-output hit testing | ADRs0029,0061,0133 |
| Independent mixed-refresh deadlines, fractional mode timing, deferred work retry | ADRs0006,0115,0118,0119,0120 |
| Per-output submitted/completed frames, pointer state/request/submission correlation | releases0.1.552–0.1.554; issue101 |
| Multiple physical connectors, independent output resources and safe hotplug | ADRs0129,0130; issue100 |
| Arbitrary per-output pane grids, placement, rotation and independent scaling | ADRs0075,0076,0123,0131 |
| One primary, login-owned joins, stable client names and disappearance/return | ADRs0122,0124–0128,0135; issue103 |
| API configuration save/load/apply and wallpaper persistence | ADRs0067,0089,0124; issue99 |
| Canvas overlays/effects and identical layer semantics on each path | published effects/overlay schema; bidirectional inventory required |
| Nonblocking screenshots, coherent observation and bounded control/shutdown under load | ADRs0038,0092,0105; observation API gate |
| Terminal byte fragmentation and nonvisual traffic without spurious repaint | ADRs0121,0134 |

## First executable increment

Candidate0.1.556 adds API-owned startup backend selection (ADR0138) so physical
comparison can use saved settings and supervised restart. The expanded
`gles` suite passes5 gates on verified offscreen/OpenGLES2: glyph geometry,
Kitty layering/transparency, motion trails, wallpaper damage/deletion and
held-scene/two-renderer metrics. These reuse the software fixtures through
`sdl_test_profile.rb`; no physical acceptance row is closed by these results.

Candidate0.1.555 corrects each SDL output's presentation counters and durations
to use that output's state instead of copying global values. The production
`surface_damage_gate.c` fixture exercises two independent software renderers:
counts2/1, aggregate3, idle-output isolation, independent timing,60/120Hz
scheduler periods and teardown reset. It failed before the fix because the
idle second output inherited the first output's count. Hardware completion
and multi-connector behavior still require their own physical evidence.

`tools/run-gates.sh gles` passes the shared wallpaper-damage fixture through
SDL offscreen/OpenGLES2 using software GL. Identity and pixels are checked;
the two-renderer output counter fixture also passes on verified GLES2.
The physical KMSDRM and hardware timing rows remain pending.

`tools/tests/sdl_wallpaper_damage_gate.rb` now takes explicit test driver and
renderer parameters. It verifies ready/backend/video-driver/renderer identity
before accepting pixel evidence, resolves a real display ID from the API,
and attaches its fixture to that display. It asserts wallpaper survives cell
damage and disappears after graphics deletion. The default remains the local
dummy/software fixture used by `tools/run-gates.sh renderer`.

On an isolated console test host with the production SDL library and no other
DRM owner, use the same fixture with `BLIT_TEST_VIDEO_DRIVER=kmsdrm` and
`BLIT_TEST_RENDERER=software` or `opengles2`. `BLIT_TEST_DISPLAY_ID` optionally
selects a discovered output. This starts a test server and must not be run
alongside the live Blit service. It does not switch the live service backend
or validate multi-output timing. Extend this runner across the matrix before
claiming feature parity.
