#ifndef BLIT_KMS_H
#define BLIT_KMS_H

#include <stddef.h>
#include <stdint.h>

#include "blit_font.h"
#include "blit_graphics.h"

typedef struct BlitKms BlitKms;

#define BLIT_KMS_MAX_MODES 64

typedef struct {
    int width;
    int height;
    double refresh_hz;
    int preferred;
} BlitKmsMode;

typedef struct {
    unsigned long long surface_id;
    unsigned long long surface_revision;
    unsigned long long display_id;
    int width;
    int height;
    int origin_x;
    int origin_y;
    int cell_width;
    int cell_height;
    int columns;
    int rows;
    const uint32_t *codepoints;
    const uint32_t *combining;
    const unsigned char *combining_count;
    const uint32_t *foreground;
    const uint32_t *background;
    const uint32_t *underline_color;
    const uint16_t *attributes;
    int stride;
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int cursor_style;
    int damage_valid;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
    int *damage_consumed;
    const BlitGraphicsImage *graphics;
    int graphics_count;
} BlitKmsSurfaceView;

typedef struct {
    unsigned long long display_id;
    int x;
    int y;
    int width;
    int height;
    uint32_t color;
    int opacity;
} BlitKmsOverlay;

typedef struct {
    unsigned long long id;
    char name[128];
    int x;
    int y;
    int width;
    int height;
    double refresh_hz;
    int mode_count;
    int active_mode;
    BlitKmsMode modes[BLIT_KMS_MAX_MODES];
    int presentation_state;
    double last_present_ms;
    double max_present_ms;
    unsigned long long present_calls;
} BlitKmsDisplayInfo;

#define BLIT_KMS_PRESENTATION_AVAILABLE 0
#define BLIT_KMS_PRESENTATION_UNAVAILABLE 1
#define BLIT_KMS_PRESENTATION_PRESENTING 2
#define BLIT_KMS_PRESENTATION_FAILED 3
#define BLIT_KMS_PRESENTATION_RECOVERING 4

BlitKms *blit_kms_new(BlitFont *font);
int blit_kms_open(BlitKms *kms, char *error, size_t error_capacity);
int blit_kms_display_count(const BlitKms *kms);
int blit_kms_display_info(const BlitKms *kms, int index,
                          BlitKmsDisplayInfo *info);
int blit_kms_set_display_position(BlitKms *kms, int index, int x, int y);
int blit_kms_fd(const BlitKms *kms);
int blit_kms_service(BlitKms *kms, int readable);
int blit_kms_cell_width(const BlitKms *kms);
int blit_kms_cell_height(const BlitKms *kms);
int blit_kms_present(BlitKms *kms, const BlitKmsSurfaceView *views,
                     int view_count, float pointer_x, float pointer_y,
                     int pointer_visible, uint32_t output_mask,
                     const BlitKmsOverlay *overlays, int overlay_count,
                     unsigned long long visual_revision,
                     int effect_enabled, int effect_intensity);
/* Return a bounded 32-bit BMP snapshot of the currently submitted front
   buffer. The caller owns *bytes and must free it. Return 1 when ready, 0
   when the output has not presented a buffer yet, and -1 on failure. */
int blit_kms_screenshot(BlitKms *kms, int index,
                        unsigned char **bytes, size_t *length);
void blit_kms_close(BlitKms *kms);

#endif
