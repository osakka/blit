/* ADR 0203: the performance panel. Blit draws it over the panes and
   overlays of the outputs it is shown on, in both compositors, from the
   counters the API already reports: it measures nothing of its own. Its
   text uses the boot PSF, loaded once into a font of its own, so it looks
   the same whatever font the terminals use.

   It is rebuilt at most hud.hz times a second and only while enabled; a
   rebuild that changes no pixel keeps the revision, and an output is
   presented for the panel only when its revision changed. Such a frame is
   counted as the output's hud_frames and left out of the frame rate the
   panel shows. Off, there is no timer and no cost.

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
#define BLIT_HUD_BAR 4  /* a sample's bar; one pixel apart */

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
    int enabled, corner, hz;
    unsigned sections;
    char display_id[32];
} BlitHudSettings;

/* document.hud: {"enabled": bool, "display_id": "display-N" | "all",
   "corner": one of hud_corners, "hz": 1..10, "sections": a subset of
   hud_section_names}. Strict like effects: an unknown or repeated member or
   a value out of range makes it invalid. What is absent takes the default:
   enabled from BLIT_HUD=1, every display, top right, 2 Hz, every section. */
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

/* The panels the compositor draws now. Called before every present. */
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

/* The worst frame time of each output in the current second, for the graph:
   read where the main loop sees that output present. */
static void hud_note_present(BlitRuntime *runtime, int output, double present_ms)
{
    if (!runtime->hud_enabled || output < 0 || output >= BLIT_MAX_DISPLAYS)
        return;
    if (present_ms > runtime->hud_panels[output].graph_second_ms)
        runtime->hud_panels[output].graph_second_ms = present_ms;
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

static int hud_columns(const char *text)
{
    int columns = 0;
    while (*text) {
        hud_utf8_next(&text);
        ++columns;
    }
    return columns;
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

static void hud_text(const BlitFont *font, BlitHudPanel *panel, int x, int y,
                     const char *text, uint32_t rgba)
{
    int cw = blit_font_width(font), ch = blit_font_height(font);
    while (*text && x + cw <= panel->width) {
        uint32_t cp = hud_utf8_next(&text);
        size_t glyph = blit_font_glyph_index(font, cp);
        int row;
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

static void hud_line(BlitHudPanel *panel, const char *format, ...)
{
    va_list arguments;
    if (panel->line_count >= BLIT_HUD_LINES)
        return;
    va_start(arguments, format);
    vsnprintf(panel->lines[panel->line_count], sizeof(panel->lines[0]), format, arguments);
    va_end(arguments);
    ++panel->line_count;
}

/* One output's panel: its text, from the values /v1/diagnostics and
   /v1/diagnostics/runtime report, then its pixels if they changed. Returns
   1 when the revision changed. */
static int hud_build(BlitRuntime *runtime, int output, uint64_t now)
{
    BlitHudPanel *panel = &runtime->hud_panels[output];
    int display_index = display_index_for_output(runtime, output);
    const BlitFont *font = &runtime->hud_font;
    int cw = blit_font_width(font), ch = blit_font_height(font);
    int graph = (runtime->hud_sections & BLIT_HUD_SECTION_GRAPH) != 0;
    int heading[BLIT_HUD_LINES];
    int bars[BLIT_HUD_GRAPH], columns = 0, width, height, x, y, i;
    double interval_ms = 1000.0 / 60.0;
    unsigned long long signature = 1469598103934665603ULL;
    BlitKmsDisplayInfo info;

    double refresh_hz = output_refresh_hz(runtime, output);

    if (display_index < 0 || blit_kms_display_info(runtime->kms, display_index, &info) != 0)
        return 0;
    if (refresh_hz >= 1.0)
        interval_ms = 1000.0 / refresh_hz;
    panel->line_count = 0;
    memset(heading, 0, sizeof(heading));
    if (runtime->hud_sections & BLIT_HUD_SECTION_BLIT) {
        double fps = runtime->diagnostic_frame_rate_hz;
        unsigned long long hits = 0, misses = 0;
        char reason[48];
        if (runtime->diagnostic_frame_rate_last_us == 0 ||
            now < runtime->diagnostic_frame_rate_last_us ||
            now - runtime->diagnostic_frame_rate_last_us > 2000000ULL)
            fps = 0.0;
        blit_raster_cache_counts(runtime->font.raster_state, &hits, &misses);
        heading[panel->line_count] = 1;
        hud_line(panel, "blit  display-%llu", runtime->outputs[output].display_id);
        hud_line(panel, "fps %.1f / %.1f Hz  %dx%d", fps, refresh_hz,
                 info.width, info.height);
        hud_line(panel, "missed %llu  late flips %llu",
                 runtime->diagnostic_frame_rate_missed_deadlines, info.flips_late);
        if (strcmp(info.compositor, "cpu") == 0)
            snprintf(reason, sizeof(reason), " (%s)",
                     strcmp(info.compositor_requested, "gles") != 0 ? "requested" :
                     info.compositor_reason[0] ? info.compositor_reason : "fallback");
        else
            reason[0] = '\0';
        /* The renderer's name, without the version after " | ". */
        hud_line(panel, "compositor %s%s%s%.*s", info.compositor, reason,
                 info.compositor_renderer[0] ? "  " : "",
                 (int)(strstr(info.compositor_renderer, " | ") != NULL ?
                       strstr(info.compositor_renderer, " | ") - info.compositor_renderer :
                       (long)strlen(info.compositor_renderer)),
                 info.compositor_renderer);
        hud_line(panel, "render %.1f/%.1f ms  present %.1f/%.1f ms",
                 runtime->diagnostic_last_render_ms,
                 runtime->diagnostic_frame_rate_max_render_ms,
                 info.last_present_ms, info.max_present_ms);
        hud_line(panel, "loop gap %.1f ms  input %.1f ms",
                 runtime->diagnostic_loop_window_max_gap_ms,
                 runtime->diagnostic_last_input_dispatch_ms);
        hud_line(panel, "kitty %.0f/s  sixel %.0f/s", runtime->hud_kitty_hz,
                 runtime->hud_sixel_hz);
        hud_line(panel, "frames %.0f/s  refused %llu  %s", runtime->hud_frames_hz,
                 runtime->frames_refused,
                 runtime->frames_last_error[0] ? runtime->frames_last_error : "-");
        hud_line(panel, "errors: rejected %llu  dropped events %llu  keys %llu",
                 runtime->graphics_payloads_rejected,
                 runtime->diagnostic_stream_dropped_events,
                 runtime->diagnostic_input_keys_dropped);
        hud_line(panel, "glyph cache %llu/%llu", hits, misses);
    }
    if (runtime->hud_sections & BLIT_HUD_SECTION_HIBR) {
        const BlitSession *session = hud_status_session(runtime, output, now);
        if (session != NULL) {
            heading[panel->line_count] = 1;
            hud_line(panel, "hibr  (%llu s)",
                     (unsigned long long)((now - session->status_us) / 1000000ULL));
            for (i = 0; i < session->status_count; ++i)
                hud_line(panel, "%s", session->status_lines[i]);
        }
    }
    for (i = 0; i < panel->line_count; ++i) {
        int n = hud_columns(panel->lines[i]);
        if (n > BLIT_HUD_COLUMNS) n = BLIT_HUD_COLUMNS;
        if (n > columns) columns = n;
    }
    /* A bar per second, the full height twice the refresh interval, so the
       interval is the line through the middle. */
    for (i = 0; graph && i < BLIT_HUD_GRAPH; ++i) {
        double ms = panel->graph[(panel->graph_next + i) % BLIT_HUD_GRAPH];
        int h = (int)(ms / (2.0 * interval_ms) * BLIT_HUD_GRAPH_HEIGHT + 0.5);
        bars[i] = (h > BLIT_HUD_GRAPH_HEIGHT ? BLIT_HUD_GRAPH_HEIGHT : h) |
            (ms > interval_ms ? 0x100 : 0);
    }
    width = columns * cw;
    if (graph && width < BLIT_HUD_GRAPH * (BLIT_HUD_BAR + 1) - 1)
        width = BLIT_HUD_GRAPH * (BLIT_HUD_BAR + 1) - 1;
    width += 2 * BLIT_HUD_PAD + 2;
    height = panel->line_count * ch + (graph ? BLIT_HUD_GRAPH_HEIGHT +
        (panel->line_count ? BLIT_HUD_PAD : 0) : 0) + 2 * BLIT_HUD_PAD + 2;
    if (panel->line_count == 0 && !graph) {
        int had = panel->width > 0;
        panel->width = panel->height = 0;
        if (had) panel->revision = hud_next_revision();
        return had;
    }
    x = runtime->hud_corner & 1 ? info.width - BLIT_HUD_MARGIN - width : BLIT_HUD_MARGIN;
    y = runtime->hud_corner & 2 ? info.height - BLIT_HUD_MARGIN - height : BLIT_HUD_MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    /* What the pixels and their place depend on; equal, nothing changed. */
#define HUD_MIX(v) do { signature ^= (unsigned long long)(v); signature *= 1099511628211ULL; } while (0)
    HUD_MIX(x); HUD_MIX(y); HUD_MIX(width); HUD_MIX(height); HUD_MIX(graph);
    HUD_MIX(font->data != NULL);
    for (i = 0; i < panel->line_count; ++i) {
        const char *c;
        HUD_MIX(heading[i]);
        for (c = panel->lines[i]; *c; ++c) HUD_MIX((unsigned char)*c);
        HUD_MIX(0x0a);
    }
    for (i = 0; graph && i < BLIT_HUD_GRAPH; ++i) HUD_MIX(bars[i]);
#undef HUD_MIX
    if (signature == panel->signature && panel->width > 0)
        return 0;
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
    panel->signature = signature;
    hud_fill(panel, 0, 0, width, 1, BLIT_HUD_BORDER);
    hud_fill(panel, 0, height - 1, width, 1, BLIT_HUD_BORDER);
    hud_fill(panel, 0, 1, 1, height - 2, BLIT_HUD_BORDER);
    hud_fill(panel, width - 1, 1, 1, height - 2, BLIT_HUD_BORDER);
    hud_fill(panel, 1, 1, width - 2, height - 2, BLIT_HUD_BACKGROUND);
    for (i = 0; i < panel->line_count; ++i)
        hud_text(font, panel, 1 + BLIT_HUD_PAD, 1 + BLIT_HUD_PAD + i * ch, panel->lines[i],
                 heading[i] ? BLIT_HUD_HEADING : BLIT_HUD_TEXT);
    if (graph) {
        int top = 1 + BLIT_HUD_PAD + panel->line_count * ch +
            (panel->line_count ? BLIT_HUD_PAD : 0);
        int left = 1 + BLIT_HUD_PAD;
        hud_fill(panel, left, top, width - 2 - 2 * BLIT_HUD_PAD, BLIT_HUD_GRAPH_HEIGHT,
                 BLIT_HUD_GRAPH_FILL);
        for (i = 0; i < BLIT_HUD_GRAPH; ++i) {
            int h = bars[i] & 0xff;
            hud_fill(panel, left + i * (BLIT_HUD_BAR + 1), top + BLIT_HUD_GRAPH_HEIGHT - h,
                     BLIT_HUD_BAR, h, bars[i] & 0x100 ? BLIT_HUD_BAR_LATE : BLIT_HUD_BAR_OK);
        }
        hud_fill(panel, left, top + BLIT_HUD_GRAPH_HEIGHT / 2,
                 width - 2 - 2 * BLIT_HUD_PAD, 1, BLIT_HUD_LIMIT);
    }
    panel->revision = hud_next_revision();
    return 1;
}

/* Every output's panel; the mask of outputs whose revision changed. */
static uint32_t hud_update(BlitRuntime *runtime, uint64_t now)
{
    uint32_t changed = 0;
    int i;
    /* The graph takes one sample a second; the rates are per second too. */
    if (runtime->hud_graph_us == 0 || now - runtime->hud_graph_us >= 1000000ULL) {
        for (i = 0; i < BLIT_MAX_DISPLAYS; ++i) {
            BlitHudPanel *panel = &runtime->hud_panels[i];
            if (runtime->hud_graph_us != 0) {
                panel->graph[panel->graph_next] = (float)panel->graph_second_ms;
                panel->graph_next = (panel->graph_next + 1) % BLIT_HUD_GRAPH;
            }
            panel->graph_second_ms = 0;
        }
        if (runtime->hud_graph_us != 0) {
            double seconds = (double)(now - runtime->hud_rates_us) / 1000000.0;
            runtime->hud_kitty_hz = (double)(runtime->graphics_payloads_kitty -
                                             runtime->hud_rates_kitty) / seconds;
            runtime->hud_sixel_hz = (double)(runtime->graphics_payloads_sixel -
                                             runtime->hud_rates_sixel) / seconds;
            runtime->hud_frames_hz = (double)(runtime->frames_accepted -
                                              runtime->hud_rates_frames) / seconds;
        }
        runtime->hud_graph_us = runtime->hud_rates_us = now;
        runtime->hud_rates_kitty = runtime->graphics_payloads_kitty;
        runtime->hud_rates_sixel = runtime->graphics_payloads_sixel;
        runtime->hud_rates_frames = runtime->frames_accepted;
    }
    for (i = 0; i < runtime->output_count && i < BLIT_MAX_DISPLAYS; ++i) {
        BlitHudPanel *panel = &runtime->hud_panels[i];
        if (hud_shows_output(runtime, i)) {
            if (hud_build(runtime, i, now))
                changed |= 1U << i;
        } else if (panel->width > 0) {
            panel->width = panel->height = 0;
            panel->signature = 0;
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
        panel->signature = 0;
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
                 "\"sections\":[",
                 runtime->hud_enabled ? "true" : "false", runtime->hud_display_id,
                 hud_corners[runtime->hud_corner & 3], runtime->hud_hz);
    for (i = 0; i < 3; ++i)
        if (runtime->hud_sections & (1u << i)) {
            json_appendf(out, capacity, &used, "%s\"%s\"", first ? "" : ",",
                         hud_section_names[i]);
            first = 0;
        }
    json_appendf(out, capacity, &used, "]}");
    return used;
}

/* /v1/diagnostics "hud": the settings in force, the panel's cost, frames
   presented only for it (the outputs' hud_frames), and each panel as drawn,
   its lines included. */
static void hud_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    char settings[256];
    unsigned long long frames = 0;
    size_t used = 0;
    int i, j, first = 1;
    for (i = 0; i < runtime->output_count; ++i) {
        BlitKmsDisplayInfo info;
        int display_index = display_index_for_output(runtime, i);
        if (display_index >= 0 &&
            blit_kms_display_info(runtime->kms, display_index, &info) == 0)
            frames += info.hud_frames;
    }
    hud_settings_json(runtime, settings, sizeof(settings));
    body[0] = '\0';
    json_appendf(body, capacity, &used,
                 "%.*s,\"frames\":%llu,\"updates\":%llu,\"last_update_ms\":%.3f,"
                 "\"mean_update_ms\":%.3f,\"max_update_ms\":%.3f,\"panels\":[",
                 (int)strlen(settings) - 1, settings, frames, runtime->hud_updates,
                 runtime->hud_last_update_ms,
                 runtime->hud_updates ? runtime->hud_update_total_ms /
                     (double)runtime->hud_updates : 0.0,
                 runtime->hud_max_update_ms);
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
