#ifndef BLIT_GLYPH_STORE_H
#define BLIT_GLYPH_STORE_H

/* ADR 0245 phase 2a: installed glyph sets, system scope.

   The store (/var/lib/blit/glyphs; BLIT_GLYPH_STORE for gates only) is a
   directory of root's, 0700, used only when it and every directory above
   it belong to root (or to the server's own user, which is root in the
   shipped service) and no one else can write them; a sticky directory of
   root's (/tmp) is allowed above it, since no one else can rename what is
   in it. Inside:

       ledger              the codepoint ledger, appended and fsynced
       system/<set>/       a set: its parts as verified, and record.json
       staging/            uploads and sets being built; emptied at start

   The ledger maps (scope, set, name) to a codepoint in U+102000-U+10FFFD,
   allocated in bytewise name order from one counter that only grows, so a
   codepoint is never given to another address; a name keeps its codepoint
   across updates and across uninstall and reinstall in the same scope. A
   name an update drops is retired there, for good:

       blit-glyph-ledger 1
       a 102000 system x-example house
       r system x-example old-name

   An import reads every part once into memory; the signature (Blit's
   release keys, src/blit_release_signature.h), the manifest's grammar and
   every part's SHA-256 are checked on those bytes before the names file is
   parsed or FreeType sees the font, and FreeType is handed the same bytes
   (FT_New_Memory_Face), which the set keeps. The parts are then written
   from memory into a new directory in staging, the ledger record is made
   durable, and the directory is renamed into system/. A failure removes
   the staging copy and changes nothing. All of that runs on a worker
   thread of the server's (blit_glyph_import_prepare, Gitea #216); the
   loop only swaps the result in (blit_glyph_import_commit). At every start each stored set is
   verified again the same way, so removing a key file revokes the sets it
   signed. The administrator's own unsigned set (origin own, HTTP and
   system only) is checked at start against the hashes recorded at import.

   Not here yet: user scope and views (2b), the frame socket's GLYPHS and
   the system-directory rescan (2c). */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "blit_glyph_sets.h"
#include "blit_release_signature.h"
#include "blit_sha256.h"

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

#define BLIT_GLYPH_STORE_DEFAULT "/var/lib/blit/glyphs"
#define BLIT_GLYPH_KEYS_DEFAULT "/usr/lib/blit/release-keys"

/* Section 3 and 10's limits. */
#define BLIT_GLYPH_LEDGER_FIRST 0x102000u
#define BLIT_GLYPH_LEDGER_LAST 0x10fffdu
#define BLIT_GLYPH_LEDGER_SIZE (BLIT_GLYPH_LEDGER_LAST - BLIT_GLYPH_LEDGER_FIRST + 1u)
#define BLIT_GLYPH_SYSTEM_QUOTA 24574u
#define BLIT_GLYPH_USER_QUOTA 8192u
#define BLIT_GLYPH_SETS_PER_SCOPE 64
#define BLIT_GLYPH_SETS_MAX 1024
#define BLIT_GLYPH_SET_GLYPHS_MAX 8192
#define BLIT_GLYPH_SET_NAMES_MAX 16384
#define BLIT_GLYPH_FACE_GLYPHS_MAX 65535
#define BLIT_GLYPH_SYSTEM_BYTES_MAX (512ull << 20)
#define BLIT_GLYPH_LEDGER_FILE_MAX (64u << 20)
#define BLIT_GLYPH_RECORD_MAX (4u << 20)

enum {
    BLIT_GLYPH_PART_MANIFEST,
    BLIT_GLYPH_PART_SIGNATURE,
    BLIT_GLYPH_PART_NAMES,
    BLIT_GLYPH_PART_FONT,
    BLIT_GLYPH_PART_LICENCE,
    BLIT_GLYPH_PART_COUNT
};

/* The parts in the order a body carries them, their names in the
   X-Blit-Glyph-Parts header and in a set's directory, and their limits. */
static const char *const blit_glyph_part_names[BLIT_GLYPH_PART_COUNT] = {
    "manifest", "signature", "names", "font", "licence"
};
static const size_t blit_glyph_part_max[BLIT_GLYPH_PART_COUNT] = {
    BLIT_GLYPH_MANIFEST_MAX, BLIT_GLYPH_SIGNATURE_BYTES, BLIT_GLYPH_FILE_MAX,
    16u << 20, 256u << 10
};

/* An answer: an HTTP status, a problem code and its detail (status 0: none). */
typedef struct {
    int status;
    const char *code;
    char detail[256];
} BlitGlyphResult;

static int glyph_fail(BlitGlyphResult *r, int status, const char *code, const char *format, ...)
{
    va_list args;
    r->status = status;
    r->code = code;
    va_start(args, format);
    vsnprintf(r->detail, sizeof(r->detail), format, args);
    va_end(args);
    return 0;
}

/* ---- The names file, format blit-glyph-names (section 4). ---- */

typedef struct {
    uint32_t name;      /* offset in the pool */
    uint32_t code;      /* the font's own codepoint: never drawn as itself */
    int32_t glyph;      /* -1 for a glyph's own name; else the glyph's index */
    uint8_t deprecated, by_count;
    uint32_t by;        /* the first replacement in by[] */
} GlyphStoreName;

typedef struct {
    char release[128], licence[128];
    GlyphStoreName *names;
    size_t count, capacity, glyphs;
    char *pool;
    size_t pool_used, pool_capacity;
    uint32_t *by;
    size_t by_count, by_capacity;
} GlyphStoreNames;

static void glyph_names_free(GlyphStoreNames *n)
{
    free(n->names);
    free(n->pool);
    free(n->by);
    memset(n, 0, sizeof(*n));
}

static const char *glyph_names_text(const GlyphStoreNames *n, uint32_t offset)
{
    return n->pool + offset;
}

static int glyph_names_pool(GlyphStoreNames *n, const char *text, uint32_t *offset)
{
    size_t length = strlen(text);
    if (n->pool_used + length + 1 > UINT32_MAX ||
        !glyph_grow((void **)&n->pool, &n->pool_capacity, n->pool_used + length + 1, 1))
        return 0;
    memcpy(n->pool + n->pool_used, text, length + 1);
    *offset = (uint32_t)n->pool_used;
    n->pool_used += length + 1;
    return 1;
}

static GlyphStoreName *glyph_names_add(GlyphStoreNames *n, const char *name, uint32_t code,
                                       int32_t glyph)
{
    GlyphStoreName *record;
    if (n->count >= BLIT_GLYPH_SET_NAMES_MAX ||
        !blit_glyph_name_valid(name, strlen(name)) ||
        !glyph_grow((void **)&n->names, &n->capacity, n->count + 1, sizeof(*n->names)))
        return NULL;
    record = &n->names[n->count];
    memset(record, 0, sizeof(*record));
    if (!glyph_names_pool(n, name, &record->name)) return NULL;
    record->code = code;
    record->glyph = glyph;
    record->by = (uint32_t)n->by_count;
    ++n->count;
    return record;
}

/* 1-6 hex digits, a Unicode scalar value other than 0: the font's code. */
static uint32_t glyph_store_code(const char *text)
{
    uint32_t value = 0;
    size_t i, length = strlen(text);
    if (length < 1 || length > 6) return 0;
    for (i = 0; i < length; ++i) {
        char h = text[i];
        value <<= 4;
        if (h >= '0' && h <= '9') value |= (uint32_t)(h - '0');
        else if (h >= 'a' && h <= 'f') value |= (uint32_t)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') value |= (uint32_t)(h - 'A' + 10);
        else return 0;
    }
    if (value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) return 0;
    return value;
}

/* An SPDX licence expression as the listing reports it: printable ASCII
   letters, digits, . + - ( ) and spaces, at most 127. */
static int glyph_licence_valid(const char *text)
{
    size_t i, length = strlen(text);
    if (length > 127) return 0;
    for (i = 0; i < length; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '+' || c == '-' || c == '(' || c == ')' || c == ' '))
            return 0;
    }
    return 1;
}

static const GlyphStoreNames *glyph_names_sorting;

static int glyph_names_index_order(const void *a, const void *b)
{
    const GlyphStoreNames *n = glyph_names_sorting;
    return strcmp(n->pool + n->names[*(const uint32_t *)a].name,
                  n->pool + n->names[*(const uint32_t *)b].name);
}

/* A name's index, by binary search over ORDER (the names' indices sorted by
   name); -1 when the set has no such name. */
static int glyph_names_find(const GlyphStoreNames *n, const uint32_t *order, const char *name)
{
    size_t low = 0, high = n->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int c = strcmp(n->pool + n->names[order[mid]].name, name);
        if (c == 0) return (int)order[mid];
        if (c < 0) low = mid + 1;
        else high = mid;
    }
    return -1;
}

/* Parse a names file (at most 4 MiB, already checked): the bounded reader,
   the address grammar and the limits of section 10. Glyph names come
   first in the result, sorted bytewise; their aliases after them. */
static int glyph_names_parse(const char *text, size_t length, GlyphStoreNames *out,
                             BlitGlyphResult *r)
{
    GlyphJson j = {text, text + length, 0};
    char key[BLIT_GLYPH_NAME_MAX + 2], member[32], value[160];
    char aliases[32][BLIT_GLYPH_NAME_MAX + 2], by[8][BLIT_GLYPH_NAME_MAX + 2];
    int first = 1, have_release = 0, have_glyphs = 0, alias_count, by_count, deprecated;
    GlyphStoreNames n;
    uint32_t *order = NULL;
    size_t i, k, glyphs;

    memset(&n, 0, sizeof(n));
    memset(out, 0, sizeof(*out));
    /* First pass: the glyphs, each with its aliases kept beside it for now
       as names whose `glyph` is the glyph's index. */
    while (gj_member(&j, &first, key, sizeof(key))) {
        if (strcmp(key, "format") == 0) {
            if (!gj_string(&j, value, sizeof(value)) || strcmp(value, "blit-glyph-names") != 0) {
                glyph_names_free(&n);
                return glyph_fail(r, 422, "glyph_names_invalid", "the names file's format is not blit-glyph-names");
            }
        } else if (strcmp(key, "release") == 0) {
            have_release = gj_string(&j, n.release, sizeof(n.release));
        } else if (strcmp(key, "licence") == 0) {
            if (!gj_string(&j, n.licence, sizeof(n.licence)) || !glyph_licence_valid(n.licence)) {
                glyph_names_free(&n);
                return glyph_fail(r, 422, "glyph_names_invalid", "the names file's licence is not an SPDX expression");
            }
        } else if (strcmp(key, "glyphs") == 0) {
            int inner = 1;
            have_glyphs = 1;
            while (gj_member(&j, &inner, key, sizeof(key))) {
                int field = 1;
                uint32_t code = 0;
                GlyphStoreName *record;
                int32_t index;
                alias_count = by_count = deprecated = 0;
                value[0] = '\0';
                while (gj_member(&j, &field, member, sizeof(member))) {
                    if (strcmp(member, "code") == 0) {
                        gj_string(&j, value, sizeof(value));
                    } else if (strcmp(member, "aliases") == 0) {
                        int element = 1;
                        while (gj_element(&j, &element)) {
                            if (alias_count == 32) { gj_fail(&j); break; }
                            gj_string(&j, aliases[alias_count++], sizeof(aliases[0]));
                        }
                    } else if (strcmp(member, "replaced_by") == 0) {
                        int element = 1;
                        while (gj_element(&j, &element)) {
                            if (by_count == 8) { gj_fail(&j); break; }
                            gj_string(&j, by[by_count++], sizeof(by[0]));
                        }
                    } else if (strcmp(member, "deprecated") == 0) {
                        gj_bool(&j, &deprecated);
                    } else {
                        gj_skip(&j, 2);
                    }
                }
                if (j.failed) break;
                if ((code = glyph_store_code(value)) == 0) {
                    glyph_names_free(&n);
                    return glyph_fail(r, 422, "glyph_names_invalid",
                                      "a glyph's code is not 1-6 hex digits of a Unicode scalar value");
                }
                index = (int32_t)n.count;
                if ((record = glyph_names_add(&n, key, code, -1)) == NULL) {
                    glyph_names_free(&n);
                    return glyph_fail(r, 422, "glyph_names_invalid",
                                      "a name is outside the address grammar, or the set has over 16384 names");
                }
                record->deprecated = (uint8_t)(deprecated != 0);
                for (k = 0; k < (size_t)by_count; ++k) {
                    uint32_t offset;
                    if (!blit_glyph_name_valid(by[k], strlen(by[k])) ||
                        !glyph_names_pool(&n, by[k], &offset) ||
                        !glyph_grow((void **)&n.by, &n.by_capacity, n.by_count + 1, sizeof(*n.by))) {
                        glyph_names_free(&n);
                        return glyph_fail(r, 422, "glyph_names_invalid", "a replaced_by name is outside the address grammar");
                    }
                    n.names[index].by_count++;
                    n.by[n.by_count++] = offset;
                }
                for (k = 0; k < (size_t)alias_count; ++k)
                    if (!glyph_names_add(&n, aliases[k], code, index)) {
                        glyph_names_free(&n);
                        return glyph_fail(r, 422, "glyph_names_invalid",
                                          "an alias is outside the address grammar, or the set has over 16384 names");
                    }
            }
        } else {
            gj_skip(&j, 1);
        }
    }
    if (j.failed || gj_peek(&j) != -1 || !have_glyphs || !have_release) {
        glyph_names_free(&n);
        return glyph_fail(r, 422, "glyph_names_invalid",
                          "the names file is not blit-glyph-names JSON with a release and glyphs");
    }
    if (!blit_release_version_valid(n.release)) {
        glyph_names_free(&n);
        return glyph_fail(r, 422, "glyph_names_invalid",
                          "the release is not a version (a digit first, then 0-9 A-Z a-z . + ~ - :)");
    }
    for (i = 0, glyphs = 0; i < n.count; ++i) glyphs += n.names[i].glyph < 0;
    if (glyphs == 0 || glyphs > BLIT_GLYPH_SET_GLYPHS_MAX) {
        glyph_names_free(&n);
        return glyph_fail(r, 422, "glyph_names_invalid", "a set has 1-8192 glyphs");
    }
    /* Reorder: glyph names bytewise, then their aliases, with each alias's
       glyph index renumbered. */
    order = malloc(n.count * sizeof(*order));
    if (!order) {
        glyph_names_free(&n);
        return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
    }
    {
        GlyphStoreNames sorted = n;
        uint32_t *renumber = malloc(n.count * sizeof(*renumber));
        size_t used = 0;
        sorted.names = malloc(n.count * sizeof(*sorted.names));
        if (!renumber || !sorted.names) {
            free(renumber);
            free(sorted.names);
            free(order);
            glyph_names_free(&n);
            return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
        }
        for (i = 0; i < n.count; ++i)
            if (n.names[i].glyph < 0) order[used++] = (uint32_t)i;
        glyph_names_sorting = &n;
        qsort(order, used, sizeof(*order), glyph_names_index_order);
        for (i = 0; i < n.count; ++i)
            if (n.names[i].glyph >= 0) order[used++] = (uint32_t)i;
        for (i = 0; i < n.count; ++i) renumber[order[i]] = (uint32_t)i;
        for (i = 0; i < n.count; ++i) {
            sorted.names[i] = n.names[order[i]];
            if (sorted.names[i].glyph >= 0) sorted.names[i].glyph = (int32_t)renumber[sorted.names[i].glyph];
        }
        free(renumber);
        free(n.names);
        n = sorted;
        n.capacity = n.count;
        n.glyphs = glyphs;
    }
    /* Every name once, glyphs and aliases together; every replacement a
       name of the set. */
    for (i = 0; i < n.count; ++i) order[i] = (uint32_t)i;
    glyph_names_sorting = &n;
    qsort(order, n.count, sizeof(*order), glyph_names_index_order);
    for (i = 1; i < n.count; ++i)
        if (strcmp(n.pool + n.names[order[i - 1]].name, n.pool + n.names[order[i]].name) == 0) {
            free(order);
            glyph_names_free(&n);
            return glyph_fail(r, 422, "glyph_names_invalid", "two entries in the set share a name");
        }
    for (i = 0; i < n.by_count; ++i)
        if (glyph_names_find(&n, order, n.pool + n.by[i]) < 0) {
            free(order);
            glyph_names_free(&n);
            return glyph_fail(r, 422, "glyph_names_invalid", "a replaced_by name is not a name of the set");
        }
    free(order);
    *out = n;
    return 1;
}

/* ---- The ledger (section 3). ---- */

typedef struct {
    uint32_t codepoint;     /* 0: a retirement */
    uint32_t scope, set, name;   /* offsets in the ledger's pool */
} GlyphLedgerEntry;

typedef struct {
    GlyphLedgerEntry *entries;
    size_t count, capacity;
    char *pool;
    size_t pool_used, pool_capacity;
    uint32_t next;              /* the next codepoint to allocate */
    size_t allocated, system_allocated;
    off_t size;                 /* the file's durable length; 0: no file yet */
} GlyphLedger;

#define BLIT_GLYPH_LEDGER_HEADER "blit-glyph-ledger 1\n"

static void glyph_ledger_free(GlyphLedger *l)
{
    free(l->entries);
    free(l->pool);
    memset(l, 0, sizeof(*l));
    l->next = BLIT_GLYPH_LEDGER_FIRST;
}

static int glyph_ledger_pool(GlyphLedger *l, const char *text, size_t length, uint32_t *offset)
{
    if (l->pool_used + length + 1 > UINT32_MAX ||
        !glyph_grow((void **)&l->pool, &l->pool_capacity, l->pool_used + length + 1, 1))
        return 0;
    memcpy(l->pool + l->pool_used, text, length);
    l->pool[l->pool_used + length] = '\0';
    *offset = (uint32_t)l->pool_used;
    l->pool_used += length + 1;
    return 1;
}

/* A scope as the ledger writes it: system, or user-<uid> (phase 2b). */
static int glyph_scope_valid(const char *s, size_t n)
{
    size_t i;
    if (n == 6 && memcmp(s, "system", 6) == 0) return 1;
    if (n < 6 || n > 15 || memcmp(s, "user-", 5) != 0 || (s[5] == '0' && n > 6)) return 0;
    for (i = 5; i < n; ++i)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

/* An installed set's id: x-, then the manifest name's grammar. */
static int blit_glyph_installed_id_valid(const char *id, size_t n)
{
    return n > 2 && id[0] == 'x' && id[1] == '-' && blit_glyph_manifest_name_valid(id + 2, n - 2);
}

static int glyph_ledger_add(GlyphLedger *l, uint32_t codepoint, const char *scope, size_t scope_n,
                            const char *set, size_t set_n, const char *name, size_t name_n)
{
    GlyphLedgerEntry *e;
    if (!glyph_grow((void **)&l->entries, &l->capacity, l->count + 1, sizeof(*l->entries)))
        return 0;
    e = &l->entries[l->count];
    if (!glyph_ledger_pool(l, scope, scope_n, &e->scope) ||
        !glyph_ledger_pool(l, set, set_n, &e->set) ||
        !glyph_ledger_pool(l, name, name_n, &e->name))
        return 0;
    e->codepoint = codepoint;
    ++l->count;
    if (codepoint) {
        l->next = codepoint + 1;
        ++l->allocated;
        if (scope_n == 6 && memcmp(scope, "system", 6) == 0) ++l->system_allocated;
    }
    return 1;
}

/* The entries of one scope and set, allocations and retirements, as
   indices sorted by name (each name's allocation first). */
static const GlyphLedger *glyph_ledger_sorting;

static int glyph_ledger_name_order(const void *a, const void *b)
{
    const GlyphLedger *l = glyph_ledger_sorting;
    const GlyphLedgerEntry *x = &l->entries[*(const size_t *)a], *y = &l->entries[*(const size_t *)b];
    int c = strcmp(l->pool + x->name, l->pool + y->name);
    if (c) return c;
    if ((x->codepoint == 0) != (y->codepoint == 0)) return x->codepoint ? -1 : 1;
    return *(const size_t *)a < *(const size_t *)b ? -1 : 1;
}

/* By the whole address: scope, set, name. */
static int glyph_ledger_address_order(const void *a, const void *b)
{
    const GlyphLedger *l = glyph_ledger_sorting;
    const GlyphLedgerEntry *x = &l->entries[*(const size_t *)a], *y = &l->entries[*(const size_t *)b];
    int c = strcmp(l->pool + x->scope, l->pool + y->scope);
    if (!c) c = strcmp(l->pool + x->set, l->pool + y->set);
    if (!c) c = strcmp(l->pool + x->name, l->pool + y->name);
    return c;
}

static size_t *glyph_ledger_select(const GlyphLedger *l, const char *scope, const char *set,
                                   size_t *count)
{
    size_t i, n = 0, *out = malloc((l->count ? l->count : 1) * sizeof(*out));
    *count = 0;
    if (!out) return NULL;
    for (i = 0; i < l->count; ++i)
        if (strcmp(l->pool + l->entries[i].scope, scope) == 0 &&
            strcmp(l->pool + l->entries[i].set, set) == 0)
            out[n++] = i;
    glyph_ledger_sorting = l;
    qsort(out, n, sizeof(*out), glyph_ledger_name_order);
    *count = n;
    return out;
}

/* NAME's allocation in the selection, and whether it was retired. */
static uint32_t glyph_ledger_lookup(const GlyphLedger *l, const size_t *selection, size_t count,
                                    const char *name, int *retired)
{
    size_t low = 0, high = count;
    uint32_t codepoint = 0;
    *retired = 0;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (strcmp(l->pool + l->entries[selection[mid]].name, name) < 0) low = mid + 1;
        else high = mid;
    }
    for (; low < count && strcmp(l->pool + l->entries[selection[low]].name, name) == 0; ++low) {
        if (l->entries[selection[low]].codepoint) codepoint = l->entries[selection[low]].codepoint;
        else *retired = 1;
    }
    return codepoint;
}

/* Has SCOPE ever had SET (an allocation in the ledger)? */
static int glyph_ledger_knows(const GlyphLedger *l, const char *scope, const char *set, size_t set_n)
{
    size_t i;
    for (i = 0; i < l->count; ++i)
        if (l->entries[i].codepoint && strcmp(l->pool + l->entries[i].scope, scope) == 0 &&
            strlen(l->pool + l->entries[i].set) == set_n &&
            memcmp(l->pool + l->entries[i].set, set, set_n) == 0)
            return 1;
    return 0;
}

/* Read the ledger: its header, then lines in the grammar above, every
   allocation above the last and each address allocated once. A last line
   without its LF is a write a crash cut short: it is cut off (no import
   was answered for it). Anything else wrong refuses the whole store. */
static int glyph_ledger_load(GlyphLedger *l, const char *path, char *why, size_t capacity)
{
    struct stat status;
    char *text = NULL;
    size_t used = 0, length, header = strlen(BLIT_GLYPH_LEDGER_HEADER);
    const char *p, *end;
    int fd;
    size_t *check = NULL, i;

    glyph_ledger_free(l);
    fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return 1;
        snprintf(why, capacity, "the ledger cannot be opened: %s", strerror(errno));
        return 0;
    }
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) || (status.st_mode & 077) != 0 ||
        (status.st_uid != 0 && status.st_uid != geteuid()) ||
        status.st_size > (off_t)BLIT_GLYPH_LEDGER_FILE_MAX ||
        (text = malloc((size_t)status.st_size + 1)) == NULL) {
        close(fd);
        snprintf(why, capacity, "the ledger is not a private regular file of at most 64 MiB");
        return 0;
    }
    while (used < (size_t)status.st_size) {
        ssize_t count = read(fd, text + used, (size_t)status.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    length = used;
    while (length > 0 && text[length - 1] != '\n') --length;
    if (length != used) {
        /* The torn last line. */
        if (ftruncate(fd, (off_t)length) < 0 || fsync(fd) < 0) {
            close(fd);
            free(text);
            snprintf(why, capacity, "the ledger's torn last line cannot be cut off");
            return 0;
        }
    }
    close(fd);
    if (length == 0) {
        free(text);
        l->size = 0;
        return 1;
    }
    if (length < header || memcmp(text, BLIT_GLYPH_LEDGER_HEADER, header) != 0) {
        free(text);
        snprintf(why, capacity, "the ledger does not start with its header");
        return 0;
    }
    p = text + header;
    end = text + length;
    while (p < end) {
        const char *lf = memchr(p, '\n', (size_t)(end - p)), *f[5];
        size_t n[5], fields = 0;
        const char *q = p;
        uint32_t codepoint = 0;
        while (q < lf && fields < 5) {
            const char *space = memchr(q, ' ', (size_t)(lf - q));
            const char *stop = space ? space : lf;
            f[fields] = q;
            n[fields++] = (size_t)(stop - q);
            q = space ? space + 1 : lf;
            if (space && q == lf) { fields = 9; break; }
        }
        if (q != lf) fields = 9;
        if (fields == 5 && n[0] == 1 && f[0][0] == 'a' && n[1] == 6) {
            char hex[7];
            memcpy(hex, f[1], 6);
            hex[6] = '\0';
            codepoint = glyph_store_code(hex);
            if (strspn(hex, "0123456789abcdef") != 6 || codepoint < l->next ||
                codepoint > BLIT_GLYPH_LEDGER_LAST || codepoint < BLIT_GLYPH_LEDGER_FIRST ||
                !glyph_scope_valid(f[2], n[2]) || !blit_glyph_installed_id_valid(f[3], n[3]) ||
                !blit_glyph_name_valid(f[4], n[4]) ||
                !glyph_ledger_add(l, codepoint, f[2], n[2], f[3], n[3], f[4], n[4]))
                break;
        } else if (fields == 4 && n[0] == 1 && f[0][0] == 'r') {
            if (!glyph_scope_valid(f[1], n[1]) || !blit_glyph_installed_id_valid(f[2], n[2]) ||
                !blit_glyph_name_valid(f[3], n[3]) ||
                !glyph_ledger_add(l, 0, f[1], n[1], f[2], n[2], f[3], n[3]))
                break;
        } else {
            break;
        }
        p = lf + 1;
    }
    free(text);
    if (p < end) {
        snprintf(why, capacity, "the ledger has a line outside its grammar, or out of order");
        glyph_ledger_free(l);
        return 0;
    }
    /* Each (scope, set, name) allocated once. */
    check = malloc((l->count ? l->count : 1) * sizeof(*check));
    if (!check) {
        snprintf(why, capacity, "out of memory reading the ledger");
        glyph_ledger_free(l);
        return 0;
    }
    {
        size_t m = 0;
        for (i = 0; i < l->count; ++i)
            if (l->entries[i].codepoint) check[m++] = i;
        glyph_ledger_sorting = l;
        qsort(check, m, sizeof(*check), glyph_ledger_address_order);
        for (i = 1; i < m; ++i)
            if (glyph_ledger_address_order(&check[i - 1], &check[i]) == 0) {
                free(check);
                snprintf(why, capacity, "the ledger allocates one address twice");
                glyph_ledger_free(l);
                return 0;
            }
    }
    free(check);
    l->size = (off_t)length;
    return 1;
}

/* fsync a directory, so a rename or a new file in it is durable. */
static int glyph_sync_directory(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC), ok;
    if (fd < 0) return 0;
    ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

/* Append TEXT to the ledger file whose durable length is *SIZE and make
   it durable; on any failure the file is cut back to *SIZE, so it holds
   whole lines only. *SIZE is the new length on success. (An import's
   worker appends with its own copy of the length, Gitea #216.) */
static int glyph_ledger_append_at(off_t *size, const char *path, const char *directory,
                                  const char *text, size_t length)
{
    size_t header = *size == 0 ? strlen(BLIT_GLYPH_LEDGER_HEADER) : 0, written = 0;
    char *all = malloc(header + length + 1);
    int fd, created = *size == 0, ok = 1;
    if (!all) return 0;
    memcpy(all, BLIT_GLYPH_LEDGER_HEADER, header);
    memcpy(all + header, text, length);
    fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0 || fchmod(fd, 0600) < 0 || lseek(fd, *size, SEEK_SET) != *size) {
        if (fd >= 0) close(fd);
        free(all);
        return 0;
    }
    while (written < header + length) {
        ssize_t count = write(fd, all + written, header + length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = 0; break; }
        written += (size_t)count;
    }
    if (ok && fsync(fd) < 0) ok = 0;
    if (!ok) {
        if (ftruncate(fd, *size) == 0) fsync(fd);
    }
    close(fd);
    free(all);
    if (ok && created && !glyph_sync_directory(directory)) ok = 0;
    if (ok) *size += (off_t)(header + length);
    return ok;
}

static int glyph_ledger_append(GlyphLedger *l, const char *path, const char *directory,
                               const char *text, size_t length)
{
    return glyph_ledger_append_at(&l->size, path, directory, text, length);
}

/* ---- Installed sets. ---- */

typedef struct {
    char id[BLIT_GLYPH_SET_ID_MAX + 1];
    char scope[16];             /* system (user-<uid> is phase 2b's) */
    int signed_set;             /* origin catalogue: signed; 0: own */
    char via[8];                /* http (frame and dir are 2c's) */
    int loaded;                 /* 1 loaded, 0 refused */
    char reason[200];
    char version[65];
    char parts[BLIT_GLYPH_PART_COUNT][65];   /* each part's SHA-256 ("" absent) */
    size_t bytes;
    long long installed;        /* when it was imported, Unix seconds */
    GlyphStoreNames names;
    uint32_t *codepoints;       /* per name: its glyph's ledger codepoint */
    FT_UInt *glyphs;            /* per name: its glyph's index in the face */
    char **retired;             /* names this scope retired in the set */
    size_t retired_count;
    unsigned char *font;        /* the bytes verified, FreeType's face's own */
    size_t font_length;
    FT_Face face;
} BlitGlyphInstalled;

typedef struct {
    uint32_t codepoint;
    FT_Face face;
    FT_UInt glyph;
} BlitGlyphRemap;

typedef struct {
    char root[PATH_MAX], keys[PATH_MAX];
    int configured;             /* opened at least once */
    int ready;                  /* the store passed its checks and is used */
    char reason[256];           /* why it is not */
    FT_Library library;
    GlyphLedger ledger;
    BlitGlyphInstalled **sets;
    int count;
    BlitGlyphRemap *remap;
    size_t remap_count;
    unsigned long generation;   /* changes whenever what installed sets draw changes */
    /* Diagnostics: how often the names reader ran and FreeType was handed a
       font (glyph_signature_gate holds a refusal to "nothing parsed" by
       them), imports answered, and the last import's time from its whole
       body to its answer. */
    unsigned long long names_parsed, fonts_opened, imports, refusals, removals;
    long last_import_us;
    /* An upload is being received or imported (the server's: from the
       PUT's headers to its answer); a second one and a removal are 409. */
    int importing;
} BlitGlyphStore;

static BlitGlyphStore blit_glyph_store;

static void glyph_installed_free(BlitGlyphInstalled *set)
{
    size_t i;
    if (!set) return;
    if (set->face) FT_Done_Face(set->face);
    free(set->font);
    free(set->codepoints);
    free(set->glyphs);
    for (i = 0; i < set->retired_count; ++i) free(set->retired[i]);
    free(set->retired);
    glyph_names_free(&set->names);
    free(set);
}

static void glyph_path(char *out, size_t capacity, const char *a, const char *b, const char *c)
{
    snprintf(out, capacity, "%s%s%s%s%s", a, b ? "/" : "", b ? b : "", c ? "/" : "", c ? c : "");
}

/* Is every directory from / down to PATH one of root's (or the server's
   own user's) that no one else can write? A sticky directory of root's
   (or of the server's own user's) is allowed above the last component:
   others may add entries there but cannot move or remove ours, and the
   next component must itself be root's or ours. PATH itself must also be 0700 when
   PRIVATE. No component may be a link, . or .. */
static int glyph_path_safe(const char *path, int private_leaf, char *why, size_t capacity)
{
    char prefix[PATH_MAX];
    size_t length = strlen(path), at = 0;
    if (length == 0 || length >= sizeof(prefix) || path[0] != '/') {
        snprintf(why, capacity, "%s is not an absolute path", path);
        return 0;
    }
    for (;;) {
        struct stat status;
        size_t next = at + 1, n;
        int leaf;
        while (next < length && path[next] != '/') ++next;
        n = next - at - 1;
        if (at == 0) {
            memcpy(prefix, "/", 2);
        }
        if (n == 0 && next < length) {
            snprintf(why, capacity, "%s has an empty component", path);
            return 0;
        }
        if ((n == 1 && path[at + 1] == '.') || (n == 2 && path[at + 1] == '.' && path[at + 2] == '.')) {
            snprintf(why, capacity, "%s has a . or .. component", path);
            return 0;
        }
        leaf = next >= length;
        if (at == 0 && lstat("/", &status) == 0 &&
            (!S_ISDIR(status.st_mode) || status.st_uid != 0 || (status.st_mode & 022))) {
            snprintf(why, capacity, "/ is not root's, or others can write it");
            return 0;
        }
        if (n > 0) {
            memcpy(prefix, path, next);
            prefix[next] = '\0';
            if (lstat(prefix, &status) < 0) {
                snprintf(why, capacity, "%s: %s", prefix, strerror(errno));
                return 0;
            }
            if (!S_ISDIR(status.st_mode)) {
                snprintf(why, capacity, "%s is not a directory (or is a link)", prefix);
                return 0;
            }
            if (status.st_uid != 0 && status.st_uid != geteuid()) {
                snprintf(why, capacity, "%s is not root's", prefix);
                return 0;
            }
            if (leaf && private_leaf ? (status.st_mode & 077) != 0 :
                (status.st_mode & 022) != 0 &&
                    !(!leaf && (status.st_mode & S_ISVTX))) {
                snprintf(why, capacity, "%s can be written by others than its owner%s", prefix,
                         leaf && private_leaf ? " (or read: it must be 0700)" : "");
                return 0;
            }
        }
        if (leaf) return 1;
        at = next;
    }
}

/* A private directory inside the store: made 0700 if absent, else it must
   be one already. */
static int glyph_private_directory(const char *path)
{
    struct stat status;
    if (mkdir(path, 0700) < 0 && errno != EEXIST) return 0;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode) &&
        status.st_uid == geteuid() && (status.st_mode & 077) == 0;
}

/* Remove a directory of the store and what is in it, one level of
   directories deep (staging's uploads and sets, a set's files). */
static void glyph_remove_tree(const char *path, int depth)
{
    DIR *listing = opendir(path);
    struct dirent *entry;
    if (listing) {
        while ((entry = readdir(listing)) != NULL) {
            struct stat status;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            if (fstatat(dirfd(listing), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISDIR(status.st_mode)) {
                char child[PATH_MAX];
                if (depth > 0) {
                    glyph_path(child, sizeof(child), path, entry->d_name, NULL);
                    glyph_remove_tree(child, depth - 1);
                }
            } else {
                unlinkat(dirfd(listing), entry->d_name, 0);
            }
        }
        closedir(listing);
    }
    rmdir(path);
}

/* A whole file of the store, at most MAX bytes, read once into memory. */
static unsigned char *glyph_read_part(const char *path, size_t max, size_t *length, int *missing)
{
    struct stat status;
    unsigned char *data;
    size_t used = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    *missing = fd < 0 && errno == ENOENT;
    if (fd < 0) return NULL;
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) || (size_t)status.st_size > max ||
        (data = malloc((size_t)status.st_size + 1)) == NULL) {
        close(fd);
        return NULL;
    }
    while (used <= (size_t)status.st_size) {
        ssize_t count = read(fd, data + used, (size_t)status.st_size + 1 - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    close(fd);
    if (used != (size_t)status.st_size) {
        free(data);
        return NULL;
    }
    *length = used;
    return data;
}

static int glyph_write_file(const char *path, const void *data, size_t length)
{
    size_t written = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    while (written < length) {
        ssize_t count = write(fd, (const char *)data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += (size_t)count;
    }
    if (written != length || fsync(fd) < 0) {
        close(fd);
        unlink(path);
        return 0;
    }
    return close(fd) == 0;
}

/* The checks of sections 6 and 7 on a set's parts, held in memory: for a
   signed set the signature over the manifest under the release keys, the
   manifest's grammar and name, each part present and its SHA-256 equal to
   the manifest's; for an own set, each part present and, when EXPECTED is
   given (at start), equal to the hashes recorded at import. Only then the
   names file is parsed and the font opened, from these bytes, with the
   limits of section 10. On success the set holds its names, its face (and
   the font's bytes) and each part's hash. LIBRARY is the FreeType the
   face is opened in, KEYS the release keys' directory; COUNTED[0] and
   COUNTED[1] count the names reader's runs and the fonts FreeType opened
   (the store's counters, or an import worker's own until the loop adds
   them, Gitea #216). Nothing else of the store is touched. */
static int glyph_set_verify(FT_Library library, const char *keys_directory, unsigned long long counted[2],
                            BlitGlyphInstalled *set,
                            unsigned char *parts[BLIT_GLYPH_PART_COUNT],
                            const size_t lengths[BLIT_GLYPH_PART_COUNT],
                            char expected[BLIT_GLYPH_PART_COUNT][65], BlitGlyphResult *r)
{
    BlitGlyphManifest manifest;
    int p, keys = 0;
    size_t i;
    FT_Face probe = NULL;
    char matched[256];

    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) {
        set->parts[p][0] = '\0';
        if (parts[p]) blit_sha256_hex(parts[p], lengths[p], set->parts[p]);
    }
#ifdef BLIT_GLYPH_GATE_AFTER_HASH
    BLIT_GLYPH_GATE_AFTER_HASH;
#endif
    if (set->signed_set) {
        if (!parts[BLIT_GLYPH_PART_MANIFEST] || !parts[BLIT_GLYPH_PART_SIGNATURE])
            return glyph_fail(r, 422, "glyph_part_missing", "a signed set carries glyphs.manifest and glyphs.sig");
        if (lengths[BLIT_GLYPH_PART_MANIFEST] > BLIT_GLYPH_MANIFEST_MAX ||
            lengths[BLIT_GLYPH_PART_SIGNATURE] != BLIT_GLYPH_SIGNATURE_BYTES)
            return glyph_fail(r, 422, "glyph_signature_invalid",
                              "the manifest is over 512 bytes, or the signature is not 64 bytes");
        if (!blit_release_verify_directory(keys_directory, 1, parts[BLIT_GLYPH_PART_SIGNATURE],
                                           (const char *)parts[BLIT_GLYPH_PART_MANIFEST],
                                           lengths[BLIT_GLYPH_PART_MANIFEST], matched,
                                           sizeof(matched), &keys))
            return glyph_fail(r, 422, "glyph_signature_invalid", keys == 0 ?
                              "no release key is installed; nothing can be verified" :
                              "the signature does not verify under any release key");
        if (!blit_glyph_manifest_parse((const char *)parts[BLIT_GLYPH_PART_MANIFEST],
                                       lengths[BLIT_GLYPH_PART_MANIFEST], &manifest))
            return glyph_fail(r, 422, "glyph_manifest_invalid",
                              "the signed bytes are not a glyph-set manifest (blit-glyph-set-signature 1)");
        if (strcmp(manifest.name, set->id + 2) != 0)
            return glyph_fail(r, 422, "glyph_manifest_invalid", "the manifest names another set");
        for (p = BLIT_GLYPH_PART_NAMES; p < BLIT_GLYPH_PART_COUNT; ++p)
            if (!parts[p])
                return glyph_fail(r, 422, "glyph_part_missing", "the set lacks its %s part",
                                  blit_glyph_part_names[p]);
        if (strcmp(set->parts[BLIT_GLYPH_PART_FONT], manifest.font) != 0 ||
            strcmp(set->parts[BLIT_GLYPH_PART_NAMES], manifest.names) != 0 ||
            strcmp(set->parts[BLIT_GLYPH_PART_LICENCE], manifest.licence) != 0)
            return glyph_fail(r, 422, "glyph_hash_mismatch", "a part's SHA-256 is not the manifest's");
    } else {
        if (parts[BLIT_GLYPH_PART_MANIFEST] || parts[BLIT_GLYPH_PART_SIGNATURE])
            return glyph_fail(r, 422, "glyph_part_missing", "a set has both a manifest and a signature, or neither");
        for (p = BLIT_GLYPH_PART_NAMES; p < BLIT_GLYPH_PART_COUNT; ++p)
            if (!parts[p])
                return glyph_fail(r, 422, "glyph_part_missing", "the set lacks its %s part",
                                  blit_glyph_part_names[p]);
        if (expected)
            for (p = BLIT_GLYPH_PART_NAMES; p < BLIT_GLYPH_PART_COUNT; ++p)
                if (strcmp(expected[p], set->parts[p]) != 0)
                    return glyph_fail(r, 422, "glyph_hash_mismatch",
                                      "the %s part is not the one imported", blit_glyph_part_names[p]);
    }
    /* Only now is anything parsed. */
    ++counted[0];
    if (!glyph_names_parse((const char *)parts[BLIT_GLYPH_PART_NAMES], lengths[BLIT_GLYPH_PART_NAMES],
                           &set->names, r))
        return 0;
    if (set->signed_set && strcmp(set->names.release, manifest.version) != 0)
        return glyph_fail(r, 422, "glyph_names_invalid", "the names file's release is not the manifest's version");
    ++counted[1];
    if (FT_New_Memory_Face(library, parts[BLIT_GLYPH_PART_FONT], (FT_Long)lengths[BLIT_GLYPH_PART_FONT],
                           -1, &probe))
        return glyph_fail(r, 422, "glyph_font_rejected", "not_a_font: FreeType does not read it as a font");
    i = (size_t)probe->num_faces;
    FT_Done_Face(probe);
    if (i != 1)
        return glyph_fail(r, 422, "glyph_font_rejected", "collection: the font holds %lu faces, not one",
                          (unsigned long)i);
    if (FT_New_Memory_Face(library, parts[BLIT_GLYPH_PART_FONT], (FT_Long)lengths[BLIT_GLYPH_PART_FONT],
                           0, &set->face)) {
        set->face = NULL;
        return glyph_fail(r, 422, "glyph_font_rejected", "not_a_font: FreeType does not read it as a font");
    }
    if (!FT_IS_SCALABLE(set->face))
        return glyph_fail(r, 422, "glyph_font_rejected", "not_scalable: the face is not an outline face");
    if (set->face->num_glyphs > BLIT_GLYPH_FACE_GLYPHS_MAX)
        return glyph_fail(r, 422, "glyph_font_rejected", "too_many_glyphs: over 65535");
    if (FT_Select_Charmap(set->face, FT_ENCODING_UNICODE))
        return glyph_fail(r, 422, "glyph_font_rejected", "no_unicode_cmap: the face has no Unicode cmap");
    set->glyphs = calloc(set->names.count ? set->names.count : 1, sizeof(*set->glyphs));
    if (!set->glyphs) return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
    for (i = 0; i < set->names.count; ++i) {
        set->glyphs[i] = FT_Get_Char_Index(set->face, set->names.names[i].code);
        if (!set->glyphs[i])
            return glyph_fail(r, 422, "glyph_font_rejected",
                              "code_unmapped: %s's code %lx is not in the font's cmap",
                              glyph_names_text(&set->names, set->names.names[i].name),
                              (unsigned long)set->names.names[i].code);
    }
    /* The face keeps the verified bytes. */
    set->font = parts[BLIT_GLYPH_PART_FONT];
    set->font_length = lengths[BLIT_GLYPH_PART_FONT];
    parts[BLIT_GLYPH_PART_FONT] = NULL;
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) set->bytes += lengths[p] * (set->parts[p][0] != 0);
    return 1;
}

/* Section 3: the set's version, the SHA-256 of its names file and its
   allocation listing in this scope (name codepoint lines, bytewise). */
static int glyph_set_version(BlitGlyphInstalled *set, const unsigned char *names, size_t length)
{
    size_t i, used = length, capacity = length + set->names.glyphs * (BLIT_GLYPH_NAME_MAX + 9) + 1;
    char *all = malloc(capacity);
    if (!all) return 0;
    memcpy(all, names, length);
    for (i = 0; i < set->names.glyphs; ++i)
        used += (size_t)snprintf(all + used, capacity - used, "%s %lx\n",
                                 glyph_names_text(&set->names, set->names.names[i].name),
                                 (unsigned long)set->codepoints[i]);
    blit_sha256_hex(all, used, set->version);
    free(all);
    return 1;
}

/* Each name's codepoint from the ledger (an alias: its glyph's). At start
   every glyph must have one; at import, NEW receives the glyph names that
   have none, which the caller allocates. Also the names this scope
   retired in the set, and a refusal of any name that comes back. */
static int glyph_set_codepoints(const GlyphLedger *l, BlitGlyphInstalled *set, int importing,
                                size_t *missing, size_t *missing_count, BlitGlyphResult *r)
{
    size_t count = 0, i, *selection = glyph_ledger_select(l, set->scope, set->id, &count);
    if (!selection) return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
    set->codepoints = calloc(set->names.count ? set->names.count : 1, sizeof(*set->codepoints));
    if (!set->codepoints) {
        free(selection);
        return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
    }
    *missing_count = 0;
    for (i = 0; i < set->names.count; ++i) {
        int retired = 0;
        const char *name = glyph_names_text(&set->names, set->names.names[i].name);
        uint32_t codepoint = glyph_ledger_lookup(l, selection, count, name, &retired);
        if (retired) {
            free(selection);
            return glyph_fail(r, importing ? 409 : 422, "glyph_retired_name",
                              "%s/%s was retired in this scope; a new glyph needs a new name", set->id, name);
        }
        if (set->names.names[i].glyph < 0) {
            set->codepoints[i] = codepoint;
            if (!codepoint) {
                if (!importing) {
                    free(selection);
                    return glyph_fail(r, 422, "glyph_ledger_mismatch", "the ledger has no codepoint for %s", name);
                }
                missing[(*missing_count)++] = i;
            }
        }
    }
    /* The retired names, for 410 retired. */
    for (i = 0; i < count; ++i) {
        const GlyphLedgerEntry *e = &l->entries[selection[i]];
        char **grown;
        if (e->codepoint) continue;
        grown = realloc(set->retired, (set->retired_count + 1) * sizeof(*grown));
        if (!grown) break;
        set->retired = grown;
        if ((set->retired[set->retired_count] = strdup(l->pool + e->name)) != NULL) ++set->retired_count;
    }
    free(selection);
    return 1;
}

static void glyph_set_alias_codepoints(BlitGlyphInstalled *set)
{
    size_t i;
    for (i = 0; i < set->names.count; ++i)
        if (set->names.names[i].glyph >= 0)
            set->codepoints[i] = set->codepoints[set->names.names[i].glyph];
}

/* record.json: a registry sets entry's schema (section 4), plus install
   and the parts' SHA-256 as imported. */
static char *glyph_record_json(const BlitGlyphInstalled *set, size_t *length)
{
    size_t capacity = 4096 + set->retired_count * (BLIT_GLYPH_NAME_MAX + 4), used = 0, i;
    char *out = malloc(capacity);
    int p;
    if (!out) return NULL;
#define RECORD_APPEND(...) used += (size_t)snprintf(out + used, capacity - used, __VA_ARGS__)
    RECORD_APPEND("{\"id\":\"%s\",\"owner\":\"x\",\"scope\":\"terminal-local\",\"faces\":[\"%s\"],"
                  "\"names\":{\"file\":\"names\",\"format\":\"blit-glyph-names\"},\"version\":\"%s\","
                  "\"retired_names\":[", set->id, set->id, set->version);
    for (i = 0; i < set->retired_count && used < capacity; ++i)
        RECORD_APPEND("%s\"%s\"", i ? "," : "", set->retired[i]);
    RECORD_APPEND("],\"install\":{\"scope\":\"%s\",\"origin\":\"%s\",\"via\":\"%s\",\"verified\":%s,"
                  "\"release\":\"%s\",\"licence\":\"%s\",\"glyphs\":%lu,\"bytes\":%lu,\"installed\":%lld},"
                  "\"parts\":{", set->scope, set->signed_set ? "catalogue" : "own", set->via,
                  set->signed_set ? "true" : "false", set->names.release, set->names.licence,
                  (unsigned long)set->names.glyphs, (unsigned long)set->bytes, set->installed);
    for (p = 0, i = 0; p < BLIT_GLYPH_PART_COUNT; ++p)
        if (set->parts[p][0]) {
            RECORD_APPEND("%s\"%s\":\"%s\"", i ? "," : "", blit_glyph_part_names[p], set->parts[p]);
            ++i;
        }
    RECORD_APPEND("}}\n");
#undef RECORD_APPEND
    if (used >= capacity) {
        free(out);
        return NULL;
    }
    *length = used;
    return out;
}

/* What start needs back from record.json: origin, via, when, and the
   parts' hashes (an own set is checked against them). */
static int glyph_record_read(const char *text, size_t length, int *signed_set, char *via, size_t via_capacity,
                             long long *installed, char parts[BLIT_GLYPH_PART_COUNT][65])
{
    GlyphJson j = {text, text + length, 0};
    char key[64], value[80];
    int first = 1, origin = -1, p;
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) parts[p][0] = '\0';
    *installed = 0;
    via[0] = '\0';
    while (gj_member(&j, &first, key, sizeof(key))) {
        if (strcmp(key, "install") == 0) {
            int inner = 1;
            while (gj_member(&j, &inner, key, sizeof(key))) {
                if (strcmp(key, "origin") == 0 && gj_string(&j, value, sizeof(value)))
                    origin = strcmp(value, "catalogue") == 0 ? 1 : strcmp(value, "own") == 0 ? 0 : -1;
                else if (strcmp(key, "via") == 0) gj_string(&j, via, via_capacity);
                else if (strcmp(key, "installed") == 0) {
                    long value_count = 0;
                    const char *start = (gj_peek(&j), j.p);
                    while (j.p < j.end && *j.p >= '0' && *j.p <= '9' && j.p - start < 18) {
                        *installed = *installed * 10 + (*j.p - '0');
                        ++j.p;
                        ++value_count;
                    }
                    if (!value_count) gj_fail(&j);
                } else gj_skip(&j, 2);
            }
        } else if (strcmp(key, "parts") == 0) {
            int inner = 1;
            while (gj_member(&j, &inner, key, sizeof(key))) {
                int found = 0;
                for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p)
                    if (strcmp(key, blit_glyph_part_names[p]) == 0) {
                        found = gj_string(&j, parts[p], 65);
                        break;
                    }
                if (!found) gj_skip(&j, 2);
            }
        } else {
            gj_skip(&j, 1);
        }
    }
    if (j.failed || origin < 0 || strcmp(via, "http") != 0) return 0;
    *signed_set = origin;
    return 1;
}

static int glyph_set_order(const void *a, const void *b)
{
    const BlitGlyphInstalled *x = *(BlitGlyphInstalled *const *)a, *y = *(BlitGlyphInstalled *const *)b;
    int c = strcmp(x->scope, y->scope);
    return c ? c : strcmp(x->id, y->id);
}

static int glyph_remap_order(const void *a, const void *b)
{
    uint32_t x = ((const BlitGlyphRemap *)a)->codepoint, y = ((const BlitGlyphRemap *)b)->codepoint;
    return x < y ? -1 : x > y;
}

/* The set table with SETS' loaded ones in it (sorted here, by scope and
   id), and the raster's remap table, built aside: nothing of the store is
   read but the sets themselves, so an import's worker builds them while
   the loop draws (Gitea #216). A set with no face yet (one the worker
   verified, whose face the loop opens) has its remap entries' face NULL
   until glyph_store_install fills them in. 0 when memory runs out. */
static int glyph_store_build(BlitGlyphInstalled **sets, int count, BlitGlyphSets **table_out,
                             BlitGlyphRemap **remap_out, size_t *remap_count_out)
{
    BlitGlyphSets *g;
    BlitGlyphRemap *remap = NULL;
    size_t remap_count = 0, total = 0, i, k;
    int s;
    qsort(sets, (size_t)count, sizeof(*sets), glyph_set_order);
    for (s = 0; s < count; ++s)
        if (sets[s]->loaded) total += sets[s]->names.glyphs;
    remap = malloc((total ? total : 1) * sizeof(*remap));
    g = blit_glyph_table_begin(count);
    if (!remap || !g) {
        free(remap);
        glyph_table_free(g);
        return 0;
    }
    for (s = 0; s < count; ++s) {
        BlitGlyphInstalled *set = sets[s];
        int index;
        if (!set->loaded) continue;
        index = blit_glyph_table_add_set(g, set->id, set->version, set->id, s);
        if (index < 0) break;
        for (i = 0; i < set->names.count; ++i) {
            const GlyphStoreName *name = &set->names.names[i];
            const char *text = glyph_names_text(&set->names, name->name);
            BlitGlyphName *record = glyph_add(g, index, text, strlen(text), set->codepoints[i],
                                              name->deprecated ? BLIT_GLYPH_FLAG_DEPRECATED : 0);
            if (!record) break;
            for (k = 0; k < name->by_count; ++k) {
                const char *by = glyph_names_text(&set->names, set->names.by[name->by + k]);
                glyph_add_by(g, record, by, strlen(by));
            }
            if (name->glyph < 0) {
                remap[remap_count].codepoint = set->codepoints[i];
                remap[remap_count].face = set->face;
                remap[remap_count].glyph = set->glyphs[i];
                ++remap_count;
            }
        }
        for (k = 0; k < set->retired_count; ++k)
            glyph_add(g, index, set->retired[k], strlen(set->retired[k]), 0, BLIT_GLYPH_FLAG_RETIRED);
    }
    qsort(remap, remap_count, sizeof(*remap), glyph_remap_order);
    blit_glyph_table_finish(g);
    *table_out = g;
    *remap_out = remap;
    *remap_count_out = remap_count;
    return 1;
}

/* On the loop: a table and remap built by glyph_store_build swapped in
   whole; the ones they replace are freed. */
static void glyph_store_install(BlitGlyphStore *st, BlitGlyphSets *table, BlitGlyphRemap *remap,
                                size_t remap_count)
{
    blit_glyph_table_swap(table);
    free(st->remap);
    st->remap = remap;
    st->remap_count = remap_count;
    ++st->generation;
}

/* The table and remap from the store's sets, built and swapped in at once
   (at start, and after a removal). */
static int blit_glyph_store_rebuild(BlitGlyphStore *st)
{
    BlitGlyphSets *g;
    BlitGlyphRemap *remap;
    size_t remap_count;
    if (!glyph_store_build(st->sets, st->count, &g, &remap, &remap_count)) return 0;
    glyph_store_install(st, g, remap, remap_count);
    return 1;
}

/* The raster's lookup (src/blit_raster.c): the face and glyph an
   installed codepoint draws, by binary search; 0 when none does. */
static int blit_glyph_store_lookup(uint32_t codepoint, void **face, unsigned *glyph)
{
    const BlitGlyphStore *st = &blit_glyph_store;
    size_t low = 0, high = st->remap_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (st->remap[mid].codepoint == codepoint) {
            *face = st->remap[mid].face;
            *glyph = st->remap[mid].glyph;
            return 1;
        }
        if (st->remap[mid].codepoint < codepoint) low = mid + 1;
        else high = mid;
    }
    return 0;
}

static BlitGlyphInstalled *blit_glyph_store_find(const char *scope, const char *id, size_t n)
{
    int s;
    for (s = 0; s < blit_glyph_store.count; ++s) {
        BlitGlyphInstalled *set = blit_glyph_store.sets[s];
        if (strcmp(set->scope, scope) == 0 && strlen(set->id) == n && memcmp(set->id, id, n) == 0)
            return set;
    }
    return NULL;
}

/* A stored set, verified again from its files (section 6): loaded, or
   refused with its reason. */
static BlitGlyphInstalled *glyph_store_load_set(BlitGlyphStore *st, const char *id)
{
    BlitGlyphInstalled *set = calloc(1, sizeof(*set));
    unsigned char *parts[BLIT_GLYPH_PART_COUNT] = {0}, *record = NULL;
    size_t lengths[BLIT_GLYPH_PART_COUNT] = {0}, record_length = 0, unused[1], none = 0;
    char directory[PATH_MAX], path[PATH_MAX], expected[BLIT_GLYPH_PART_COUNT][65];
    BlitGlyphResult r;
    int p, missing, ok = 0;
    if (!set) return NULL;
    memset(&r, 0, sizeof(r));
    snprintf(set->id, sizeof(set->id), "%s", id);
    snprintf(set->scope, sizeof(set->scope), "system");
    snprintf(set->via, sizeof(set->via), "http");
    glyph_path(directory, sizeof(directory), st->root, "system", id);
    glyph_path(path, sizeof(path), directory, "record.json", NULL);
    record = glyph_read_part(path, BLIT_GLYPH_RECORD_MAX, &record_length, &missing);
    if (!record || !glyph_record_read((const char *)record, record_length, &set->signed_set, set->via,
                                      sizeof(set->via), &set->installed, expected)) {
        glyph_fail(&r, 422, "glyph_record_invalid", "record.json is missing or not Blit's");
        goto done;
    }
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) {
        glyph_path(path, sizeof(path), directory, blit_glyph_part_names[p], NULL);
        parts[p] = glyph_read_part(path, blit_glyph_part_max[p], &lengths[p], &missing);
        if (!parts[p] && !missing) {
            glyph_fail(&r, 413, "glyph_too_large", "the %s part is unreadable or over its limit",
                       blit_glyph_part_names[p]);
            goto done;
        }
    }
    {
        unsigned long long counted[2] = {0, 0};
        int verified = glyph_set_verify(st->library, st->keys, counted, set, parts, lengths,
                                        set->signed_set ? NULL : expected, &r);
        st->names_parsed += counted[0];
        st->fonts_opened += counted[1];
        if (!verified) goto done;
    }
    if (!glyph_set_codepoints(&st->ledger, set, 0, unused, &none, &r)) goto done;
    glyph_set_alias_codepoints(set);
    if (!glyph_set_version(set, parts[BLIT_GLYPH_PART_NAMES], lengths[BLIT_GLYPH_PART_NAMES])) {
        glyph_fail(&r, 500, "glyph_store_failed", "out of memory");
        goto done;
    }
    ok = 1;
done:
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) free(parts[p]);
    free(record);
    set->loaded = ok;
    if (!ok) {
        /* Refused: kept for the listing with its reason; nothing of it is
           drawn or served. */
        if (set->face) FT_Done_Face(set->face);
        set->face = NULL;
        free(set->font);
        set->font = NULL;
        snprintf(set->reason, sizeof(set->reason), "%s: %s", r.code ? r.code : "glyph_store_failed", r.detail);
    }
    return set;
}

/* At start, for each loaded set: a name the ledger allocated in its scope
   and set that the set no longer has is retired, if the ledger does not
   say so yet (an import whose retirements a crash cut off after its
   rename). */
static void glyph_store_repair(BlitGlyphStore *st)
{
    char path[PATH_MAX];
    int s;
    glyph_path(path, sizeof(path), st->root, "ledger", NULL);
    for (s = 0; s < st->count; ++s) {
        BlitGlyphInstalled *set = st->sets[s];
        size_t count = 0, i, used = 0, capacity, *selection;
        uint32_t *order;
        char *text;
        if (!set->loaded) continue;
        selection = glyph_ledger_select(&st->ledger, set->scope, set->id, &count);
        order = malloc((set->names.count ? set->names.count : 1) * sizeof(*order));
        capacity = count * (BLIT_GLYPH_NAME_MAX + 64) + 1;
        text = malloc(capacity);
        if (!selection || !order || !text) {
            free(selection); free(order); free(text);
            continue;
        }
        for (i = 0; i < set->names.count; ++i) order[i] = (uint32_t)i;
        glyph_names_sorting = &set->names;
        qsort(order, set->names.count, sizeof(*order), glyph_names_index_order);
        for (i = 0; i < count; ++i) {
            const GlyphLedgerEntry *e = &st->ledger.entries[selection[i]];
            int retired = 0;
            if (!e->codepoint || glyph_names_find(&set->names, order, st->ledger.pool + e->name) >= 0) continue;
            glyph_ledger_lookup(&st->ledger, selection, count, st->ledger.pool + e->name, &retired);
            if (!retired)
                used += (size_t)snprintf(text + used, capacity - used, "r %s %s %s\n", set->scope, set->id,
                                         st->ledger.pool + e->name);
        }
        free(selection);
        free(order);
        if (used > 0 && glyph_ledger_append(&st->ledger, path, st->root, text, used)) {
            /* The same lines into memory, and into the set's retired names. */
            char *line = text;
            while (line < text + used) {
                char *lf = strchr(line, '\n'), *name = strrchr(line, ' ') + 1, **grown;
                *lf = '\0';
                glyph_ledger_add(&st->ledger, 0, set->scope, strlen(set->scope), set->id, strlen(set->id),
                                 name, strlen(name));
                grown = realloc(set->retired, (set->retired_count + 1) * sizeof(*grown));
                if (grown) {
                    set->retired = grown;
                    if ((set->retired[set->retired_count] = strdup(name)) != NULL) ++set->retired_count;
                }
                line = lf + 1;
            }
        }
        free(text);
    }
}

/* At start: the store's checks, the ledger, every stored set verified
   again, and the table. A store that is absent or fails a check is not
   used (the shipped sets are still served); the reason is kept for the
   diagnostics log. Returns 1 when the store is used. */
static int blit_glyph_store_open(void)
{
    BlitGlyphStore *st = &blit_glyph_store;
    const char *root = getenv("BLIT_GLYPH_STORE"), *keys = getenv("BLIT_GLYPH_KEYS_DIR");
    char path[PATH_MAX];
    DIR *listing;
    struct dirent *entry;
    struct stat status;

    st->configured = 1;
    st->ready = 0;
    snprintf(st->root, sizeof(st->root), "%s", root && root[0] ? root : BLIT_GLYPH_STORE_DEFAULT);
    snprintf(st->keys, sizeof(st->keys), "%s", keys && keys[0] ? keys : BLIT_GLYPH_KEYS_DEFAULT);
    glyph_ledger_free(&st->ledger);
    if (lstat(st->root, &status) < 0 && errno == ENOENT) {
        snprintf(st->reason, sizeof(st->reason), "%s does not exist", st->root);
        return 0;
    }
    if (!glyph_path_safe(st->root, 1, st->reason, sizeof(st->reason))) return 0;
    if (!st->library && FT_Init_FreeType(&st->library)) {
        st->library = NULL;
        snprintf(st->reason, sizeof(st->reason), "FreeType could not start");
        return 0;
    }
    glyph_path(path, sizeof(path), st->root, "system", NULL);
    if (!glyph_private_directory(path)) {
        snprintf(st->reason, sizeof(st->reason), "%s is not a private directory", path);
        return 0;
    }
    glyph_path(path, sizeof(path), st->root, "staging", NULL);
    if (!glyph_private_directory(path)) {
        snprintf(st->reason, sizeof(st->reason), "%s is not a private directory", path);
        return 0;
    }
    /* What an import or a removal left when the server stopped. */
    if ((listing = opendir(path)) != NULL) {
        while ((entry = readdir(listing)) != NULL) {
            char child[PATH_MAX];
            if (entry->d_name[0] == '.') continue;
            glyph_path(child, sizeof(child), path, entry->d_name, NULL);
            glyph_remove_tree(child, 1);
        }
        closedir(listing);
    }
    glyph_path(path, sizeof(path), st->root, "ledger", NULL);
    if (!glyph_ledger_load(&st->ledger, path, st->reason, sizeof(st->reason))) return 0;
    glyph_path(path, sizeof(path), st->root, "system", NULL);
    if ((listing = opendir(path)) != NULL) {
        while ((entry = readdir(listing)) != NULL && st->count < BLIT_GLYPH_SETS_MAX) {
            BlitGlyphInstalled *set, **grown;
            if (entry->d_name[0] == '.' ||
                !blit_glyph_installed_id_valid(entry->d_name, strlen(entry->d_name)))
                continue;
            set = glyph_store_load_set(st, entry->d_name);
            if (!set) continue;
            grown = realloc(st->sets, (size_t)(st->count + 1) * sizeof(*grown));
            if (!grown) {
                glyph_installed_free(set);
                continue;
            }
            st->sets = grown;
            st->sets[st->count++] = set;
        }
        closedir(listing);
    }
    glyph_store_repair(st);
    st->reason[0] = '\0';
    st->ready = 1;
    blit_glyph_store_rebuild(st);
    return 1;
}

static size_t glyph_scope_sets(const BlitGlyphStore *st, const char *scope, unsigned long long *bytes,
                               const char *except)
{
    size_t count = 0;
    int s;
    *bytes = 0;
    for (s = 0; s < st->count; ++s)
        if (strcmp(st->sets[s]->scope, scope) == 0 && (!except || strcmp(st->sets[s]->id, except) != 0)) {
            ++count;
            *bytes += st->sets[s]->bytes;
        }
    return count;
}

/* The upload in its staging directory: what the X-Blit-Glyph-Parts header
   said of each part (present, length and the transport's SHA-256). */
typedef struct {
    int present[BLIT_GLYPH_PART_COUNT];
    size_t lengths[BLIT_GLYPH_PART_COUNT];
    char sha256[BLIT_GLYPH_PART_COUNT][65];
} BlitGlyphUpload;

/* A directory of its own in staging for an upload's parts. */
static int blit_glyph_store_stage(char *directory, size_t capacity)
{
    BlitGlyphStore *st = &blit_glyph_store;
    char pattern[PATH_MAX];
    if (!st->ready) return 0;
    glyph_path(pattern, sizeof(pattern), st->root, "staging", "upload-XXXXXX");
    if (mkdtemp(pattern) == NULL) return 0;
    snprintf(directory, capacity, "%s", pattern);
    return 1;
}

/* ---- Section 5: an import into system, in two halves (Gitea #216). ----

   blit_glyph_import_prepare is everything that takes time, and runs on a
   worker thread while the loop draws: each staged part read back once;
   the transport's hash, the signature, the manifest and the parts' hashes
   checked on those bytes; then the names file and the font (in FreeType
   of the worker's own, opened only to check it), the ledger's codepoints
   (507 when they cannot be had) and the limits; the new set table and the
   remap built aside; the parts written from memory into a new directory,
   the ledger's allocations made durable, the directory renamed into
   place, the retirements appended -- 2a's order on disk, unchanged. It
   reads the store (its paths, the ledger, the installed sets) and changes
   nothing of it in memory, and nothing else changes those while it runs:
   one import at a time, a removal refused meanwhile (409
   glyph_import_active), the ledger written only by imports.

   blit_glyph_import_commit, on the loop, makes memory say what the files
   now say: the ledger's lines as appended (even when a later step failed),
   the face opened in the store's FreeType from the verified bytes, the
   set, the table and the remap swapped in. A refusal changes nothing.
   blit_glyph_store_import does both in turn. */

typedef struct {
    /* Set by blit_glyph_import_begin, on the loop. */
    char id[BLIT_GLYPH_SET_ID_MAX + 1];
    char upload_directory[PATH_MAX];
    BlitGlyphUpload upload;
    struct timespec started;
    off_t ledger_size;              /* the ledger file's durable length */
    /* Prepare's. */
    int ok;
    BlitGlyphResult result;
    BlitGlyphInstalled *set, *old;
    size_t *missing, missing_count; /* the names given new codepoints */
    size_t retired_before;          /* set->retired past this: this import's drops */
    int allocated_durable, retired_durable;
    BlitGlyphInstalled **sets;      /* the store's sets with this one in */
    int count;
    BlitGlyphSets *table;
    BlitGlyphRemap *remap;
    size_t remap_count;
    unsigned long long counted[2];  /* names parsed, fonts opened */
    /* Commit's. */
    size_t allocated;
    long import_us, loop_us;
} BlitGlyphImport;

static void blit_glyph_import_begin(BlitGlyphImport *job, const char *id, const char *upload_directory,
                                    const BlitGlyphUpload *upload)
{
    memset(job, 0, sizeof(*job));
    snprintf(job->id, sizeof(job->id), "%s", id);
    snprintf(job->upload_directory, sizeof(job->upload_directory), "%s", upload_directory);
    job->upload = *upload;
    job->ledger_size = blit_glyph_store.ledger.size;
    clock_gettime(CLOCK_MONOTONIC, &job->started);
}

static long glyph_elapsed_us(const struct timespec *t0)
{
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (long)(t1.tv_sec - t0->tv_sec) * 1000000L + (t1.tv_nsec - t0->tv_nsec) / 1000L;
}

static int blit_glyph_import_prepare(BlitGlyphImport *job)
{
    const BlitGlyphStore *st = &blit_glyph_store;
    const char *id = job->id;
    const BlitGlyphUpload *upload = &job->upload;
    BlitGlyphResult *r = &job->result;
    BlitGlyphInstalled *set = NULL, *old = NULL;
    unsigned char *parts[BLIT_GLYPH_PART_COUNT] = {0};
    size_t lengths[BLIT_GLYPH_PART_COUNT] = {0}, *missing = NULL, missing_count = 0, i;
    char path[PATH_MAX], built[PATH_MAX], target[PATH_MAX], system_dir[PATH_MAX], ledger_path[PATH_MAX];
    char *ledger_text = NULL, *record = NULL;
    size_t ledger_used = 0, ledger_capacity = 0, allocations_used = 0, record_length = 0;
    unsigned long long bytes = 0;
    int p, missing_part, sets_in_scope, s;
    FT_Library library = NULL;

    built[0] = '\0';
    job->ok = 0;
    if (!st->ready) return glyph_fail(r, 503, "glyph_store_unavailable", "the glyph store is not in use: %s", st->reason);
    set = calloc(1, sizeof(*set));
    if (!set) return glyph_fail(r, 500, "glyph_store_failed", "out of memory");
    job->set = set;
    snprintf(set->id, sizeof(set->id), "%s", id);
    snprintf(set->scope, sizeof(set->scope), "system");
    snprintf(set->via, sizeof(set->via), "http");
    set->signed_set = upload->present[BLIT_GLYPH_PART_MANIFEST] || upload->present[BLIT_GLYPH_PART_SIGNATURE];
    set->installed = (long long)time(NULL);
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) {
        char transport[65];
        if (!upload->present[p]) continue;
        glyph_path(path, sizeof(path), job->upload_directory, blit_glyph_part_names[p], NULL);
        parts[p] = glyph_read_part(path, blit_glyph_part_max[p], &lengths[p], &missing_part);
        if (!parts[p] || lengths[p] != upload->lengths[p]) {
            glyph_fail(r, 500, "glyph_store_failed", "the staged %s part could not be read back",
                       blit_glyph_part_names[p]);
            goto done;
        }
        blit_sha256_hex(parts[p], lengths[p], transport);
        if (strcmp(transport, upload->sha256[p]) != 0) {
            glyph_fail(r, 422, "glyph_hash_mismatch",
                       "the %s part's bytes are not the ones X-Blit-Glyph-Parts hashed (the transport)",
                       blit_glyph_part_names[p]);
            goto done;
        }
    }
    /* FreeType of this thread's own: the store's is the loop's. The face
       opened here is only checked; the loop opens the one it draws with
       from the same bytes (glyph_store_open_face). */
    if (FT_Init_FreeType(&library)) {
        library = NULL;
        glyph_fail(r, 500, "glyph_store_failed", "FreeType could not start");
        goto done;
    }
    {
        int verified = glyph_set_verify(library, st->keys, job->counted, set, parts, lengths, NULL, r);
        if (set->face) FT_Done_Face(set->face);
        set->face = NULL;
        if (!verified) goto done;
    }
    /* The ledger: retired names refused, kept names their codepoints, new
       glyph names the next ones in name order. */
    missing = malloc((set->names.glyphs ? set->names.glyphs : 1) * sizeof(*missing));
    if (!missing) {
        glyph_fail(r, 500, "glyph_store_failed", "out of memory");
        goto done;
    }
    job->missing = missing;
    if (!glyph_set_codepoints(&st->ledger, set, 1, missing, &missing_count, r)) goto done;
    old = blit_glyph_store_find("system", id, strlen(id));
    sets_in_scope = (int)glyph_scope_sets(st, "system", &bytes, id);
    if (!old && (sets_in_scope >= BLIT_GLYPH_SETS_PER_SCOPE || st->count >= BLIT_GLYPH_SETS_MAX)) {
        glyph_fail(r, 507, "glyph_store_full", "the system scope holds its 64 sets (1024 in all)");
        goto done;
    }
    if (bytes + set->bytes > BLIT_GLYPH_SYSTEM_BYTES_MAX) {
        glyph_fail(r, 507, "glyph_store_full", "the system scope's sets would be over 512 MiB");
        goto done;
    }
    if (missing_count > 0 &&
        ((unsigned long long)st->ledger.next + missing_count - 1 > BLIT_GLYPH_LEDGER_LAST)) {
        glyph_fail(r, 507, "glyph_ledger_full", "the ledger has %lu codepoints left; the set needs %lu new ones",
                   (unsigned long)(BLIT_GLYPH_LEDGER_LAST + 1 - st->ledger.next), (unsigned long)missing_count);
        goto done;
    }
    if (st->ledger.system_allocated + missing_count > BLIT_GLYPH_SYSTEM_QUOTA) {
        glyph_fail(r, 507, "glyph_quota_exhausted", "the system scope has %lu of its 24574 allocations left; "
                   "the set needs %lu", (unsigned long)(BLIT_GLYPH_SYSTEM_QUOTA - st->ledger.system_allocated),
                   (unsigned long)missing_count);
        goto done;
    }
    /* The ledger lines: the new allocations, and the names this import
       drops retired (only a name the ledger has, or an alias of the set
       as it was). */
    ledger_capacity = (missing_count + 16) * (BLIT_GLYPH_NAME_MAX + 64);
    {
        size_t count = 0, *selection = glyph_ledger_select(&st->ledger, "system", id, &count);
        uint32_t *order = malloc((set->names.count ? set->names.count : 1) * sizeof(*order));
        char **drop = NULL;
        size_t drops = 0;
        if (!selection || !order) {
            free(selection);
            free(order);
            glyph_fail(r, 500, "glyph_store_failed", "out of memory");
            goto done;
        }
        for (i = 0; i < set->names.count; ++i) order[i] = (uint32_t)i;
        glyph_names_sorting = &set->names;
        qsort(order, set->names.count, sizeof(*order), glyph_names_index_order);
        for (i = 0; i < count; ++i) {
            const GlyphLedgerEntry *e = &st->ledger.entries[selection[i]];
            if (e->codepoint && glyph_names_find(&set->names, order, st->ledger.pool + e->name) < 0) {
                int retired = 0;
                glyph_ledger_lookup(&st->ledger, selection, count, st->ledger.pool + e->name, &retired);
                if (!retired) {
                    char **grown = realloc(drop, (drops + 1) * sizeof(*grown));
                    if (grown) { drop = grown; drop[drops++] = st->ledger.pool + e->name; }
                }
            }
        }
        if (old && old->loaded)
            for (i = 0; i < old->names.count; ++i) {
                const char *name = glyph_names_text(&old->names, old->names.names[i].name);
                int retired = 0;
                if (old->names.names[i].glyph < 0 || glyph_names_find(&set->names, order, name) >= 0) continue;
                if (glyph_ledger_lookup(&st->ledger, selection, count, name, &retired) || retired) continue;
                {
                    char **grown = realloc(drop, (drops + 1) * sizeof(*grown));
                    if (grown) { drop = grown; drop[drops++] = (char *)name; }
                }
            }
        ledger_capacity += drops * (BLIT_GLYPH_NAME_MAX + 64);
        ledger_text = malloc(ledger_capacity);
        if (ledger_text) {
            for (i = 0; i < missing_count; ++i) {
                uint32_t codepoint = st->ledger.next + (uint32_t)i;
                set->codepoints[missing[i]] = codepoint;
                ledger_used += (size_t)snprintf(ledger_text + ledger_used, ledger_capacity - ledger_used,
                                                "a %06lx system %s %s\n", (unsigned long)codepoint, id,
                                                glyph_names_text(&set->names, set->names.names[missing[i]].name));
            }
            allocations_used = ledger_used;
            job->retired_before = set->retired_count;
            for (i = 0; i < drops; ++i) {
                char **grown;
                ledger_used += (size_t)snprintf(ledger_text + ledger_used, ledger_capacity - ledger_used,
                                                "r system %s %s\n", id, drop[i]);
                grown = realloc(set->retired, (set->retired_count + 1) * sizeof(*grown));
                if (grown) {
                    set->retired = grown;
                    if ((set->retired[set->retired_count] = strdup(drop[i])) != NULL) ++set->retired_count;
                }
            }
        }
        free(drop);
        free(selection);
        free(order);
        if (!ledger_text) {
            glyph_fail(r, 500, "glyph_store_failed", "out of memory");
            goto done;
        }
    }
    job->missing_count = missing_count;
    glyph_set_alias_codepoints(set);
    if (!glyph_set_version(set, parts[BLIT_GLYPH_PART_NAMES], lengths[BLIT_GLYPH_PART_NAMES])) {
        glyph_fail(r, 500, "glyph_store_failed", "out of memory");
        goto done;
    }
    /* The set table and the remap with this set in place of the one it
       updates, built aside; nothing so far changed anything. */
    job->sets = malloc((size_t)(st->count + 1) * sizeof(*job->sets));
    if (!job->sets) {
        glyph_fail(r, 500, "glyph_store_failed", "out of memory");
        goto done;
    }
    for (s = 0; s < st->count; ++s) job->sets[job->count++] = st->sets[s] == old ? set : st->sets[s];
    if (!old) job->sets[job->count++] = set;
    set->loaded = 1;
    if (!glyph_store_build(job->sets, job->count, &job->table, &job->remap, &job->remap_count)) {
        glyph_fail(r, 500, "glyph_store_failed", "out of memory building the set table");
        goto done;
    }
    /* The verified bytes, written from memory into a set directory of
       their own; the font's are the ones the face will hold. */
    glyph_path(built, sizeof(built), st->root, "staging", "set-XXXXXX");
    if (mkdtemp(built) == NULL) {
        built[0] = '\0';
        glyph_fail(r, 500, "glyph_store_failed", "a staging directory could not be made");
        goto done;
    }
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) {
        const unsigned char *data = p == BLIT_GLYPH_PART_FONT ? set->font : parts[p];
        if (!set->parts[p][0]) continue;
        glyph_path(path, sizeof(path), built, blit_glyph_part_names[p], NULL);
        if (!glyph_write_file(path, data, lengths[p])) {
            glyph_fail(r, 507, "glyph_store_full", "the %s part could not be written", blit_glyph_part_names[p]);
            goto done;
        }
    }
    record = glyph_record_json(set, &record_length);
    glyph_path(path, sizeof(path), built, "record.json", NULL);
    if (!record || !glyph_write_file(path, record, record_length) || !glyph_sync_directory(built)) {
        glyph_fail(r, 507, "glyph_store_full", "record.json could not be written");
        goto done;
    }
    /* Durable before visible: the allocations first, then the rename. The
       retirements follow the rename, so a set left in place by a rename
       that did not happen never holds a name the ledger calls retired;
       if they are lost, the next start writes them (glyph_store_repair).
       The loop adds to the ledger in memory what was appended here
       (blit_glyph_import_commit), whatever happens after. */
    glyph_path(ledger_path, sizeof(ledger_path), st->root, "ledger", NULL);
    if (allocations_used > 0) {
        if (!glyph_ledger_append_at(&job->ledger_size, ledger_path, st->root, ledger_text, allocations_used)) {
            glyph_fail(r, 507, "glyph_store_full", "the ledger could not be written");
            goto done;
        }
        job->allocated_durable = 1;
    }
    glyph_path(system_dir, sizeof(system_dir), st->root, "system", NULL);
    glyph_path(target, sizeof(target), system_dir, id, NULL);
    if (old) {
        int exchanged = syscall(SYS_renameat2, AT_FDCWD, built, AT_FDCWD, target, RENAME_EXCHANGE) == 0;
        if (!exchanged) {
            /* No RENAME_EXCHANGE here: the old one aside, then the new. */
            char aside[PATH_MAX];
            glyph_path(aside, sizeof(aside), st->root, "staging", "old-XXXXXX");
            if (mkdtemp(aside) == NULL || rmdir(aside) < 0 || rename(target, aside) < 0 ||
                rename(built, target) < 0) {
                glyph_fail(r, 500, "glyph_store_failed", "the set could not be put in place");
                goto done;
            }
            snprintf(built, sizeof(built), "%s", aside);
        }
    } else if (rename(built, target) < 0) {
        glyph_fail(r, 500, "glyph_store_failed", "the set could not be put in place");
        goto done;
    } else {
        built[0] = '\0';
    }
    glyph_sync_directory(system_dir);
    if (ledger_used > allocations_used &&
        glyph_ledger_append_at(&job->ledger_size, ledger_path, st->root, ledger_text + allocations_used,
                               ledger_used - allocations_used))
        job->retired_durable = 1;
    job->old = old;
    job->ok = 1;
done:
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) free(parts[p]);
    free(ledger_text);
    free(record);
    /* An update's old directory (exchanged out), or the new one not put
       in place. */
    if (built[0]) glyph_remove_tree(built, 0);
    if (library) FT_Done_FreeType(library);
    return job->ok;
}

/* On the loop: the face the raster draws with, in the store's FreeType,
   from the bytes the worker verified (FreeType reads the same tables
   again; it parses no glyph). */
static int glyph_store_open_face(BlitGlyphStore *st, BlitGlyphInstalled *set)
{
    if (FT_New_Memory_Face(st->library, set->font, (FT_Long)set->font_length, 0, &set->face)) {
        set->face = NULL;
        return 0;
    }
    if (FT_Select_Charmap(set->face, FT_ENCODING_UNICODE)) {
        FT_Done_Face(set->face);
        set->face = NULL;
        return 0;
    }
    return 1;
}

/* On the loop, once prepare is done: memory made to say what the files
   say, and the set swapped in (or, refused, everything of it freed).
   Returns 1 when the set is in use. */
static int blit_glyph_import_commit(BlitGlyphImport *job)
{
    BlitGlyphStore *st = &blit_glyph_store;
    BlitGlyphInstalled *set = job->set;
    BlitGlyphResult *r = &job->result;
    struct timespec t0;
    size_t i;
    int ok = job->ok;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    st->names_parsed += job->counted[0];
    st->fonts_opened += job->counted[1];
    st->ledger.size = job->ledger_size;
    if (job->allocated_durable)
        for (i = 0; i < job->missing_count; ++i) {
            const char *name = glyph_names_text(&set->names, set->names.names[job->missing[i]].name);
            glyph_ledger_add(&st->ledger, set->codepoints[job->missing[i]], "system", 6, job->id,
                             strlen(job->id), name, strlen(name));
        }
    /* The drops alone: the worker kept only names the ledger had not
       retired, and the set's earlier retirements are in it already, so no
       search of the ledger on the loop. */
    if (job->retired_durable)
        for (i = job->retired_before; i < set->retired_count; ++i)
            glyph_ledger_add(&st->ledger, 0, "system", 6, job->id, strlen(job->id), set->retired[i],
                             strlen(set->retired[i]));
    if (ok && !glyph_store_open_face(st, set)) {
        /* Installed on disk; it is served from the next start. */
        glyph_fail(r, 500, "glyph_store_failed", "the set is stored but its face could not be opened; "
                   "it is served from the next start");
        ok = 0;
    }
    if (ok) {
        for (i = 0; i < job->remap_count; ++i)
            if (!job->remap[i].face) job->remap[i].face = set->face;
        free(st->sets);
        st->sets = job->sets;
        st->count = job->count;
        glyph_store_install(st, job->table, job->remap, job->remap_count);
        if (job->old) glyph_installed_free(job->old);
        job->allocated = job->missing_count;
        ++st->imports;
    } else {
        glyph_table_free(job->table);
        free(job->remap);
        free(job->sets);
        glyph_installed_free(set);
        ++st->refusals;
    }
    job->set = job->old = NULL;
    job->sets = NULL;
    job->table = NULL;
    job->remap = NULL;
    free(job->missing);
    job->missing = NULL;
    job->loop_us = glyph_elapsed_us(&t0);
    job->import_us = glyph_elapsed_us(&job->started);
    st->last_import_us = job->import_us;
    return ok;
}

/* Both halves in turn, where waiting is fine (glyph_signature_gate, and
   the server when no thread can be had). */
static int blit_glyph_store_import(const char *id, const char *upload_directory,
                                   const BlitGlyphUpload *upload, BlitGlyphResult *r,
                                   size_t *allocated_out)
{
    BlitGlyphImport job;
    int ok;
    blit_glyph_import_begin(&job, id, upload_directory, upload);
    blit_glyph_import_prepare(&job);
    ok = blit_glyph_import_commit(&job);
    *r = job.result;
    *allocated_out = job.allocated;
    return ok;
}

/* Uninstall (section 9): the scope's copy goes; the ledger keeps every
   allocation. Refused while an import is under way, from its first byte
   to its answer: the store takes one change at a time, and the import's
   worker reads the installed sets (Gitea #216). */
static int blit_glyph_store_remove(const char *id, BlitGlyphResult *r)
{
    BlitGlyphStore *st = &blit_glyph_store;
    BlitGlyphInstalled *set;
    char target[PATH_MAX], aside[PATH_MAX], system_dir[PATH_MAX];
    int s;
    memset(r, 0, sizeof(*r));
    if (!st->ready) return glyph_fail(r, 503, "glyph_store_unavailable", "the glyph store is not in use: %s", st->reason);
    if (st->importing)
        return glyph_fail(r, 409, "glyph_import_active",
                          "a glyph set is being imported; the store takes one change at a time; "
                          "remove %s once the import has answered", id);
    set = blit_glyph_store_find("system", id, strlen(id));
    if (!set) return glyph_fail(r, 404, "glyph_set_not_installed", "%s is not installed in the system scope", id);
    glyph_path(system_dir, sizeof(system_dir), st->root, "system", NULL);
    glyph_path(target, sizeof(target), system_dir, id, NULL);
    glyph_path(aside, sizeof(aside), st->root, "staging", "removed-XXXXXX");
    if (mkdtemp(aside) == NULL || rmdir(aside) < 0 || rename(target, aside) < 0)
        return glyph_fail(r, 500, "glyph_store_failed", "the set could not be removed");
    glyph_sync_directory(system_dir);
    glyph_remove_tree(aside, 0);
    for (s = 0; s < st->count; ++s)
        if (st->sets[s] == set) {
            st->sets[s] = st->sets[--st->count];
            break;
        }
    blit_glyph_store_rebuild(st);
    glyph_installed_free(set);
    ++st->removals;
    return 1;
}

/* Was SET installed in SCOPE and is no longer (410 uninstalled)? */
static int blit_glyph_store_uninstalled(const char *scope, const char *id, size_t n)
{
    const BlitGlyphStore *st = &blit_glyph_store;
    return st->ready && blit_glyph_installed_id_valid(id, n) && !blit_glyph_store_find(scope, id, n) &&
        glyph_ledger_knows(&st->ledger, scope, id, n);
}

#endif
