#ifndef BLIT_DISPLAY_LAYOUT_H
#define BLIT_DISPLAY_LAYOUT_H
#include <ctype.h>

/* ADR0131. The settings document is the sole desired-layout owner. */
typedef struct {
    char output[128];
    int columns, rows;
} BlitDisplayLayout;

static int display_layouts_parse(const char *document, BlitDisplayLayout *layouts,
                                 int *count, int *present)
{
    const char *cursor = document, *next;
    char key[128], map[8192];
    static char object[BLIT_SETTINGS_CAPACITY];
    int i;
    *count = *present = 0;
    while (*cursor && *cursor != '{') ++cursor;
    if (!*cursor) return 1;
    ++cursor;
    while ((next = json_next_member(cursor, key, sizeof(key), object, sizeof(object)))) {
        cursor = next;
        if (strcmp(key, "display_layouts")) continue;
        if ((*present)++ || object[0] != '{' || strlen(object) >= sizeof(map)) return 0;
        snprintf(map, sizeof(map), "%s", object);
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor != '}') return 0;
    if (!*present) return 1;
    cursor = map + 1;
    while ((next = json_next_member(cursor, key, sizeof(key), object, sizeof(object)))) {
        cursor = next;
        BlitDisplayLayout *layout;
        const char *member;
        char field[32], value[64];
        unsigned seen = 0;
        size_t k;
        if (*count >= BLIT_MAX_DISPLAYS || !key[0] || object[0] != '{') return 0;
        for (k = 0; key[k]; ++k)
            if (!isalnum((unsigned char)key[k]) && key[k] != '-' && key[k] != '_' &&
                key[k] != '.' && key[k] != ':' && key[k] != ' ') return 0;
        for (i = 0; i < *count; ++i)
            if (!strcmp(layouts[i].output, key)) return 0;
        layout = &layouts[(*count)++];
        memset(layout, 0, sizeof(*layout));
        snprintf(layout->output, sizeof(layout->output), "%s", key);
        member = object + 1;
        while ((next = json_next_member(member, field, sizeof(field), value, sizeof(value)))) {
            member = next;
            int bit, number;
            char *end = value + strlen(value);
            while (end > value && isspace((unsigned char)end[-1])) *--end = 0;
            if (!strcmp(field, "columns")) bit = 1;
            else if (!strcmp(field, "rows")) bit = 2;
            else return 0;
            if (seen & bit || strlen(value) != 1 || value[0] < '1' || value[0] > '8') return 0;
            number = value[0] - '0';
            if (bit == 1) layout->columns = number; else layout->rows = number;
            seen |= bit;
        }
        while (isspace((unsigned char)*member)) ++member;
        if (*member != '}') return 0;
        if (seen != 3) return 0;
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor != '}') return 0;
    return 1;
}

static const char *display_selector(const BlitDisplay *display)
{
    return display->connector[0] ? display->connector : display->name;
}

static int display_layouts_bulk_fits(const BlitRuntime *runtime)
{
    BlitDisplayLayout layouts[BLIT_MAX_DISPLAYS];
    int count, present, total, i, j;
    if (!display_layouts_parse(runtime->settings_document, layouts, &count, &present)) return 0;
    total = count;
    for (i = 0; i < runtime->display_count; ++i) {
        for (j = 0; j < count; ++j)
            if (!strcmp(layouts[j].output, display_selector(&runtime->displays[i]))) break;
        if (j == count) ++total;
    }
    return total <= BLIT_MAX_DISPLAYS;
}

static void display_layout_resolve(const BlitRuntime *runtime, const char *document,
                                   const BlitDisplay *display, int *columns, int *rows)
{
    BlitDisplayLayout layouts[BLIT_MAX_DISPLAYS];
    int count, present, i;
    *columns = runtime->pane_columns > 0 ? runtime->pane_columns : 1;
    *rows = runtime->pane_rows > 0 ? runtime->pane_rows : 1;
    if (!display_layouts_parse(document, layouts, &count, &present)) {
        *columns = *rows = 1;
        return;
    }
    if (!present) return;
    *columns = *rows = 1;
    for (i = 0; i < count; ++i)
        if (!strcmp(layouts[i].output, display_selector(display))) {
            *columns = layouts[i].columns; *rows = layouts[i].rows;
            return;
        }
}

/* Legacy bulk API and one-time startup migration both use this projection.
   Offline entries survive; newly hotplugged outputs never call this. */
static void display_layouts_materialize(BlitRuntime *runtime, int force)
{
    BlitDisplayLayout layouts[BLIT_MAX_DISPLAYS];
    static char next[BLIT_SETTINGS_CAPACITY];
    static char value[BLIT_SETTINGS_CAPACITY];
    const char *cursor = runtime->settings_document;
    char key[128];
    size_t used = 0;
    int count, present, i, j, first = 1;
    if (!display_layouts_parse(cursor, layouts, &count, &present) || (present && !force)) return;
    if (runtime->pane_columns < 1) runtime->pane_columns = 1;
    if (runtime->pane_rows < 1) runtime->pane_rows = 1;
    json_appendf(next, sizeof(next), &used, "{");
    while (*cursor && *cursor != '{') ++cursor;
    if (*cursor) ++cursor;
    while ((cursor = json_next_member(cursor, key, sizeof(key), value, sizeof(value)))) {
        if (!strcmp(key, "display_layouts")) continue;
        json_appendf(next, sizeof(next), &used, "%s\"%s\":%s", first ? "" : ",", key, value);
        first = 0;
    }
    json_appendf(next, sizeof(next), &used, "%s\"display_layouts\":{", first ? "" : ",");
    first = 1;
    for (i = 0; i < count; ++i) {
        for (j = 0; j < runtime->display_count; ++j)
            if (!strcmp(layouts[i].output, display_selector(&runtime->displays[j]))) break;
        if (j < runtime->display_count) continue;
        json_appendf(next, sizeof(next), &used, "%s\"%s\":{\"columns\":%d,\"rows\":%d}",
                     first ? "" : ",", layouts[i].output, layouts[i].columns, layouts[i].rows);
        first = 0;
    }
    for (i = 0; i < runtime->display_count; ++i) {
        char escaped[256];
        json_escape(display_selector(&runtime->displays[i]), escaped, sizeof(escaped));
        json_appendf(next, sizeof(next), &used, "%s\"%s\":{\"columns\":%d,\"rows\":%d}",
                     first ? "" : ",", escaped, runtime->pane_columns, runtime->pane_rows);
        first = 0;
    }
    json_appendf(next, sizeof(next), &used, "}}");
    snprintf(runtime->settings_document, sizeof(runtime->settings_document), "%s", next);
    /* Non-default legacy grids must be durable before a future boot can
       enumerate additional outputs. Otherwise that boot re-inherits the
       old global split. Default1x1 needs no eager disk write. */
    if (runtime->pane_columns != 1 || runtime->pane_rows != 1)
        settings_mark_dirty(runtime);
}

/* Returns 400 for malformed/over-capacity intent, 409 if a live pane would
   disappear. No settings, PTY or geometry is changed during preflight. */
static int display_layouts_preflight(BlitRuntime *runtime, const char *document)
{
    BlitDisplayLayout layouts[BLIT_MAX_DISPLAYS];
    int count, present, i, j, total = 0;
    if (!display_layouts_parse(document, layouts, &count, &present)) return 400;
    if (!present) return 0; /* Legacy global validation remains at its API. */
    for (i = 0; i < runtime->display_count; ++i) {
        int columns, rows;
        display_layout_resolve(runtime, document, &runtime->displays[i], &columns, &rows);
        total += columns * rows;
        if (total > BLIT_MAX_PANES ||
            runtime->displays[i].bounds.w / columns < runtime_terminal_cell_width(runtime) ||
            runtime->displays[i].bounds.h / rows < runtime_terminal_cell_height(runtime)) return 400;
        for (j = 0; j < BLIT_MAX_SESSIONS; ++j) {
            BlitSession *session = &runtime->sessions[j];
            char prefix[128];
            int ordinal;
            if (!session->used || !session->pane_id[0]) continue;
            snprintf(prefix, sizeof(prefix), "pane-display-%llu-", runtime->displays[i].id);
            if (strncmp(session->pane_id, prefix, strlen(prefix))) continue;
            ordinal = atoi(session->pane_id + strlen(prefix));
            if (ordinal > columns * rows) return 409;
        }
    }
    return 0;
}
#endif
