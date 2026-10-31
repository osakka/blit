# ADR 0119: KMS refresh keeps the mode timing fraction

- Status: accepted; in use since 0.1.533
- Date: 2026-10-17
- Issue: [Blit #92](https://git.home.arpa/itdlabs/blit/issues/92)

## Evidence

Direct KMS reported active and enumerated refresh from `mode.vrefresh`.
Linux's [drm_mode_vrefresh and drm_mode_convert_to_umode](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/drm_modes.c)
round this field to an integer. A fixture with a 148352-kHz clock and totals
2200x1125 therefore returned 60.000000 instead of 59.940202 Hz. The production
mode helper's regression fails before correction and passes afterward.

The real 0.1.532 6000-frame soak had zero stale pixels but failed its strict
refresh comparison at 59.912 submissions/s against reported 60.0 Hz. Correct
metadata must precede interpreting that difference; rounding is not assumed
to explain it without a new live measurement.

## Decision

Compute refresh as `clock_kHz * 1000 / (htotal * vtotal)`, applying the kernel's
interlace, doublescan, and vscan factors while retaining the fraction. Use one
helper for active and enumerated modes. Keep the existing integer/default
fallback for invalid timing tuples. Preserve the existing mode selection and
physical configuration; this correction does not select a different mode.

## Verification

Test the known fractional fixture and each scan flag, then retain the KMS
ownership/pixel gates. The API's existing number-valued refresh fields must
report the actual mode timing. Repeat the identical long physical soak with
the finite-window refresh assertion and full-pane pixel comparison; do not
weaken the acceptance threshold to hide a failure.
