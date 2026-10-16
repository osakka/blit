# ADR 0104: Relocate an auto-started login manager to an explicit pane

- Status: accepted
- Date: 2026-10-16
- Scope: login-manager lifecycle and direct-KMS pane presentation

## Context

The packaged service auto-starts one login manager. A client may then issue
`POST /v1/login` with an explicit pane target. Previously, when the manager
was already running, Blit updated the resource's requested pane but returned
the existing session unchanged. The API therefore reported the lower pane
while the session and its pixels remained in the upper pane.

Moving the session also changes its physical origin. Direct KMS compared only
surface id, revision, and graphics signature, so old pixels could remain at
the previous origin when the same surface moved.

## Decision

When `POST /v1/login` names a pane and an auto-started login manager is already
running, Blit relocates that existing session to the requested pane before
returning success. The session's pane and display ids, surface geometry, and
topology revision are updated together.

Direct KMS records each presented view's origin. An origin change forces a
complete output redraw, so the old pane region is cleared from the new scene
before the moved session is presented.

## Consequences

- `/v1/login` and the returned session describe the same physical pane.
- Moving a login manager cannot leave its previous pixels behind.
- The change is entirely in Blit lifecycle/composition code; Hibr is not
  modified.
- A failed relocation returns a typed conflict instead of claiming success.

## Verification boundary

- The login lifecycle gate must continue to pass.
- The KMS composition gate and TCC build must pass.
- Live evidence must show `login.pane_id == login.session.pane_id`, a null
  surface on the upper pane, a surface on the lower pane, and a screenshot
  with the upper region cleared.
