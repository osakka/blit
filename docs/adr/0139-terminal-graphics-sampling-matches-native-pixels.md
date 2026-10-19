# ADR 0139: Terminal graphics use nearest-neighbor sampling on every backend

- Status: implemented; bounded physical comparison passes on all three backends
- Date: 2026-10-19
- Issue: Blit104

## Evidence and decision

The 0.1.556 physical comparison passed 12/12 motion captures on direct KMS and
SDL/KMSDRM software, but the SDL Kitty composition probe returned blended
colours for a 2x2 RGB image enlarged to an 8x16 cell. For example, a red sample
was 211,31,13 rather than 255,0,0. Native KMS returned exact quadrants.
`sdl_graphics_texture()` set blending but left texture sampling at SDL's
default. A local production-code fixture reproduces the fault before the fix:
software output 0 pixel 2,0 is 223,31,0 rather than 255,0,0.

Set nearest-neighbor sampling explicitly when creating terminal graphics
textures. This matches native KMS's source-pixel lookup and applies to Kitty
and sixel through their existing shared image/texture path. Retain the existing
alpha blend policy and cache; a sampling-policy setup failure rejects the
texture with a diagnostic rather than silently choosing another policy.

Default-driver filtering and relaxed pixel tolerances would retain different
visible results across backends; reject both. A future configurable image
filter requires its own shared API and implementation; it is not implicit in
backend selection.

## Verification

The same fixture checks every pixel of an enlarged four-colour image on both
independent output renderers. Run it on software and actual OpenGLES2 through
the explicit GLES suite. Re-run the physical comparison on 10.54, retain exact
captures, and report each backend's result separately. The first native trial's
pointer-overlap failure remains recorded; the comparison now parks the pointer
outside the sampled area and verifies its state without masking failed pixels.

Physical revalidation on 10.54 with package 0.1.557 passes exact Kitty
quadrants and deletion on direct KMS, SDL/KMSDRM software and SDL/KMSDRM
OpenGLES2. Evidence: `/tmp/blit-backend-parity-557-20261019/`.
