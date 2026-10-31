# ADR 0155: pinned SDL KMS compatibility boundary

- Status: superseded by ADR 0184
- Date: 2026-10-20
- Superseded by [ADR 0184](0184-sdl-is-removed.md): SDL is removed; native KMS is the only presentation path.
- Tracking: Blit 104

Physical 571 fault: second output first SDL_RenderPresent, before its completion
marker. RIP 7f015c951bc1 lies in SDL executable map 7f015c832000..7f015c98c000,
file offset 32000. ELF executable PT_LOAD virtual address equals that file
offset; resulting ELF address 151bc1 resolves to add_plane_property(),
SDL_kmsdrmvideo.c:248. Instruction loads p->props. Remote/local/bundled SDL
SHA256 all d91f2936576a0fee7bf5cb231f8528be65ffa44e2ed7cbea480d599a0bbd08c2.

Pinned source allocates zeroed per-display data, but initializes display_plane
inside KMSDRM_GBMInit behind the device-global gbm_init guard. Only the first
display receives plane setup. The second atomic swap dereferences its unset
plane. Both fenced and double-buffered atomic paths consume that plane, so
changing buffering alone cannot address this defect. Native Blit KMS is not
using this SDL plane state. Current upstream source inspected still has this
guard; no verified upstream fix is available from that inspection.

Use SDL's supported SDL_HINT_KMSDRM_ATOMIC=0 before SDL initialization for the
exact pinned SDL 3.4.16 version. The legacy swap path does not consume the
secondary atomic plane. Do not copy private structs, patch the vendor, add
a second presentation implementation or silently report native fallback as
SDL success. Normal native backend exits before this compatibility policy.

Respect explicit environment priority: if an operator forces atomic=1 and
the selected driver is kmsdrm, refuse presentation with a diagnostic/API
backend error before window creation. Do not silently override that request
or crash. Other video drivers/versions are not rejected by this guard.
Diagnostics expose render.sdl_kms_mode (legacy/atomic/null), plus the explicit
sdl_kms_legacy_compatibility warning. This is a derived dependency constraint,
not another saved setting or a capability claim. A dependency upgrade must
revisit the policy and re-run physical startup/pixels/cadence/hotplug gates.

Tests: sdl_kms_policy_gate.rb uses actual SDL hints with mocked driver/version,
separate processes for cached environment cases; pinned default, explicit
conflict, other driver/version and native reporting. Schema/lifecycle and
software/GLES startup boundary tests precede timed physical trials. Startup
success alone is not pixel, performance or hotplug parity. No Hibr/SDL edits.
An additional actual-discovery fixture verifies hint selection before SDL_Init
and explicit conflict refusal before enumeration, with no DRM opened.
572 physical trial baseline/trial/restoration all had ONE output after the
operator's temporary monitor removal; this is not validation of this policy
with two outputs. Retain that acceptance gap until the hardware returns.

References:

- https://wiki.libsdl.org/SDL3/SDL_HINT_KMSDRM_ATOMIC
- https://github.com/libsdl-org/SDL/blob/main/src/video/kmsdrm/SDL_kmsdrmvideo.c
- Local exact pinned source/disassembly and build/trial571-journal.txt;
  remote library digest: build/trial571-library-sha.txt.
