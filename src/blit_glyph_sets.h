#ifndef BLIT_GLYPH_SETS_H
#define BLIT_GLYPH_SETS_H

/* ADR 0243 step 3, the set level (docs/protocols/glyph-sets.md): every glyph
   set Blit answers for, looked up by set and name. A set is a table of
   names, not a face. The registry's sets level (assets/fonts/fonts.source.json)
   declares each names source; its generator derives the set ids and writes
   them, with the retired names, to blit_font_registry.h. The names
   themselves are read here, once, at start, from the packaged files in the
   fonts root, into one table sorted by set and name (bytewise): the same
   file /v1/fonts/icons serves is the one the blit set is read from, and the
   blit set's version is its SHA-256, the catalogue's revision.

   Nothing here is asked with a request's bytes until they have passed the
   address grammar (blit_glyph_set_id_valid, blit_glyph_name_valid), and a
   lookup is a binary search: it never opens a file, and nothing loops over
   a count a request supplies. A names file that is missing, too large, not
   the JSON this build reads, or holds a name outside the grammar, a
   duplicate or a codepoint in the registration block, is not served at all
   (its sets are unknown) and the reason is kept for the diagnostics log;
   the server starts either way.

   ADR 0245: installed sets (the glyph store, blit_glyph_store.h) join the
   same table. The shipped sets are read once, into blit_glyph_shipped;
   whenever the installed sets change, a new table is built from a copy of
   the shipped one plus the installed sets' names (blit_glyph_table_begin,
   blit_glyph_table_add_set, glyph_add, blit_glyph_table_finish), with the
   same grammar, the same alias rule and the same derived ranges, and
   swapped in whole (blit_glyph_table_swap). blit_glyph_sets is always the
   table in use. Building reads only the shipped table, which never changes
   after start, so an import's worker thread builds a table while the loop
   reads the one in use (Gitea #216); the swap is the loop's. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "blit_font.h"
#include "blit_font_registry.h"
#include "blit_sha256.h"

#define BLIT_GLYPH_SET_ID_MAX 32    /* octets, glyph sets section 4.1 */
#define BLIT_GLYPH_NAME_MAX 96
#define BLIT_GLYPH_PAGE_MAX 128     /* names per page, section 7.4 */
#define BLIT_GLYPH_FILE_MAX (4u << 20)

#define BLIT_GLYPH_FLAG_DEPRECATED 1u
#define BLIT_GLYPH_FLAG_RETIRED 2u

typedef struct {
    uint32_t name;        /* offset of the name in the pool */
    uint32_t codepoint;   /* 0 for a retired name */
    int32_t alias;        /* index of the canonical name; -1 when it is one */
    uint32_t by;          /* its first replacement in by[] */
    uint16_t set;
    uint8_t flags;
    uint8_t by_count;
    uint8_t tile[4];      /* column, row, columns, rows; columns 0: no tile */
} BlitGlyphName;

typedef struct {
    const char *id, *prefix;
    int source, loaded;             /* source: its names source; -1 installed */
    size_t first, end;              /* its names in the table */
    size_t listed;                  /* not deprecated, not retired */
    size_t range_first, range_count;
    /* What the listing reports: a shipped set's from its source, an
       installed set's from the store (ADR 0245 section 8). */
    const char *owner, *scope, *faces, *version;
    int installed;                  /* the store's set index; -1 shipped */
} BlitGlyphSet;

typedef struct {
    const char *key, *owner, *scope, *format, *file, *version, *faces, *lists;
    char sha256[65];
    char error[192];
    int loaded;
} BlitGlyphSource;

typedef struct {
    BlitGlyphSource sources[BLIT_GLYPH_SOURCE_COUNT];
    BlitGlyphSet *sets;             /* the shipped sets, then the installed */
    int set_count, set_capacity;
    BlitGlyphName *names;
    size_t count, capacity;
    char *pool;
    size_t pool_used, pool_capacity;
    uint32_t *by;
    size_t by_count, by_capacity;
    uint32_t *ranges;               /* pairs: first, last */
    size_t range_count, range_capacity;
    long load_us;
    size_t bytes;
} BlitGlyphSets;

/* The shipped sets as read at start, and the table in use: the shipped
   one until a set is installed, then the last one committed. */
static BlitGlyphSets blit_glyph_shipped;
static BlitGlyphSets *blit_glyph_table = &blit_glyph_shipped;
#define blit_glyph_sets (*blit_glyph_table)

/* Section 4.1: owner *( "-" id-part ), 1-32 octets, a-z and 0-9, a letter
   first. */
static int blit_glyph_set_id_valid(const char *s, size_t n)
{
    size_t i;
    if (n < 1 || n > BLIT_GLYPH_SET_ID_MAX || s[0] < 'a' || s[0] > 'z') return 0;
    for (i = 1; i < n; ++i) {
        char c = s[i];
        if (c == '-') {
            if (s[i - 1] == '-' || i + 1 == n) return 0;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return 0;
        }
    }
    return 1;
}

/* name-start *95( name-char ): a-z or 0-9 first, then also - and _. */
static int blit_glyph_name_valid(const char *s, size_t n)
{
    size_t i;
    if (n < 1 || n > BLIT_GLYPH_NAME_MAX) return 0;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (i > 0 && (c == '-' || c == '_'))))
            return 0;
    }
    return 1;
}

static const char *blit_glyph_name_text(const BlitGlyphName *name)
{
    return blit_glyph_sets.pool + name->name;
}

/* Bytewise, as strcmp, against a name that is not terminated. */
static int glyph_name_compare(const char *a, const char *b, size_t n)
{
    size_t la = strlen(a);
    int c = memcmp(a, b, la < n ? la : n);
    if (c) return c;
    return la < n ? -1 : la > n ? 1 : 0;
}

/* ---- A small reader for the two JSON shapes this build reads. ---- */

typedef struct {
    const char *p, *end;
    int failed;
} GlyphJson;

static int gj_fail(GlyphJson *j)
{
    j->failed = 1;
    return 0;
}

static int gj_peek(GlyphJson *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
        ++j->p;
    return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int gj_take(GlyphJson *j, char c)
{
    if (j->failed || gj_peek(j) != (unsigned char)c) return gj_fail(j);
    ++j->p;
    return 1;
}

/* A string, into out when it is given (a string too long for it fails). An
   escape of anything outside ASCII, or of NUL, becomes 0x80, which no name
   or codepoint accepts. */
static int gj_string(GlyphJson *j, char *out, size_t capacity)
{
    size_t used = 0;
    const char *quote, *escape;
    if (!gj_take(j, '"')) return 0;
    /* The common case, a string with no escape, in one piece. */
    quote = memchr(j->p, '"', (size_t)(j->end - j->p));
    escape = quote ? memchr(j->p, '\\', (size_t)(quote - j->p)) : NULL;
    if (quote && !escape) {
        size_t n = (size_t)(quote - j->p), i;
        for (i = 0; i < n; ++i)
            if ((unsigned char)j->p[i] < 0x20) return gj_fail(j);
        if (out) {
            if (n + 1 > capacity) return gj_fail(j);
            memcpy(out, j->p, n);
            out[n] = '\0';
        }
        j->p = quote + 1;
        return 1;
    }
    while (j->p < j->end && *j->p != '"') {
        unsigned char c = (unsigned char)*j->p++;
        if (c < 0x20) return gj_fail(j);
        if (c == '\\') {
            if (j->p >= j->end) return gj_fail(j);
            c = (unsigned char)*j->p++;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                unsigned value = 0;
                int k;
                if (j->end - j->p < 4) return gj_fail(j);
                for (k = 0; k < 4; ++k) {
                    char h = *j->p++;
                    value <<= 4;
                    if (h >= '0' && h <= '9') value |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') value |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') value |= (unsigned)(h - 'A' + 10);
                    else return gj_fail(j);
                }
                c = value && value < 0x80 ? (unsigned char)value : 0x80;
                break;
            }
            default: return gj_fail(j);
            }
        }
        if (out) {
            if (used + 1 >= capacity) return gj_fail(j);
            out[used++] = (char)c;
        }
    }
    if (j->p >= j->end) return gj_fail(j);
    ++j->p;
    if (out) out[used] = '\0';
    return 1;
}

static int gj_skip(GlyphJson *j, int depth)
{
    int c = gj_peek(j);
    const char *start;
    if (j->failed || depth > 32) return gj_fail(j);
    if (c == '"') return gj_string(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        ++j->p;
        if (gj_peek(j) == close) {
            ++j->p;
            return 1;
        }
        for (;;) {
            if (c == '{' && (!gj_string(j, NULL, 0) || !gj_take(j, ':'))) return 0;
            if (!gj_skip(j, depth + 1)) return 0;
            if (gj_peek(j) == ',') {
                ++j->p;
                continue;
            }
            return gj_take(j, close);
        }
    }
    /* A number or a literal. */
    start = j->p;
    while (j->p < j->end && *j->p && strchr("+-.0123456789eEtrufalsn", *j->p)) ++j->p;
    return j->p > start ? 1 : gj_fail(j);
}

/* The next member of an object, its key read and the cursor at its value:
   1, or 0 at the object's end (or a failure). *first is 1 before the first
   call. */
static int gj_member(GlyphJson *j, int *first, char *key, size_t capacity)
{
    if (j->failed) return 0;
    if (*first) {
        *first = 0;
        if (!gj_take(j, '{')) return 0;
        if (gj_peek(j) == '}') {
            ++j->p;
            return 0;
        }
    } else if (gj_peek(j) == ',') {
        ++j->p;
    } else {
        gj_take(j, '}');
        return 0;
    }
    return gj_string(j, key, capacity) && gj_take(j, ':');
}

/* The next element of an array, the cursor at it: as gj_member. */
static int gj_element(GlyphJson *j, int *first)
{
    if (j->failed) return 0;
    if (*first) {
        *first = 0;
        if (!gj_take(j, '[')) return 0;
        if (gj_peek(j) == ']') {
            ++j->p;
            return 0;
        }
        return 1;
    }
    if (gj_peek(j) == ',') {
        ++j->p;
        return 1;
    }
    gj_take(j, ']');
    return 0;
}

/* A whole number 0..999999. */
static int gj_count(GlyphJson *j, long *value)
{
    long v = 0;
    int digits = 0;
    gj_peek(j);
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9' && digits < 7) {
        v = v * 10 + (*j->p++ - '0');
        ++digits;
    }
    if (!digits || digits > 6) return gj_fail(j);
    *value = v;
    return 1;
}

static int gj_bool(GlyphJson *j, int *value)
{
    gj_peek(j);
    if (j->end - j->p >= 4 && memcmp(j->p, "true", 4) == 0) {
        j->p += 4;
        *value = 1;
        return 1;
    }
    if (j->end - j->p >= 5 && memcmp(j->p, "false", 5) == 0) {
        j->p += 5;
        *value = 0;
        return 1;
    }
    return gj_fail(j);
}

/* 1-6 hex digits, a scalar value, never in the registration block or the
   rest of Blit's old icon range (section 9); 0 when it is not. */
static uint32_t glyph_codepoint(const char *text)
{
    uint32_t value = 0;
    size_t i, n = strlen(text);
    if (n < 1 || n > 6) return 0;
    for (i = 0; i < n; ++i) {
        char h = text[i];
        value <<= 4;
        if (h >= '0' && h <= '9') value |= (uint32_t)(h - '0');
        else if (h >= 'a' && h <= 'f') value |= (uint32_t)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') value |= (uint32_t)(h - 'A' + 10);
        else return 0;
    }
    if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu) ||
        (value >= 0x100000u && value <= 0x100494u))
        return 0;
    return value;
}

/* ---- The table. ---- */

static int glyph_grow(void **items, size_t *capacity, size_t need, size_t size)
{
    size_t next = *capacity ? *capacity : 64;
    void *grown;
    if (need <= *capacity) return 1;
    while (next < need) next *= 2;
    grown = realloc(*items, next * size);
    if (!grown) return 0;
    *items = grown;
    *capacity = next;
    return 1;
}

static int glyph_pool_add(BlitGlyphSets *g, const char *text, size_t n, uint32_t *offset)
{
    if (g->pool_used + n + 1 > UINT32_MAX ||
        !glyph_grow((void **)&g->pool, &g->pool_capacity, g->pool_used + n + 1, 1))
        return 0;
    memcpy(g->pool + g->pool_used, text, n);
    g->pool[g->pool_used + n] = '\0';
    *offset = (uint32_t)g->pool_used;
    g->pool_used += n + 1;
    return 1;
}

static BlitGlyphName *glyph_add(BlitGlyphSets *g, int set, const char *name, size_t n,
                                uint32_t codepoint, unsigned flags)
{
    BlitGlyphName *record;
    if (!blit_glyph_name_valid(name, n) ||
        !glyph_grow((void **)&g->names, &g->capacity, g->count + 1, sizeof(*g->names)))
        return NULL;
    record = &g->names[g->count];
    memset(record, 0, sizeof(*record));
    if (!glyph_pool_add(g, name, n, &record->name)) return NULL;
    record->codepoint = codepoint;
    record->alias = -1;
    record->set = (uint16_t)set;
    record->flags = (uint8_t)flags;
    record->by = (uint32_t)g->by_count;
    ++g->count;
    return record;
}

static int glyph_add_by(BlitGlyphSets *g, BlitGlyphName *record, const char *name, size_t n)
{
    uint32_t offset;
    if (!blit_glyph_name_valid(name, n) || record->by_count == 255 ||
        !glyph_pool_add(g, name, n, &offset) ||
        !glyph_grow((void **)&g->by, &g->by_capacity, g->by_count + 1, sizeof(*g->by)))
        return 0;
    g->by[g->by_count++] = offset;
    ++record->by_count;
    return 1;
}

static int glyph_list_has(const char *list, const char *word)
{
    size_t n = strlen(word);
    while (list && *list) {
        const char *comma = strchr(list, ',');
        size_t length = comma ? (size_t)(comma - list) : strlen(list);
        if (length == n && memcmp(list, word, n) == 0) return 1;
        list = comma ? comma + 1 : NULL;
    }
    return 0;
}

/* One entry of a catalogue list. */
static int glyph_catalogue_entry(BlitGlyphSets *g, GlyphJson *j, int set, char *error, size_t capacity)
{
    char key[64], id[128] = "", point[16] = "";
    long columns = 1, column = 0, rows = 1, row = 0, span = 0, position = 0;
    int first = 1, deprecated = 0, have_id = 0, have_point = 0;
    char by_names[8][128];
    int by_count = 0, i;
    BlitGlyphName *record;
    uint32_t codepoint;

    while (gj_member(j, &first, key, sizeof(key))) {
        if (strcmp(key, "id") == 0) have_id = gj_string(j, id, sizeof(id));
        else if (strcmp(key, "codepoint") == 0) have_point = gj_string(j, point, sizeof(point));
        else if (strcmp(key, "deprecated") == 0) gj_bool(j, &deprecated);
        else if (strcmp(key, "columns") == 0) gj_count(j, &columns);
        else if (strcmp(key, "column") == 0) gj_count(j, &column);
        else if (strcmp(key, "rows") == 0) gj_count(j, &rows);
        else if (strcmp(key, "row") == 0) gj_count(j, &row);
        else if (strcmp(key, "replaced_by") == 0) {
            int element = 1;
            while (gj_element(j, &element)) {
                if (by_count == 8) {
                    gj_fail(j);
                    break;
                }
                gj_string(j, by_names[by_count++], sizeof(by_names[0]));
            }
        } else if (strcmp(key, "cells") == 0) {
            int member = 1;
            char cell_key[32];
            while (gj_member(j, &member, cell_key, sizeof(cell_key))) {
                if (strcmp(cell_key, "span") == 0) gj_count(j, &span);
                else if (strcmp(cell_key, "position") == 0) gj_count(j, &position);
                else gj_skip(j, 1);
            }
        } else {
            gj_skip(j, 1);
        }
    }
    if (j->failed || !have_id || !have_point) {
        snprintf(error, capacity, "an entry is not an object with an id and a codepoint");
        return 0;
    }
    if ((codepoint = glyph_codepoint(point)) == 0) {
        snprintf(error, capacity, "an entry's codepoint is not a scalar outside the registration block");
        return 0;
    }
    if ((record = glyph_add(g, set, id, strlen(id), codepoint,
                            deprecated ? BLIT_GLYPH_FLAG_DEPRECATED : 0)) == NULL) {
        snprintf(error, capacity, "an id is outside the address grammar (or memory ran out)");
        return 0;
    }
    for (i = 0; i < by_count; ++i)
        if (!glyph_add_by(g, record, by_names[i], strlen(by_names[i]))) {
            snprintf(error, capacity, "a replaced_by name is outside the address grammar");
            return 0;
        }
    if (span > 1) {
        columns = span;
        column = position;
        rows = 1;
        row = 0;
    }
    if (columns > 1 || rows > 1) {
        if (columns > 255 || rows > 255 || column >= columns || row >= rows) {
            snprintf(error, capacity, "an entry's tile is outside its picture");
            return 0;
        }
        record->tile[0] = (uint8_t)column;
        record->tile[1] = (uint8_t)row;
        record->tile[2] = (uint8_t)columns;
        record->tile[3] = (uint8_t)rows;
    }
    return 1;
}

/* Blit's catalogue: the entries of the lists the registry names; every
   other member (its retired ranges, its metadata) is not a name. */
static int glyph_read_catalogue(BlitGlyphSets *g, const BlitGlyphSource *source, int set,
                                const char *text, size_t length, char *error, size_t capacity)
{
    GlyphJson j = {text, text + length, 0};
    char key[64];
    int first = 1, seen = 0, lists = 0;
    const char *list;
    for (list = source->lists; list && *list; list = strchr(list, ',') ? strchr(list, ',') + 1 : NULL)
        ++lists;
    while (gj_member(&j, &first, key, sizeof(key))) {
        int element = 1;
        if (!glyph_list_has(source->lists, key)) {
            gj_skip(&j, 0);
            continue;
        }
        ++seen;
        while (gj_element(&j, &element))
            if (!glyph_catalogue_entry(g, &j, set, error, capacity)) return 0;
    }
    if (j.failed || gj_peek(&j) != -1 || seen != lists) {
        snprintf(error, capacity, "the catalogue is not the JSON this build reads, or lacks one of its lists");
        return 0;
    }
    return 1;
}

/* Nerd Fonts' glyphnames.json: {"METADATA": {...}, "<prefix>-<rest>":
   {"char": ..., "code": "<hex>"}, ...}; the name <prefix>-<rest> is the
   address nerd-<prefix>/<rest>. */
static int glyph_read_nerd(BlitGlyphSets *g, const BlitGlyphSource *source, int index,
                           const char *text, size_t length, char *error, size_t capacity)
{
    GlyphJson j = {text, text + length, 0};
    char key[160], member[32], value[64];
    int first = 1, version_ok = 0;
    while (gj_member(&j, &first, key, sizeof(key))) {
        int inner = 1, set = -1, s;
        char *dash;
        if (strcmp(key, "METADATA") == 0) {
            while (gj_member(&j, &inner, member, sizeof(member))) {
                if (strcmp(member, "version") == 0) {
                    if (gj_string(&j, value, sizeof(value)))
                        version_ok = source->version && strcmp(value, source->version) == 0;
                } else {
                    gj_skip(&j, 1);
                }
            }
            continue;
        }
        if ((dash = strchr(key, '-')) == NULL) {
            snprintf(error, capacity, "a name without a prefix");
            return 0;
        }
        *dash = '\0';
        for (s = 0; s < BLIT_GLYPH_SET_COUNT; ++s)
            if (g->sets[s].source == index && g->sets[s].prefix && strcmp(g->sets[s].prefix, key) == 0)
                set = s;
        if (set < 0) {
            snprintf(error, capacity, "the names file has a prefix this build did not derive from it");
            return 0;
        }
        value[0] = '\0';
        while (gj_member(&j, &inner, member, sizeof(member))) {
            if (strcmp(member, "code") == 0) gj_string(&j, value, sizeof(value));
            else gj_skip(&j, 1);
        }
        if (j.failed) break;
        {
            uint32_t codepoint = glyph_codepoint(value);
            if (!codepoint) {
                snprintf(error, capacity, "a code is not a scalar outside the registration block");
                return 0;
            }
            if (!glyph_add(g, set, dash + 1, strlen(dash + 1), codepoint, 0)) {
                snprintf(error, capacity, "a name is outside the address grammar (or memory ran out)");
                return 0;
            }
        }
    }
    if (j.failed || gj_peek(&j) != -1) {
        snprintf(error, capacity, "the names file is not the JSON this build reads");
        return 0;
    }
    if (!version_ok) {
        snprintf(error, capacity, "the names file's METADATA version is not %s", source->version);
        return 0;
    }
    return 1;
}

static const char *glyph_sort_pool;
static const BlitGlyphSets *glyph_sort_table;

static int glyph_order(const void *a, const void *b)
{
    const BlitGlyphName *x = a, *y = b;
    if (x->set != y->set) return x->set < y->set ? -1 : 1;
    return strcmp(glyph_sort_pool + x->name, glyph_sort_pool + y->name);
}

static int glyph_codepoint_order(const void *a, const void *b)
{
    const BlitGlyphName *x = &glyph_sort_table->names[*(const size_t *)a];
    const BlitGlyphName *y = &glyph_sort_table->names[*(const size_t *)b];
    if (x->set != y->set) return x->set < y->set ? -1 : 1;
    if (x->codepoint != y->codepoint) return x->codepoint < y->codepoint ? -1 : 1;
    return strcmp(glyph_sort_pool + x->name, glyph_sort_pool + y->name);
}

static int glyph_u32_order(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static const struct {
    const char *set, *name, *replaced_by;
} glyph_retired[] = {
#define GLYPH_RETIRED_ROW(set_, name_, replaced_by_) {set_, name_, replaced_by_},
    BLIT_GLYPH_RETIRED(GLYPH_RETIRED_ROW)
#undef GLYPH_RETIRED_ROW
    {NULL, NULL, NULL}   /* the end: the list may be empty (C99 has no empty initializer) */
};

/* One names source into the table; on failure, nothing of it stays. */
static int glyph_load_source(BlitGlyphSets *g, int index)
{
    BlitGlyphSource *source = &g->sources[index];
    size_t start = g->count, pool_start = g->pool_used, by_start = g->by_count, length = 0, i;
    long size = 0;
    char *text = NULL;
    FILE *file = fopen(blit_font_root_file(source->file), "rb");
    int ok = 0, s;

    if (!file) {
        snprintf(source->error, sizeof(source->error), "%s: not readable", source->file);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 &&
        size <= (long)BLIT_GLYPH_FILE_MAX && fseek(file, 0, SEEK_SET) == 0 &&
        (text = malloc((size_t)size + 1)) != NULL)
        length = fread(text, 1, (size_t)size + 1, file);
    fclose(file);
    if (!text || length == 0 || length != (size_t)size) {
        snprintf(source->error, sizeof(source->error), "%s: empty, unreadable or over 4 MiB", source->file);
        free(text);
        return 0;
    }
    if (strcmp(source->format, "blit-catalogue") == 0) {
        int set = -1;
        for (s = 0; s < BLIT_GLYPH_SET_COUNT; ++s)
            if (g->sets[s].source == index) set = s;
        if (source->version == NULL) blit_sha256_hex(text, length, source->sha256);
        ok = set >= 0 && glyph_read_catalogue(g, source, set, text, length,
                                              source->error, sizeof(source->error));
    } else if (strcmp(source->format, "nerd-glyphnames") == 0) {
        ok = glyph_read_nerd(g, source, index, text, length, source->error, sizeof(source->error));
    } else {
        snprintf(source->error, sizeof(source->error), "unknown names format");
    }
    free(text);
    /* The names the registry carries as retired: never reused, never
       unknown. */
    for (i = 0; ok && glyph_retired[i].set; ++i)
        for (s = 0; ok && s < BLIT_GLYPH_SET_COUNT; ++s)
            if (g->sets[s].source == index && strcmp(g->sets[s].id, glyph_retired[i].set) == 0) {
                const char *name = glyph_retired[i].name, *list = glyph_retired[i].replaced_by;
                BlitGlyphName *record = glyph_add(g, s, name, strlen(name), 0, BLIT_GLYPH_FLAG_RETIRED);
                ok = record != NULL;
                while (ok && list && *list) {
                    const char *comma = strchr(list, ',');
                    size_t n = comma ? (size_t)(comma - list) : strlen(list);
                    ok = glyph_add_by(g, record, list, n);
                    list = comma ? comma + 1 : NULL;
                }
                if (!ok) snprintf(source->error, sizeof(source->error), "a retired name could not be added");
            }
    if (ok) {
        /* A name is unique within its set: a live name and a retired one,
           or two entries, with one name refuse the whole source. */
        glyph_sort_pool = g->pool;
        qsort(g->names + start, g->count - start, sizeof(*g->names), glyph_order);
        for (i = start + 1; i < g->count && ok; ++i)
            if (glyph_order(&g->names[i - 1], &g->names[i]) == 0) {
                snprintf(source->error, sizeof(source->error), "two entries in one set share a name");
                ok = 0;
            }
    }
    if (!ok) {
        g->count = start;
        g->pool_used = pool_start;
        g->by_count = by_start;
        return 0;
    }
    source->loaded = 1;
    return 1;
}

static void glyph_table_finish(BlitGlyphSets *g);

/* Read every names source the registry declares into the one table: at
   start, once. Returns how many sources were served. */
static int blit_glyph_sets_load(void)
{
    BlitGlyphSets *g = &blit_glyph_shipped;
    struct timespec t0, t1;
    int s, loaded = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    memset(g, 0, sizeof(*g));
    g->sets = calloc(BLIT_GLYPH_SET_COUNT, sizeof(*g->sets));
    if (!g->sets) return 0;
    g->set_count = g->set_capacity = BLIT_GLYPH_SET_COUNT;
    {
        int n = 0;
#define GLYPH_SOURCE(key_, owner_, scope_, format_, file_, version_, faces_, lists_) \
        g->sources[n].key = key_; g->sources[n].owner = owner_;                      \
        g->sources[n].scope = scope_; g->sources[n].format = format_;                \
        g->sources[n].file = file_; g->sources[n].version = version_;                \
        g->sources[n].faces = faces_; g->sources[n].lists = lists_; ++n;
        BLIT_GLYPH_SOURCES(GLYPH_SOURCE)
#undef GLYPH_SOURCE
        n = 0;
#define GLYPH_SET(id_, source_, prefix_) \
        g->sets[n].id = id_; g->sets[n].source = source_; g->sets[n].prefix = prefix_; \
        g->sets[n].installed = -1; ++n;
        BLIT_GLYPH_SETS(GLYPH_SET)
#undef GLYPH_SET
    }
    for (s = 0; s < BLIT_GLYPH_SOURCE_COUNT; ++s) loaded += glyph_load_source(g, s);
    for (s = 0; s < BLIT_GLYPH_SET_COUNT; ++s) {
        const BlitGlyphSource *source = &g->sources[g->sets[s].source];
        g->sets[s].owner = source->owner;
        g->sets[s].scope = source->scope;
        g->sets[s].faces = source->faces;
        g->sets[s].version = source->version ? source->version : source->sha256;
    }
    glyph_table_finish(g);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    g->load_us = (long)(t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000L;
    return loaded;
}

/* The table's derived parts, from its names: sorted by set and name, each
   set's run, the aliases (section 4.2) and the codepoint ranges. The same
   for the shipped table and for every table built with installed sets. */
static void glyph_table_finish(BlitGlyphSets *g)
{
    size_t i, *order = NULL, live = 0;
    int s;
    uint32_t *points = NULL;

    for (s = 0; s < g->set_count; ++s) {
        BlitGlyphSet *set = &g->sets[s];
        set->loaded = 0;
        set->first = set->end = set->listed = set->range_first = set->range_count = 0;
    }
    g->range_count = 0;
    for (i = 0; i < g->count; ++i) g->names[i].alias = -1;
    /* Each source's names are sorted already; the whole table is too
       when the sources' sets do not interleave, as now. */
    glyph_sort_pool = g->pool;
    glyph_sort_table = g;
    for (i = 1; i < g->count && glyph_order(&g->names[i - 1], &g->names[i]) < 0; ++i)
        ;
    if (i < g->count) qsort(g->names, g->count, sizeof(*g->names), glyph_order);
    for (i = 0; i < g->count; ++i) {
        BlitGlyphSet *set = &g->sets[g->names[i].set];
        if (!set->loaded) {
            set->loaded = 1;
            set->first = i;
        }
        set->end = i + 1;
        if (!(g->names[i].flags & (BLIT_GLYPH_FLAG_DEPRECATED | BLIT_GLYPH_FLAG_RETIRED))) ++set->listed;
        if (!(g->names[i].flags & BLIT_GLYPH_FLAG_RETIRED)) ++live;
    }
    for (s = 0; s < g->set_count; ++s)
        if (g->sets[s].source >= 0 && !g->sources[g->sets[s].source].loaded) g->sets[s].loaded = 0;
    /* Section 4.2: one glyph with several names in a set has one canonical
       name, the bytewise smallest; the rest are its aliases. Two sets that
       share a codepoint are not aliases of each other (4.3). */
    order = malloc((live ? live : 1) * sizeof(*order));
    points = malloc((live ? live : 1) * sizeof(*points));
    if (order && points) {
        size_t n = 0, run;
        for (i = 0; i < g->count; ++i)
            if (!(g->names[i].flags & BLIT_GLYPH_FLAG_RETIRED)) order[n++] = i;
        qsort(order, n, sizeof(*order), glyph_codepoint_order);
        for (i = 0; i < n; i = run) {
            for (run = i + 1; run < n && g->names[order[run]].set == g->names[order[i]].set &&
                 g->names[order[run]].codepoint == g->names[order[i]].codepoint; ++run)
                g->names[order[run]].alias = (int32_t)order[i];
        }
        /* The codepoint ranges each set covers: derived, for the listing. */
        for (s = 0; s < g->set_count; ++s) {
            BlitGlyphSet *set = &g->sets[s];
            size_t m = 0, k;
            if (!set->loaded) continue;
            for (k = set->first; k < set->end; ++k)
                if (!(g->names[k].flags & BLIT_GLYPH_FLAG_RETIRED)) points[m++] = g->names[k].codepoint;
            qsort(points, m, sizeof(*points), glyph_u32_order);
            set->range_first = g->range_count / 2;
            for (k = 0; k < m; ++k) {
                if (k && points[k] <= points[k - 1] + 1) {
                    g->ranges[g->range_count - 1] = points[k];
                    continue;
                }
                if (!glyph_grow((void **)&g->ranges, &g->range_capacity, g->range_count + 2,
                                sizeof(*g->ranges)))
                    break;
                g->ranges[g->range_count++] = points[k];
                g->ranges[g->range_count++] = points[k];
                ++set->range_count;
            }
        }
    }
    free(order);
    free(points);
    /* The table is read-only from here: give back what growth left over. */
    if (g->count) {
        void *fit = realloc(g->names, g->count * sizeof(*g->names));
        if (fit) { g->names = fit; g->capacity = g->count; }
    }
    if (g->pool_used) {
        void *fit = realloc(g->pool, g->pool_used);
        if (fit) { g->pool = fit; g->pool_capacity = g->pool_used; }
    }
    g->bytes = g->capacity * sizeof(*g->names) + g->pool_capacity +
               g->by_capacity * sizeof(*g->by) + g->range_capacity * sizeof(*g->ranges);
}

/* ---- ADR 0245: a table with the installed sets. ---- */

/* A new table: a copy of the shipped one, with room for INSTALLED sets
   more. NULL when memory runs out. */
static BlitGlyphSets *blit_glyph_table_begin(int installed)
{
    const BlitGlyphSets *shipped = &blit_glyph_shipped;
    BlitGlyphSets *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    *g = *shipped;
    g->names = NULL; g->pool = NULL; g->by = NULL; g->ranges = NULL; g->sets = NULL;
    g->capacity = g->pool_capacity = g->by_capacity = g->range_capacity = 0;
    g->count = g->pool_used = g->by_count = g->range_count = 0;
    g->set_capacity = shipped->set_count + installed;
    g->sets = calloc((size_t)(g->set_capacity ? g->set_capacity : 1), sizeof(*g->sets));
    if (!g->sets ||
        !glyph_grow((void **)&g->names, &g->capacity, shipped->count + 1, sizeof(*g->names)) ||
        !glyph_grow((void **)&g->pool, &g->pool_capacity, shipped->pool_used + 1, 1) ||
        !glyph_grow((void **)&g->by, &g->by_capacity, shipped->by_count + 1, sizeof(*g->by))) {
        free(g->sets); free(g->names); free(g->pool); free(g->by); free(g);
        return NULL;
    }
    memcpy(g->sets, shipped->sets, (size_t)shipped->set_count * sizeof(*g->sets));
    g->set_count = shipped->set_count;
    if (shipped->count) memcpy(g->names, shipped->names, shipped->count * sizeof(*g->names));
    if (shipped->pool_used) memcpy(g->pool, shipped->pool, shipped->pool_used);
    if (shipped->by_count) memcpy(g->by, shipped->by, shipped->by_count * sizeof(*g->by));
    g->count = shipped->count;
    g->pool_used = shipped->pool_used;
    g->by_count = shipped->by_count;
    return g;
}

/* An installed set's entry; its names are then added with glyph_add (and
   glyph_add_by), its retired ones with codepoint 0. -1 when it does not
   fit. ID, VERSION and FACES must outlive the table (the store's). */
static int blit_glyph_table_add_set(BlitGlyphSets *g, const char *id, const char *version,
                                    const char *faces, int installed)
{
    BlitGlyphSet *set;
    if (g->set_count >= g->set_capacity || g->set_count >= 65535) return -1;
    set = &g->sets[g->set_count];
    memset(set, 0, sizeof(*set));
    set->id = id;
    set->source = -1;
    set->owner = "x";
    set->scope = "terminal-local";
    set->faces = faces;
    set->version = version;
    set->installed = installed;
    return g->set_count++;
}

static void glyph_table_free(BlitGlyphSets *g)
{
    if (!g || g == &blit_glyph_shipped) return;
    free(g->sets); free(g->names); free(g->pool); free(g->by); free(g->ranges);
    free(g);
}

/* Derive what a new table needs (its sort, aliases and ranges): reads
   only G, so an import's worker does it aside (Gitea #216). */
static void blit_glyph_table_finish(BlitGlyphSets *g)
{
    glyph_table_finish(g);
}

/* Swap a finished table in whole; the table it replaces is freed. On the
   loop only: nothing reads the table between here and the next request. */
static void blit_glyph_table_swap(BlitGlyphSets *g)
{
    BlitGlyphSets *old = blit_glyph_table;
    blit_glyph_table = g;
    glyph_table_free(old);
}

static const char *blit_glyph_set_version(const BlitGlyphSet *set)
{
    return set->version;
}

/* A set by id, only one that is served; the id has passed the grammar. */
static const BlitGlyphSet *blit_glyph_set_find(const char *id, size_t n)
{
    int s;
    for (s = 0; s < blit_glyph_sets.set_count; ++s) {
        const BlitGlyphSet *set = &blit_glyph_sets.sets[s];
        if (set->loaded && strlen(set->id) == n && memcmp(set->id, id, n) == 0) return set;
    }
    return NULL;
}

/* The first name in the set bytewise after `after` (n octets; n 0 is the
   set's first name), as an index into the table. */
static size_t blit_glyph_name_after(const BlitGlyphSet *set, const char *after, size_t n)
{
    size_t low = set->first, high = set->end;
    if (n == 0) return low;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (glyph_name_compare(blit_glyph_name_text(&blit_glyph_sets.names[mid]), after, n) <= 0)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

static const BlitGlyphName *blit_glyph_name_find(const BlitGlyphSet *set, const char *name, size_t n)
{
    size_t low = set->first, high = set->end;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int c = glyph_name_compare(blit_glyph_name_text(&blit_glyph_sets.names[mid]), name, n);
        if (c == 0) return &blit_glyph_sets.names[mid];
        if (c < 0) low = mid + 1;
        else high = mid;
    }
    return NULL;
}

#endif
