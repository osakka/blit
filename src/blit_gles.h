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

/* Rows uploaded so far: unchanged rows are not sent again. */
unsigned long long blit_gles_uploaded_rows(const BlitGlesTarget *target);

/* Reads the target back as top-down XRGB8888: a surfaceless target, or on
   GBM the frame being composed, before blit_gles_frame_end swaps it. */
int blit_gles_read_frame(BlitGles *gles, BlitGlesTarget *target,
                         void *pixels, int pitch);

/* ---- Composition (ADR 0181 steps 3-4): the GPU draws a view itself. ----
   Coordinates are target pixels, top-left origin. Every quad blends as
   premultiplied source-over, as the CPU compositor's integer rules do:
     SOLID     colour at opacity `alpha` (255 = opaque)
     COVERAGE  a sprite's alpha tints `rgb` (glyph ink)
     PREMULT   a sprite's premultiplied colour (colour emoji)
     IMAGE     straight-alpha RGBA or RGB texture (Kitty/sixel/API images)
   State changes flush the batch; draw order is submission order. */
enum {
    BLIT_GLES_SOLID = 0,
    BLIT_GLES_COVERAGE = 1,
    BLIT_GLES_PREMULT = 2,
    BLIT_GLES_IMAGE = 3
};
typedef unsigned int BlitGlesTexture;

/* RGBA or RGB bytes in natural order; 0 on failure. */
BlitGlesTexture blit_gles_texture_create(BlitGles *gles, int width, int height,
                                         int bytes_per_pixel);
int blit_gles_texture_update(BlitGles *gles, BlitGlesTexture texture,
                             int bytes_per_pixel, int x, int y, int width,
                             int height, const void *pixels, int pitch);
void blit_gles_texture_delete(BlitGles *gles, BlitGlesTexture texture);

int blit_gles_frame_begin(BlitGles *gles, BlitGlesTarget *target,
                          char *error, size_t error_capacity);
void blit_gles_rect(BlitGles *gles, int x, int y, int width, int height,
                    uint32_t rgb, unsigned alpha);
void blit_gles_quad(BlitGles *gles, int mode, BlitGlesTexture texture,
                    int texture_width, int texture_height,
                    int source_x, int source_y, int source_width,
                    int source_height, int x, int y, int width, int height,
                    uint32_t rgb);
/* An IMAGE quad that samples exactly the source pixel the CPU compositor's
   integer scaler picks, floor(d * source / destination), at any ratio. */
void blit_gles_image(BlitGles *gles, BlitGlesTexture texture,
                     int texture_width, int texture_height,
                     int source_x, int source_y, int source_width,
                     int source_height, int x, int y, int width, int height);
/* Clip later quads to a rectangle; width 0 clears the clip. */
void blit_gles_clip(BlitGles *gles, int x, int y, int width, int height);
/* Flushes; on GBM queues the frame (lock it with blit_gles_lock_front). */
int blit_gles_frame_end(BlitGles *gles, BlitGlesTarget *target,
                        char *error, size_t error_capacity);
unsigned long long blit_gles_draw_calls(const BlitGles *gles);
/* Cumulative draw calls and vertices submitted. */
void blit_gles_counters(const BlitGles *gles, unsigned long long *draw_calls,
                        unsigned long long *vertices);

/* GPU time of a target's frames (frame_begin to frame_end), measured with
   GL_EXT_disjoint_timer_query and collected a few frames late. -1 when the
   driver cannot time them. */
typedef struct {
    double last_ms, max_ms;
    unsigned long long total_us, frames;
} BlitGlesGpuTime;
int blit_gles_gpu_time(const BlitGlesTarget *target, BlitGlesGpuTime *out);

/* A persistent offscreen scene, as the CPU compositor keeps one in RAM:
   views are composed into it, and what was not recomposed keeps its pixels.
   Opaque; cleared to black when opened. */
typedef struct BlitGlesScene BlitGlesScene;
BlitGlesScene *blit_gles_scene_open(BlitGles *gles, int width, int height);
void blit_gles_scene_close(BlitGles *gles, BlitGlesScene *scene);
/* Compose into the scene; blit_gles_rect/quad/clip apply to it until
   blit_gles_scene_end. */
int blit_gles_scene_begin(BlitGles *gles, BlitGlesScene *scene);
void blit_gles_scene_end(BlitGles *gles);
/* Inside blit_gles_frame_begin/end: the whole scene, copied 1:1 at (0, 0). */
void blit_gles_scene_draw(BlitGles *gles, BlitGlesScene *scene);

/* ---- ADR 0187: presentation effects, drawn as the scene is presented. ----
   1 when this context can draw them: highp fragment floats and the present
   program. Otherwise they are unavailable, and nothing else changes. */
int blit_gles_presents(const BlitGles *gles);
/* A pane drawn by the presentation pass: its rectangle in target pixels
   (top-left origin), the curvature warp's k (blit_effect_curvature_k), the
   glass scanlines' alpha (0..255, 0 none), glow's gain (0 none) and RGB
   shift's distance in pixels (0 none). */
typedef struct {
    int x, y, width, height;
    double k;
    unsigned scanline_alpha;
    double glow;
    double shift_px;   /* RGB shift's sample distance, 0 none */
} BlitGlesPresentPane;
/* Inside blit_gles_frame_begin/end, after the scene was composed: rebuild a
   pane's blurred copy from its rectangle of the scene (halved twice). */
int blit_gles_scene_glow(BlitGles *gles, BlitGlesScene *scene,
                         int x, int y, int width, int height);
/* Inside blit_gles_frame_begin/end: the plain copy, then each pane drawn
   again by the presentation program over its own rectangle. */
void blit_gles_scene_present(BlitGles *gles, BlitGlesScene *scene,
                             const BlitGlesPresentPane *panes, int count);

/* GBM only: the buffer just drawn, for drmModeAddFB2 and a page flip. It
   stays locked until released, which must wait for its flip to complete. */
void *blit_gles_lock_front(BlitGles *gles, BlitGlesTarget *target,
                           uint32_t *handle, uint32_t *pitch);
void blit_gles_release(BlitGles *gles, BlitGlesTarget *target, void *buffer);

#endif
