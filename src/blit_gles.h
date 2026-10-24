#ifndef BLIT_GLES_H
#define BLIT_GLES_H

/* ADR 0181: the GLES compositor inside native KMS. Mesa's EGL, GLESv2 and
   GBM are loaded at runtime, so a host without them keeps the CPU
   compositor. Two platforms: GBM on the KMS device for scanout, and Mesa's
   surfaceless platform for headless tests, where a target is a framebuffer
   object.

   Step 2 draws a complete XRGB8888 frame, as the CPU compositor leaves it,
   as one textured quad. Later steps replace what is drawn, not the
   pipeline. */

#include <stddef.h>
#include <stdint.h>

typedef struct BlitGles BlitGles;
typedef struct BlitGlesTarget BlitGlesTarget;

BlitGles *blit_gles_open_surfaceless(char *error, size_t error_capacity);
BlitGles *blit_gles_open_gbm(int drm_fd, char *error, size_t error_capacity);
void blit_gles_close(BlitGles *gles);
/* "GL_RENDERER | GL_VERSION", for diagnostics. */
const char *blit_gles_identity(const BlitGles *gles);

/* An offscreen target (surfaceless) or a scanout surface (GBM). */
BlitGlesTarget *blit_gles_target_open(BlitGles *gles, int width, int height,
                                      char *error, size_t error_capacity);
void blit_gles_target_close(BlitGles *gles, BlitGlesTarget *target);

/* Draws a top-down XRGB8888 frame of the target's size. On GBM the result
   is queued with eglSwapBuffers; lock it with blit_gles_lock_front. Returns
   0, or -1 with the reason (a lost context is a failure). */
int blit_gles_draw_frame(BlitGles *gles, BlitGlesTarget *target,
                         const void *pixels, int pitch,
                         char *error, size_t error_capacity);

/* Reads the target back as top-down XRGB8888 (surfaceless targets). */
int blit_gles_read_frame(BlitGles *gles, BlitGlesTarget *target,
                         void *pixels, int pitch);

/* GBM only: the buffer just drawn, for drmModeAddFB2 and a page flip. It
   stays locked until released, which must wait for its flip to complete. */
void *blit_gles_lock_front(BlitGles *gles, BlitGlesTarget *target,
                           uint32_t *handle, uint32_t *pitch);
void blit_gles_release(BlitGles *gles, BlitGlesTarget *target, void *buffer);

#endif
