/* ADR 0203: the performance panel. Blit draws it over the panes and
   overlays of the outputs it is shown on, in both compositors, from the
   counters the API already reports: it measures nothing of its own. Its
   text uses the boot PSF, loaded once into a font of its own, so it looks
   the same whatever font the terminals use.

   Each panel is about its own output: what it drew in the last second and
   why, its draw and flip times over the last ten, its gpu, the pictures its
   sessions sent, and a graph of the last minute. Blit as a whole -- loop,
   input, glyph cache, errors, and an update waiting for a restart (ADR
   0205) -- is said once, on the first panel shown.
   Those figures (recent_tick) are kept whether or not the panel is on, and
   /v1/diagnostics reports each one.

   It is rebuilt at most hud.hz times a second and only while enabled, and
   only the lines and graph that changed are redrawn; a rebuild that changes
   no pixel keeps the revision, and an output is presented for the panel
   only when its revision changed. Such a frame is counted as the output's
   hud_frames and left out of every rate the panel shows. Off, there is no
   timer and no cost but the figures' bookkeeping, once a second.

   Hibr's section is the latest STA1 its frame client sent (ADR 0202,
   version 2), shown while younger than five seconds. Included into
   blit_server.c before blit_frames.h. */

#define BLIT_HUD_SECTION_BLIT 1u
#define BLIT_HUD_SECTION_HIBR 2u
#define BLIT_HUD_SECTION_GRAPH 4u
#define BLIT_HUD_SECTIONS_ALL 7u
#define BLIT_HUD_MARGIN 16
#define BLIT_HUD_PAD 6
#define BLIT_HUD_GRAPH_HEIGHT 48

static const char *const hud_corners[] = {"top-left", "top-right", "bottom-left",
                                          "bottom-right"};
static const char *const hud_section_names[] = {"blit", "hibr", "graph"};

/* Colours, as RGBA in memory order. */
#define BLIT_HUD_RGBA(r, g, b) ((uint32_t)(r) | (uint32_t)(g) << 8 | \
                                (uint32_t)(b) << 16 | 0xff000000U)
#define BLIT_HUD_BACKGROUND BLIT_HUD_RGBA(0x10, 0x10, 0x14)
#define BLIT_HUD_BORDER BLIT_HUD_RGBA(0x50, 0x50, 0x5a)
#define BLIT_HUD_TEXT BLIT_HUD_RGBA(0xd8, 0xd8, 0xdc)
#define BLIT_HUD_HEADING BLIT_HUD_RGBA(0x80, 0xb4, 0xff)
#define BLIT_HUD_GRAPH_FILL BLIT_HUD_RGBA(0x18, 0x18, 0x20)
#define BLIT_HUD_BAR_OK BLIT_HUD_RGBA(0x4c, 0xb0, 0x6e)
#define BLIT_HUD_BAR_LATE BLIT_HUD_RGBA(0xff, 0x5a, 0x4a)
#define BLIT_HUD_LIMIT BLIT_HUD_RGBA(0x90, 0x90, 0x9c)

typedef struct {
    int enabled, corner, hz, opacity;
    unsigned sections;
    char display_id[32];
} BlitHudSettings;

/* document.hud: {"enabled": bool, "display_id": "display-N" | "all",
   "corner": one of hud_corners, "hz": 1..10, "opacity": 10..100 (percent),
   "sections": a subset of hud_section_names}. Strict like effects: an
   unknown or repeated member or a value out of range makes it invalid. What
   is absent takes the default: enabled from BLIT_HUD=1, every display, top
   right, 2 Hz, opaque, every section. */
static int settings_hud_parse(const char *document, BlitHudSettings *out)
{
    static char section[BLIT_SETTINGS_CAPACITY];
    char key[64], value[256];
    const char *cursor = document, *member;
    const char *boot = getenv("BLIT_HUD");
    int found = 0;
    unsigned seen = 0;

    memset(out, 0, sizeof(*out));
    out->enabled = boot != NULL && strcmp(boot, "1") == 0;
    out->corner = 1;
    out->hz = 2;
    out->opacity = 100;
    out->sections = BLIT_HUD_SECTIONS_ALL;
    snprintf(out->display_id, sizeof(out->display_id), "all");
    if (document == NULL || document[0] == '\0') return 1;
    while (*cursor && *cursor != '{') ++cursor;
    if (*cursor++ != '{') return 0;
    while ((cursor = json_next_member(cursor, key, sizeof(key), section,
                                      sizeof(section))) != NULL) {
        if (strcmp(key, "hud") != 0) continue;
        if (found++ || section[0] != '{') return 0;
        break;
    }
    if (!found) return 1;
    member = section;
    while ((member = json_next_member(member, key, sizeof(key), value,
                                      sizeof(value))) != NULL) {
        int i;
        if (strcmp(key, "enabled") == 0) {
            if ((seen & 1u) || (out->enabled = effects_boolean(value)) < 0) return 0;
            seen |= 1u;
        } else if (strcmp(key, "display_id") == 0) {
            size_t length = strlen(value);
            if ((seen & 2u) || length < 3 || length - 2 >= sizeof(out->display_id) ||
                value[0] != '"' || value[length - 1] != '"') return 0;
            seen |= 2u;
            snprintf(out->display_id, sizeof(out->display_id), "%.*s",
                     (int)(length - 2), value + 1);
            if (strcmp(out->display_id, "all") != 0) {
                const char *digits = out->display_id + 8;
                if (strncmp(out->display_id, "display-", 8) != 0 || *digits < '1' ||
                    *digits > '9') return 0;
                while (*digits >= '0' && *digits <= '9') ++digits;
                if (*digits != '\0') return 0;
            }
        } else if (strcmp(key, "corner") == 0) {
            if (seen & 4u) return 0;
            seen |= 4u;
            for (i = 0; i < 4; ++i) {
                char quoted[24];
                snprintf(quoted, sizeof(quoted), "\"%s\"", hud_corners[i]);
                if (strcmp(value, quoted) == 0) break;
            }
            if (i == 4) return 0;
            out->corner = i;
        } else if (strcmp(key, "hz") == 0) {
            if ((seen & 8u) || !effects_integer(value, 10, &out->hz) || out->hz < 1)
                return 0;
            seen |= 8u;
        } else if (strcmp(key, "opacity") == 0) {
            if ((seen & 32u) || !effects_integer(value, 100, &out->opacity) ||
                out->opacity < 10)
                return 0;
            seen |= 32u;
        } else if (strcmp(key, "sections") == 0) {
            const char *item = value;
            if ((seen & 16u) || *item++ != '[') return 0;
            seen |= 16u;
            out->sections = 0;
            for (;;) {
                while (*item == ' ' || *item == '\t' || *item == '\n' || *item == '\r') ++item;
                if (*item == ']' && item[1] == '\0') break;
                if (*item++ != '"') return 0;
                for (i = 0; i < 3; ++i) {
                    size_t length = strlen(hud_section_names[i]);
                    if (strncmp(item, hud_section_names[i], length) == 0 &&
                        item[length] == '"') break;
                }
                if (i == 3 || (out->sections & (1u << i))) return 0;
                out->sections |= 1u << i;
                item += strlen(hud_section_names[i]) + 1;
                while (*item == ' ' || *item == '\t' || *item == '\n' || *item == '\r') ++item;
                if (*item == ',') ++item;
                else if (*item != ']') return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

static int settings_hud_valid(const char *document)
{
    BlitHudSettings settings;
    return settings_hud_parse(document, &settings);
}

static int hud_shows_output(const BlitRuntime *runtime, int output)
{
    char id[32];
    if (!runtime->hud_enabled || output < 0 || output >= runtime->output_count ||
        output >= BLIT_MAX_DISPLAYS)
        return 0;
    if (strcmp(runtime->hud_display_id, "all") == 0)
        return 1;
    snprintf(id, sizeof(id), "display-%llu", runtime->outputs[output].display_id);
    return strcmp(runtime->hud_display_id, id) == 0;
}

/* The panels the compositor draws now, with the rows each changed. */
static void hud_attach(BlitRuntime *runtime)
{
    BlitKmsHud huds[BLIT_MAX_DISPLAYS];
    int i, count = 0;
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        const BlitHudPanel *panel = &runtime->hud_panels[i];
        if (panel->width < 1 || panel->rgba == NULL)
            continue;
        huds[count].display_id = runtime->outputs[i].display_id;
        huds[count].x = panel->x;
        huds[count].y = panel->y;
        huds[count].width = panel->width;
        huds[count].height = panel->height;
        huds[count].rgba = panel->rgba;
        huds[count].revision = panel->revision;
        huds[count].base_revision = panel->base_revision;
        huds[count].dirty_y = panel->dirty_y;
        huds[count].dirty_height = panel->dirty_height;
        huds[count].translucent = runtime->hud_opacity < 100;
        ++count;
    }
    blit_kms_set_hud(runtime->kms, huds, count);
}

/* Present the outputs in `mask` soon, as an animation tick does: an idle
   output's stale deadline is not one this frame missed. */
static void hud_schedule(BlitRuntime *runtime, uint32_t mask, uint64_t now)
{
    int i;
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        if (!(mask & (1U << i)))
            continue;
        if (!runtime->render_pending || runtime->render_next_us[i] > now)
            runtime->render_next_us[i] = now;
        runtime->render_pending = 1;
    }
}

/* Revisions are unique across panels, so a texture or a remembered
   revision can never match a different picture. */
static unsigned long long hud_next_revision(void)
{
    static unsigned long long revision;
    return ++revision;
}

/* A present the main loop saw on an output: its draw, flip and whole frame
   times, into the current second's worst. */
static void recent_note_present(BlitRuntime *runtime, int output,
                                const BlitKmsDisplayInfo *info)
{
    BlitOutputRecent *recent;
    if (output < 0 || output >= BLIT_MAX_DISPLAYS)
        return;
    recent = &runtime->output_recent[output];
    recent->draw_ms = info->draw_ms;
    recent->flip_ms = info->last_flip_ms;
    if (info->draw_ms > recent->draw_second) recent->draw_second = info->draw_ms;
    if (info->last_flip_ms > recent->flip_second) recent->flip_second = info->last_flip_ms;
    if (info->last_present_ms > recent->frame_second)
        recent->frame_second = info->last_present_ms;
}

static double recent_rate(unsigned long long now, unsigned long long then, double seconds)
{
    return now >= then && seconds > 0 ? (double)(now - then) / seconds : 0.0;
}

/* The worst of the last ten seconds and the current one. */
static double recent_worst(const double *seconds, double current)
{
    double worst = current;
    int i;
    for (i = 0; i < BLIT_RECENT_SECONDS; ++i)
        if (seconds[i] > worst) worst = seconds[i];
    return worst;
}

/* Once a turn: the loop gap and input dispatch into the current second.
   At each whole second, each output's rates over it -- presents, and why;
   late flips; missed deadlines; its sessions' pictures -- and the worst
   times move on. A few comparisons a turn, the rest once a second. */
static void recent_tick(BlitRuntime *runtime, uint64_t now)
{
    int i, j, start = runtime->recent_us == 0, slot = runtime->recent_ring;
    double seconds;
    if (runtime->diagnostic_last_loop_gap_ms > runtime->loop_second)
        runtime->loop_second = runtime->diagnostic_last_loop_gap_ms;
    if (runtime->diagnostic_input_events != runtime->recent_input_seen) {
        runtime->recent_input_seen = runtime->diagnostic_input_events;
        if (runtime->diagnostic_last_input_dispatch_ms > runtime->input_second)
            runtime->input_second = runtime->diagnostic_last_input_dispatch_ms;
    }
    if (!start && now - runtime->recent_us < 1000000ULL)
        return;
    seconds = start ? 0.0 : (double)(now - runtime->recent_us) / 1000000.0;
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        BlitOutputRecent *r = &runtime->output_recent[i];
        BlitKmsDisplayInfo info;
        int display_index = display_index_for_output(runtime, i);
        unsigned long long descriptor = 0, kitty = 0, sixel = 0, refused = 0;
        if (display_index < 0 || blit_kms_display_info(runtime->kms, display_index, &info) != 0)
            continue;
        for (j = 0; j < BLIT_MAX_SESSIONS; ++j) {
            const BlitSession *session = &runtime->sessions[j];
            if (!session->used || session_output_index(runtime, session) != i)
                continue;
            descriptor += session->frames_accepted;
            kitty += session->graphics_kitty;
            sixel += session->graphics_sixel;
            refused += session->frames_refused;
        }
        if (!start) {
            double draws = recent_rate(info.present_calls, r->presents, seconds) -
                recent_rate(info.hud_frames, r->hud, seconds);
            r->draws_hz = draws > 0 ? draws : 0;
            r->late_hz = recent_rate(info.flips_late, r->late, seconds);
            r->missed_hz = recent_rate(runtime->render_missed_deadlines[i], r->missed, seconds);
            r->effects_hz = recent_rate(info.effects_animation_frames, r->effects, seconds);
            r->pictures_hz = recent_rate(info.picture_frames, r->pictures, seconds);
            r->pointer_hz = recent_rate(info.pointer_frames, r->pointer, seconds);
            r->text_hz = recent_rate(info.text_frames, r->text, seconds);
            r->other_hz = r->draws_hz - r->effects_hz - r->pictures_hz - r->pointer_hz -
                r->text_hz;
            if (r->other_hz < 0) r->other_hz = 0;
            r->descriptor_hz = recent_rate(descriptor, r->descriptor, seconds);
            r->kitty_hz = recent_rate(kitty, r->kitty, seconds);
            r->sixel_hz = recent_rate(sixel, r->sixel, seconds);
            r->refused_hz = recent_rate(refused, r->refused, seconds);
            r->draw_worst[slot] = r->draw_second;
            r->flip_worst[slot] = r->flip_second;
            r->graph[r->graph_next] = (float)r->frame_second;
            r->graph_next = (r->graph_next + 1) % BLIT_HUD_GRAPH;
        }
        r->presents = info.present_calls;
        r->hud = info.hud_frames;
        r->late = info.flips_late;
        r->missed = runtime->render_missed_deadlines[i];
        r->effects = info.effects_animation_frames;
        r->pictures = info.picture_frames;
        r->pointer = info.pointer_frames;
        r->text = info.text_frames;
        r->descriptor = descriptor;
        r->kitty = kitty;
        r->sixel = sixel;
        r->refused = refused;
        r->draw_second = r->flip_second = r->frame_second = 0;
    }
    if (start) {
        /* The error counts ten seconds ago are, for now, these. */
        for (j = 0; j < BLIT_RECENT_SECONDS; ++j) {
            runtime->errors_rejected[j] = runtime->graphics_payloads_rejected;
            runtime->errors_dropped[j] = runtime->diagnostic_stream_dropped_events;
            runtime->errors_keys[j] = runtime->diagnostic_input_keys_dropped;
        }
        runtime->input_idle = 1;
    } else {
        runtime->loop_worst[slot] = runtime->loop_second;
        runtime->input_worst[slot] = runtime->input_second;
        runtime->input_idle = runtime->recent_input_seen == runtime->recent_input_start;
        runtime->errors_rejected[slot] = runtime->graphics_payloads_rejected;
        runtime->errors_dropped[slot] = runtime->diagnostic_stream_dropped_events;
        runtime->errors_keys[slot] = runtime->diagnostic_input_keys_dropped;
        runtime->recent_ring = (slot + 1) % BLIT_RECENT_SECONDS;
    }
    runtime->recent_input_start = runtime->recent_input_seen;
    runtime->loop_second = runtime->input_second = 0;
    runtime->recent_us = now;
}

/* Errors in the last ten seconds: now, less the counts as they began. */
static void recent_errors(const BlitRuntime *runtime, unsigned long long *rejected,
                          unsigned long long *dropped, unsigned long long *keys)
{
    int oldest = runtime->recent_ring;
    *rejected = runtime->graphics_payloads_rejected - runtime->errors_rejected[oldest];
    *dropped = runtime->diagnostic_stream_dropped_events - runtime->errors_dropped[oldest];
    *keys = runtime->diagnostic_input_keys_dropped - runtime->errors_keys[oldest];
}

/* The share of glyph lookups the cache answered, or -1 with none. */
static int recent_glyph_percent(const BlitRuntime *runtime)
{
    unsigned long long hits = 0, misses = 0;
    blit_raster_cache_counts(runtime->font.raster_state, &hits, &misses);
    return hits + misses ? (int)(hits * 100ULL / (hits + misses)) : -1;
}

static uint32_t hud_utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t cp = *p++;
    int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
    if (cp >= 0x80) cp &= 0x3fu >> more;
    while (more-- > 0 && (*p & 0xc0) == 0x80)
        cp = cp << 6 | (*p++ & 0x3fu);
    *text = (const char *)p;
    return cp;
}

static void hud_fill(BlitHudPanel *panel, int x, int y, int width, int height,
                     uint32_t rgba)
{
    int row, column;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > panel->width) width = panel->width - x;
    if (y + height > panel->height) height = panel->height - y;
    if (width < 1 || height < 1)
        return;
    /* One row by hand, the rest copied: tcc's loops are slow, memcpy is not. */
    {
        uint32_t *first = (uint32_t *)panel->rgba + (size_t)y * panel->width + x;
        for (column = 0; column < width; ++column)
            first[column] = rgba;
        for (row = 1; row < height; ++row)
            memcpy(first + (size_t)row * panel->width, first, (size_t)width * 4);
    }
}

/* Ink only; the background is already there. At most `columns` cells. */
static void hud_text(const BlitFont *font, BlitHudPanel *panel, int x, int y,
                     const char *text, int columns, uint32_t rgba)
{
    int cw = blit_font_width(font), ch = blit_font_height(font);
    while (*text && columns-- > 0 && x + cw <= panel->width) {
        uint32_t cp = hud_utf8_next(&text);
        size_t glyph;
        int row;
        if (cp == ' ') {
            x += cw;
            continue;
        }
        glyph = blit_font_glyph_index(font, cp);
        for (row = 0; row < ch && y + row < panel->height; ++row) {
            uint32_t bits = blit_font_glyph_row(font, glyph, cp, row);
            uint32_t *pixel = (uint32_t *)panel->rgba + (size_t)(y + row) * panel->width + x;
            int column;
            for (column = 0; bits != 0 && column < cw; ++column)
                if (bits & (1U << (cw - column - 1)))
                    pixel[column] = rgba;
        }
        x += cw;
    }
}

/* The session whose STA1 this output shows: of those it shows, the one
   heard from last, while younger than five seconds. */
static const BlitSession *hud_status_session(BlitRuntime *runtime, int output,
                                             uint64_t now)
{
    const BlitSession *best = NULL;
    int i;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        const BlitSession *session = &runtime->sessions[i];
        if (!session->used || session->status_count == 0 || now < session->status_us ||
            now - session->status_us >= BLIT_HUD_STATUS_US ||
            session_output_index(runtime, session) != output)
            continue;
        if (best == NULL || session->status_us > best->status_us)
            best = session;
    }
    return best;
}

static void hud_line(BlitHudPanel *panel, int *heading, int is_heading,
                     const char *format, ...)
{
    va_list arguments;
    if (panel->line_count >= BLIT_HUD_LINES)
        return;
    va_start(arguments, format);
    vsnprintf(panel->lines[panel->line_count], sizeof(panel->lines[0]), format, arguments);
    va_end(arguments);
    heading[panel->line_count++] = is_heading;
}

/* The renderer, short: its name before " (" and the first item inside,
   "AMD Radeon Graphics (radeonsi)". */
static void hud_renderer(const char *renderer, char *out, size_t capacity)
{
    const char *open = strstr(renderer, " (");
    const char *bar = strstr(renderer, " | ");
    if (open != NULL && (bar == NULL || open < bar))
        snprintf(out, capacity, "%.*s (%.*s)", (int)(open - renderer), renderer,
                 (int)strcspn(open + 2, ",)"), open + 2);
    else
        snprintf(out, capacity, "%.*s",
                 (int)(bar != NULL ? (size_t)(bar - renderer) : strlen(renderer)), renderer);
}

static unsigned long long hud_hash(unsigned long long hash, const void *data, size_t length)
{
    const unsigned char *byte = data;
    while (length-- > 0) {
        hash ^= *byte++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* This output's lines: what it drew in the last second and why, its own
   times and pictures, all as /v1/diagnostics reports them per output. */
static void hud_display_lines(BlitRuntime *runtime, int output, BlitHudPanel *panel,
                              int *heading, const BlitKmsDisplayInfo *info,
                              double refresh_hz)
{
    const BlitOutputRecent *r = &runtime->output_recent[output];
    char why[96], renderer[96];
    size_t used = 0;
    hud_line(panel, heading, 1, "display-%llu  %dx%d  %.0f Hz",
             runtime->outputs[output].display_id, info->width, info->height, refresh_hz);
    hud_line(panel, heading, 0, "draws   %.0f/s   late %.0f/s   missed %.0f/s",
             r->draws_hz, r->late_hz, r->missed_hz);
    why[0] = '\0';
    if (r->effects_hz >= 0.5)
        used += (size_t)snprintf(why + used, sizeof(why) - used, "effects %.0f  ", r->effects_hz);
    if (r->pictures_hz >= 0.5 && used < sizeof(why))
        used += (size_t)snprintf(why + used, sizeof(why) - used, "video %.0f  ", r->pictures_hz);
    if (r->pointer_hz >= 0.5 && used < sizeof(why))
        used += (size_t)snprintf(why + used, sizeof(why) - used, "pointer %.0f  ", r->pointer_hz);
    if (r->text_hz >= 0.5 && used < sizeof(why))
        used += (size_t)snprintf(why + used, sizeof(why) - used, "text %.0f  ", r->text_hz);
    if (r->other_hz >= 0.5 && used < sizeof(why))
        used += (size_t)snprintf(why + used, sizeof(why) - used, "other %.0f  ", r->other_hz);
    if (used >= 2 && used < sizeof(why))
        why[used - 2] = '\0';
    hud_line(panel, heading, 0, "why     %s", why[0] ? why : "nothing drawn");
    hud_line(panel, heading, 0, "draw    %.1f ms  (max %.1f in 10 s)", r->draw_ms,
             recent_worst(r->draw_worst, r->draw_second));
    hud_line(panel, heading, 0, "flip    %.1f ms  (max %.1f in 10 s)", r->flip_ms,
             recent_worst(r->flip_worst, r->flip_second));
    if (strcmp(info->compositor, "gles") == 0) {
        hud_renderer(info->compositor_renderer, renderer, sizeof(renderer));
        hud_line(panel, heading, 0, "gpu     %s", renderer);
    } else if (strcmp(info->compositor_requested, "gles") != 0) {
        hud_line(panel, heading, 0, "cpu (requested)");
    } else {
        hud_line(panel, heading, 0, "cpu: %s",
                 info->compositor_reason[0] ? info->compositor_reason : "fallback");
    }
    hud_line(panel, heading, 0, "video   %.0f/s  by descriptor  (kitty %.0f  sixel %.0f)",
             r->descriptor_hz, r->kitty_hz, r->sixel_hz);
    if (r->refused_hz >= 0.5)
        hud_line(panel, heading, 0, "refused %.0f/s  %s", r->refused_hz,
                 runtime->frames_last_error);
}

/* Blit as a whole, once, on the first panel shown. */
static void hud_blit_lines(BlitRuntime *runtime, BlitHudPanel *panel, int *heading)
{
    unsigned long long rejected, dropped, keys;
    int glyphs = recent_glyph_percent(runtime);
    hud_line(panel, heading, 1, "blit");
    hud_line(panel, heading, 0, "loop    worst %.1f ms in 10 s",
             recent_worst(runtime->loop_worst, runtime->loop_second));
    if (runtime->input_idle)
        hud_line(panel, heading, 0, "input   idle");
    else
        hud_line(panel, heading, 0, "input   %.1f ms last, %.1f max",
                 runtime->diagnostic_last_input_dispatch_ms,
                 recent_worst(runtime->input_worst, runtime->input_second));
    if (glyphs < 0)
        hud_line(panel, heading, 0, "glyphs  %s", runtime->font.raster_state == NULL ?
                 "bitmap font, no cache" : "no lookups yet");
    else
        hud_line(panel, heading, 0, "glyphs  %d%% hit", glyphs);
    recent_errors(runtime, &rejected, &dropped, &keys);
    if (rejected + dropped + keys == 0) {
        hud_line(panel, heading, 0, "errors  none");
    } else {
        char line[96];
        size_t used = 0;
        line[0] = '\0';
        if (rejected) used += (size_t)snprintf(line + used, sizeof(line) - used,
                                               "rejected %llu  ", rejected);
        if (dropped && used < sizeof(line))
            used += (size_t)snprintf(line + used, sizeof(line) - used, "dropped %llu  ", dropped);
        if (keys && used < sizeof(line))
            used += (size_t)snprintf(line + used, sizeof(line) - used, "keys %llu  ", keys);
        hud_line(panel, heading, 0, "errors  %sin 10 s", line);
    }
    /* ADR 0205: an update installed under this Blit, while pending; the
       version cut at 11 keeps the longer form inside BLIT_HUD_COLUMNS. */
    if (runtime->update_restart_pending)
        hud_line(panel, heading, 0, "update  %.11s installed, %s",
                 runtime->update_installed_version,
                 runtime->restart_after_update_manual ? "restart to apply" : "at login screen");
}

/* One output's panel: its text, then only the lines and graph that
   changed, unless its size or place did. Returns 1 when the revision
   changed. */
static int hud_build(BlitRuntime *runtime, int output, int first, uint64_t now)
{
    BlitHudPanel *panel = &runtime->hud_panels[output];
    const BlitOutputRecent *recent = &runtime->output_recent[output];
    int display_index = display_index_for_output(runtime, output);
    const BlitFont *font = &runtime->hud_font;
    int cw = blit_font_width(font), ch = blit_font_height(font);
    int graph = (runtime->hud_sections & BLIT_HUD_SECTION_GRAPH) != 0;
    int heading[BLIT_HUD_LINES], bars[BLIT_HUD_GRAPH];
    int graph_after = 0, width, height, x, y, i, full, dirty_top, dirty_bottom;
    int inner, graph_top = 0;
    double refresh_hz = output_refresh_hz(runtime, output), interval_ms = 1000.0 / 60.0;
    unsigned long long layout = 1469598103934665603ULL, graph_signature = 0;
    /* Only the background and the graph's fill take hud.opacity; ink,
       border, bars and the limit line stay opaque, so it still reads. */
    uint32_t alpha = (uint32_t)(runtime->hud_opacity * 255 / 100);
    uint32_t background = (BLIT_HUD_BACKGROUND & 0x00ffffffU) | alpha << 24;
    uint32_t graph_fill = (BLIT_HUD_GRAPH_FILL & 0x00ffffffU) | alpha << 24;
    BlitKmsDisplayInfo info;

    if (display_index < 0 || blit_kms_display_info(runtime->kms, display_index, &info) != 0)
        return 0;
    if (refresh_hz >= 1.0)
        interval_ms = 1000.0 / refresh_hz;
    panel->line_count = 0;
    if (runtime->hud_sections & BLIT_HUD_SECTION_BLIT)
        hud_display_lines(runtime, output, panel, heading, &info, refresh_hz);
    graph_after = panel->line_count;
    if (runtime->hud_sections & BLIT_HUD_SECTION_HIBR) {
        const BlitSession *session = hud_status_session(runtime, output, now);
        if (session != NULL) {
            hud_line(panel, heading, 1, "hibr  (%llu s)",
                     (unsigned long long)((now - session->status_us) / 1000000ULL));
            for (i = 0; i < session->status_count; ++i)
                hud_line(panel, heading, 0, "%s", session->status_lines[i]);
        }
    }
    if (graph && panel->line_count > graph_after)
        graph_after = panel->line_count;  /* the graph after hibr's lines */
    if (first && (runtime->hud_sections & BLIT_HUD_SECTION_BLIT))
        hud_blit_lines(runtime, panel, heading);
    if (panel->line_count == 0 && !graph) {
        int had = panel->width > 0;
        panel->width = panel->height = 0;
        panel->layout = 0;
        if (had) panel->revision = hud_next_revision();
        return had;
    }
    width = BLIT_HUD_COLUMNS * cw + 2 * BLIT_HUD_PAD + 2;
    inner = width - 2 - 2 * BLIT_HUD_PAD;
    height = panel->line_count * ch + (graph ? BLIT_HUD_GRAPH_HEIGHT + BLIT_HUD_PAD : 0) +
        2 * BLIT_HUD_PAD + 2;
    x = runtime->hud_corner & 1 ? info.width - BLIT_HUD_MARGIN - width : BLIT_HUD_MARGIN;
    y = runtime->hud_corner & 2 ? info.height - BLIT_HUD_MARGIN - height : BLIT_HUD_MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    /* The bars, oldest left, newest at the right edge: each second's worst
       frame time (render.outputs[].last_present_ms), the full height twice
       the refresh interval, so the interval is the line through the
       middle. */
    for (i = 0; graph && i < BLIT_HUD_GRAPH; ++i) {
        double ms = recent->graph[(recent->graph_next + i) % BLIT_HUD_GRAPH];
        int h = (int)(ms / (2.0 * interval_ms) * BLIT_HUD_GRAPH_HEIGHT + 0.5);
        if (ms > 0 && h < 1) h = 1;  /* a second that drew anything shows */
        bars[i] = (h > BLIT_HUD_GRAPH_HEIGHT ? BLIT_HUD_GRAPH_HEIGHT : h) |
            (ms > interval_ms ? 0x100 : 0);
    }
    if (graph)
        graph_signature = hud_hash(1469598103934665603ULL, bars, sizeof(bars));
    layout = hud_hash(layout, &x, sizeof(x));
    layout = hud_hash(layout, &y, sizeof(y));
    layout = hud_hash(layout, &height, sizeof(height));
    layout = hud_hash(layout, &graph, sizeof(graph));
    layout = hud_hash(layout, &graph_after, sizeof(graph_after));
    layout = hud_hash(layout, &panel->line_count, sizeof(panel->line_count));
    layout = hud_hash(layout, &runtime->hud_opacity, sizeof(runtime->hud_opacity));
    full = layout != panel->layout || panel->width != width || panel->rgba == NULL;
    if (full) {
        if ((size_t)width * (size_t)height * 4 > panel->capacity) {
            unsigned char *rgba = realloc(panel->rgba, (size_t)width * (size_t)height * 4);
            if (rgba == NULL)
                return 0;
            panel->rgba = rgba;
            panel->capacity = (size_t)width * (size_t)height * 4;
        }
        panel->x = x;
        panel->y = y;
        panel->width = width;
        panel->height = height;
        panel->layout = layout;
        hud_fill(panel, 0, 0, width, 1, BLIT_HUD_BORDER);
        hud_fill(panel, 0, height - 1, width, 1, BLIT_HUD_BORDER);
        hud_fill(panel, 0, 1, 1, height - 2, BLIT_HUD_BORDER);
        hud_fill(panel, width - 1, 1, 1, height - 2, BLIT_HUD_BORDER);
        hud_fill(panel, 1, 1, width - 2, height - 2, background);
    }
    dirty_top = height;
    dirty_bottom = 0;
    for (i = 0; i < panel->line_count; ++i) {
        int top = 1 + BLIT_HUD_PAD + i * ch +
            (graph && i >= graph_after ? BLIT_HUD_GRAPH_HEIGHT + BLIT_HUD_PAD : 0);
        unsigned long long signature = hud_hash(1469598103934665603ULL, panel->lines[i],
                                                strlen(panel->lines[i]));
        signature = hud_hash(signature, &heading[i], sizeof(heading[i]));
        if (!full && signature == panel->line_signature[i])
            continue;
        panel->line_signature[i] = signature;
        if (!full)
            hud_fill(panel, 1 + BLIT_HUD_PAD, top, inner, ch, background);
        hud_text(font, panel, 1 + BLIT_HUD_PAD, top, panel->lines[i], BLIT_HUD_COLUMNS,
                 heading[i] ? BLIT_HUD_HEADING : BLIT_HUD_TEXT);
        if (top < dirty_top) dirty_top = top;
        if (top + ch > dirty_bottom) dirty_bottom = top + ch;
    }
    if (graph)
        graph_top = 1 + BLIT_HUD_PAD + graph_after * ch + BLIT_HUD_PAD / 2;
    if (graph && full)
        hud_text(font, panel, 1 + BLIT_HUD_PAD,
                 graph_top + (BLIT_HUD_GRAPH_HEIGHT - ch) / 2, "60 s", 4, BLIT_HUD_TEXT);
    if (graph && (full || graph_signature != panel->graph_signature)) {
        /* Sixty bars across what is left of the width, one pixel apart. */
        int left = 1 + BLIT_HUD_PAD + 5 * cw, span = inner - 5 * cw;
        panel->graph_signature = graph_signature;
        hud_fill(panel, left, graph_top, span, BLIT_HUD_GRAPH_HEIGHT, graph_fill);
        for (i = 0; i < BLIT_HUD_GRAPH; ++i) {
            int x0 = left + i * span / BLIT_HUD_GRAPH;
            int x1 = left + (i + 1) * span / BLIT_HUD_GRAPH - 1;
            int h = bars[i] & 0xff;
            hud_fill(panel, x0, graph_top + BLIT_HUD_GRAPH_HEIGHT - h, x1 - x0, h,
                     bars[i] & 0x100 ? BLIT_HUD_BAR_LATE : BLIT_HUD_BAR_OK);
        }
        hud_fill(panel, left, graph_top + BLIT_HUD_GRAPH_HEIGHT / 2, span, 1, BLIT_HUD_LIMIT);
        if (graph_top < dirty_top) dirty_top = graph_top;
        if (graph_top + BLIT_HUD_GRAPH_HEIGHT > dirty_bottom)
            dirty_bottom = graph_top + BLIT_HUD_GRAPH_HEIGHT;
    }
    if (!full && dirty_bottom <= dirty_top)
        return 0;
    /* The rows that differ from the picture before, for a texture holding
       that one; after a full redraw, every row. */
    panel->base_revision = full ? 0 : panel->revision;
    panel->dirty_y = full ? 0 : dirty_top;
    panel->dirty_height = full ? height : dirty_bottom - dirty_top;
    panel->revision = hud_next_revision();
    return 1;
}

/* Every output's panel; the mask of outputs whose revision changed. */
static uint32_t hud_update(BlitRuntime *runtime, uint64_t now)
{
    uint32_t changed = 0;
    int i, first = 1;
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        BlitHudPanel *panel = &runtime->hud_panels[i];
        if (hud_shows_output(runtime, i)) {
            if (hud_build(runtime, i, first, now))
                changed |= 1U << i;
            first = 0;
        } else if (panel->width > 0) {
            panel->width = panel->height = 0;
            panel->layout = 0;
            panel->revision = hud_next_revision();
            changed |= 1U << i;
        }
    }
    hud_attach(runtime);
    return changed;
}

static void hud_tick(BlitRuntime *runtime, uint64_t now)
{
    uint64_t started, period;
    uint32_t changed;
    if (!runtime->hud_enabled || now < runtime->hud_next_us)
        return;
    period = 1000000ULL / (uint64_t)(runtime->hud_hz > 0 ? runtime->hud_hz : 2);
    started = monotonic_us();
    changed = hud_update(runtime, now);
    runtime->hud_last_update_ms = (double)(monotonic_us() - started) / 1000.0;
    runtime->hud_update_total_ms += runtime->hud_last_update_ms;
    if (runtime->hud_last_update_ms > runtime->hud_max_update_ms)
        runtime->hud_max_update_ms = runtime->hud_last_update_ms;
    ++runtime->hud_updates;
    hud_schedule(runtime, changed, now);
    /* Just after a vblank, as an animation frame is: the whole refresh to
       compose in. */
    runtime->hud_next_us = runtime->output_count > 0 ?
        animation_after_vblank(runtime, 0, now + period, now) : now + period;
}

/* Apply document.hud. Only the state: a settings write resets to the
   defaults and applies the new document in one turn, so what follows from
   it -- panels cleared, HUD1 sent -- waits for hud_reconcile, and a write
   that leaves the panel as it was changes nothing a client can see. */
static void settings_apply_hud(BlitRuntime *runtime)
{
    BlitHudSettings settings;
    if (!settings_hud_parse(runtime->settings_document, &settings)) {
        diagnostic_log(runtime, "warning", "invalid_settings_hud",
                       "The stored performance panel settings are invalid; the defaults apply until they are corrected through the settings API.");
        settings_hud_parse(NULL, &settings);
    }
    runtime->hud_enabled = settings.enabled;
    runtime->hud_corner = settings.corner;
    runtime->hud_hz = settings.hz;
    runtime->hud_opacity = settings.opacity;
    runtime->hud_sections = settings.sections;
    snprintf(runtime->hud_display_id, sizeof(runtime->hud_display_id), "%s",
             settings.display_id);
    if (runtime->hud_enabled && !runtime->hud_font_loaded) {
        if (blit_font_load_path(&runtime->hud_font,
                                blit_font_root_file("Blit-Terminus16.psf")) != 0)
            fprintf(stderr, "blit: performance panel font unavailable; using the built-in fallback\n");
        runtime->hud_font_loaded = 1;
    }
    /* On, it is rebuilt at the next turn; an unchanged picture keeps its
       revision and costs no frame. */
    runtime->hud_next_us = runtime->hud_enabled ? 1 : 0;
}

/* Once a turn, after the API: a panel switched off is taken away -- the
   compositor sees it gone from where it was and repaints that output -- and
   frame clients are told of any change in HUD1. Off, with nothing shown,
   this is a scan of the panels and of the frame clients. */
static void hud_reconcile(BlitRuntime *runtime)
{
    uint32_t cleared = 0;
    int i;
    for (i = 0; !runtime->hud_enabled && i < BLIT_MAX_DISPLAYS; ++i) {
        BlitHudPanel *panel = &runtime->hud_panels[i];
        if (panel->width < 1)
            continue;
        cleared |= 1U << i;
        panel->width = panel->height = 0;
        panel->layout = 0;
        panel->revision = hud_next_revision();
    }
    if (cleared) {
        hud_attach(runtime);
        hud_schedule(runtime, cleared, monotonic_us());
    }
    frame_hud_notify(runtime);
}

static size_t hud_settings_json(const BlitRuntime *runtime, char *out, size_t capacity)
{
    size_t used = 0;
    int i, first = 1;
    out[0] = '\0';
    json_appendf(out, capacity, &used,
                 "{\"enabled\":%s,\"display_id\":\"%s\",\"corner\":\"%s\",\"hz\":%d,"
                 "\"opacity\":%d,\"sections\":[",
                 runtime->hud_enabled ? "true" : "false", runtime->hud_display_id,
                 hud_corners[runtime->hud_corner & 3], runtime->hud_hz,
                 runtime->hud_opacity);
    for (i = 0; i < 3; ++i)
        if (runtime->hud_sections & (1u << i)) {
            json_appendf(out, capacity, &used, "%s\"%s\"", first ? "" : ",",
                         hud_section_names[i]);
            first = 0;
        }
    json_appendf(out, capacity, &used, "]}");
    return used;
}

/* render.outputs[].recent in /v1/diagnostics: the output's last second and
   its worst times in the last ten, the numbers its panel shows. */
static void recent_json(const BlitRuntime *runtime, int output, char *out, size_t capacity)
{
    const BlitOutputRecent *r = &runtime->output_recent[output];
    size_t used = 0;
    out[0] = '\0';
    if (output < 0 || output >= BLIT_MAX_DISPLAYS) {
        json_appendf(out, capacity, &used, "null");
        return;
    }
    json_appendf(out, capacity, &used,
                 "{\"draws_hz\":%.3f,\"late_flips_hz\":%.3f,\"missed_deadlines_hz\":%.3f,"
                 "\"why_hz\":{\"effects\":%.3f,\"video\":%.3f,\"pointer\":%.3f,\"text\":%.3f,"
                 "\"other\":%.3f},"
                 "\"descriptor_frames_hz\":%.3f,\"kitty_hz\":%.3f,\"sixel_hz\":%.3f,"
                 "\"frames_refused_hz\":%.3f,\"draw_ms\":%.3f,\"draw_max_ms\":%.3f,"
                 "\"flip_ms\":%.3f,\"flip_max_ms\":%.3f,\"window_s\":%d}",
                 r->draws_hz, r->late_hz, r->missed_hz, r->effects_hz, r->pictures_hz,
                 r->pointer_hz, r->text_hz, r->other_hz, r->descriptor_hz, r->kitty_hz, r->sixel_hz,
                 r->refused_hz, r->draw_ms, recent_worst(r->draw_worst, r->draw_second),
                 r->flip_ms, recent_worst(r->flip_worst, r->flip_second),
                 BLIT_RECENT_SECONDS);
}

/* /v1/diagnostics "hud": the settings in force, the panel's cost, frames
   presented only for it (the outputs' hud_frames), Blit's own last seconds
   (the panel's "blit" lines), and each panel as drawn, its lines included. */
static void hud_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    char settings[256], glyph_percent[16] = "null";
    unsigned long long frames = 0, rejected, dropped, keys;
    size_t used = 0;
    int i, j, first = 1, glyphs;
    for (i = 0; i < runtime->output_count; ++i) {
        BlitKmsDisplayInfo info;
        int display_index = display_index_for_output(runtime, i);
        if (display_index >= 0 &&
            blit_kms_display_info(runtime->kms, display_index, &info) == 0)
            frames += info.hud_frames;
    }
    hud_settings_json(runtime, settings, sizeof(settings));
    recent_errors(runtime, &rejected, &dropped, &keys);
    glyphs = recent_glyph_percent(runtime);
    if (glyphs >= 0) snprintf(glyph_percent, sizeof(glyph_percent), "%d", glyphs);
    body[0] = '\0';
    json_appendf(body, capacity, &used,
                 "%.*s,\"frames\":%llu,\"updates\":%llu,\"last_update_ms\":%.3f,"
                 "\"mean_update_ms\":%.3f,\"max_update_ms\":%.3f,"
                 "\"recent\":{\"window_s\":%d,\"loop_gap_max_ms\":%.3f,\"input_idle\":%s,"
                 "\"input_last_ms\":%.3f,\"input_max_ms\":%.3f,\"glyph_hit_percent\":%s,"
                 "\"errors\":{\"graphics_rejected\":%llu,\"stream_dropped_events\":%llu,"
                 "\"keys_dropped\":%llu}},\"panels\":[",
                 (int)strlen(settings) - 1, settings, frames, runtime->hud_updates,
                 runtime->hud_last_update_ms,
                 runtime->hud_updates ? runtime->hud_update_total_ms /
                     (double)runtime->hud_updates : 0.0,
                 runtime->hud_max_update_ms, BLIT_RECENT_SECONDS,
                 recent_worst(runtime->loop_worst, runtime->loop_second),
                 runtime->input_idle ? "true" : "false",
                 runtime->diagnostic_last_input_dispatch_ms,
                 recent_worst(runtime->input_worst, runtime->input_second),
                 glyph_percent, rejected, dropped, keys);
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        const BlitHudPanel *panel = &runtime->hud_panels[i];
        if (!runtime->hud_enabled || panel->width < 1)
            continue;
        json_appendf(body, capacity, &used,
                     "%s{\"display_id\":\"display-%llu\",\"x\":%d,\"y\":%d,\"width\":%d,"
                     "\"height\":%d,\"revision\":%llu,\"lines\":[",
                     first ? "" : ",", runtime->outputs[i].display_id, panel->x,
                     panel->y, panel->width, panel->height, panel->revision);
        for (j = 0; j < panel->line_count; ++j) {
            char escaped[BLIT_HUD_COLUMNS * 8];
            json_escape(panel->lines[j], escaped, sizeof(escaped));
            json_appendf(body, capacity, &used, "%s\"%s\"", j ? "," : "", escaped);
        }
        json_appendf(body, capacity, &used, "]}");
        first = 0;
    }
    json_appendf(body, capacity, &used, "]}");
}
