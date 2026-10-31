# ADR 0149: submitted pointer state belongs to each output

- Status: accepted
- Date: 2026-10-19
- Tracking: Blit 101; related ADR 0136, ADR 0137

Native KMS had one global previous-pointer state, updated whenever a present
call did not defer. Outputs have independent deadlines/masks. Servicing an
idle output could advance that global state without submitting any pixels;
the pointer's actual output then saw no change and skipped its page flip.
Content redraw could force submission anyway, masking the lost cursor update.
This is a concrete mechanism consistent with the operator's video clue, not
proof that every perceived motion issue has the same cause.

Keep cursor state on each KmsOutput and advance it only after a successful
page-flip submission or recovery modeset for that output. Use that same state
for old-footprint damage, crossing and hide decisions. Failed, deferred,
masked-out and unchanged outputs do not consume cursor state. Pointer art
replacement invalidates each output. Remove the competing global snapshot.
No schema, settings, input mapping, font, Hibr or SDL backend change.

kms_pointer_phase_gate.c includes the production compositor and the existing
mock DRM boundary. Before the fix it fails "idle output consumed active
pointer motion". Afterward it verifies 120 idle-first motions on both outputs,
crossings in both orders, hide, pending-flip/latest-position retention,
modeset and exact cursor/no-trail pixels. Existing native composition, pointer
art/geometry and hotplug gates remain required. Compare the same bounded live
workload before/after; retain native backend, layout and current font.

On 10.54, the clean requested 60Hz API workload improved from 23.62 to 54.84
confirmed DP flips/sec; a 120Hz offered workload reached 58.18 confirmed/sec
on the 59.997Hz output. Zero physical mouse events in these windows. Absolute
position/scene differed across versions; deterministic regression isolates the
defect, while API measurements show bounded recovery, not universal physical
latency acceptance. See verification-status.md and issue 101.
