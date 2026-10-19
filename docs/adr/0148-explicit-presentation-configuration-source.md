# ADR0148: explicit presentation source and safe invalid-environment handling

- Date: 2026-10-19
- Status: accepted
- Tracking: Blit108; related ADR0138

The saved settings API remains the sole writable presentation owner. Environment
variables are startup defaults, not competing overrides. Keep precedence:
saved settings, then valid environment, then existing default SDL. Do not
silently interpret invalid BLIT_DISPLAY_BACKEND as SDL. Valid values are kms
and sdl; kmsdrm names SDL's video driver, not Blit's native backend.

Expose derived read-only effective.presentation_configuration metadata naming
backend/renderer source, raw backend environment value/validity/override,
configuration validity, renderer applicability and actual video driver.
Never add a second editable preference file to the panel. Preserve existing
effective fields and make the new object additive for older clients.

If saved settings select a valid backend, an invalid environment default is
ignored with an explicit warning. If no valid saved selection exists, refuse
display initialization, report the validation error and keep HTTP control
reachable for repair. A valid settings save plus supervised restart recovers
without requiring a console. Reset/ignore do not bypass invalid defaults.

The control panel displays actual/requested backend, owner/source, renderer
applicability and validation, distinguishing native KMS from SDL/KMSDRM.
Package-generated environment guidance names saved-settings precedence.
Existing operator files and tokens are not rewritten by package upgrades.

Verify both precedence directions, unset/valid/invalid environment values,
API survival and recovery, ignore/reset and no durable-setting mutation on
invalid input. Build the maintenance package from the deployed561 source,
not the pending font candidate. Merge the same patch into main; no divergent
runtime implementation or Hibr changes. Physical output ownership stays native.
