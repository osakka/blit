/* ADR 0233: Arabizi, a keyboard of our own -- the engine.

   Latin Arabizi typed key by key becomes Arabic: a word's units (digits,
   digraphs, long vowels, endings, letters) each have Arabic options by
   position from one rules file, the first the default; a word's readings
   are tried against a word-frequency list and the likeliest kept, each
   non-default option costing a little. An article, a conjunction or a
   phrase's first words are held across a space and joined. A word much
   likelier as English than as any Arabic reading stays as typed.

   One rules file (arabizi.rules) and two word lists (ar-words.txt,
   en-words.txt) in BLIT_ARABIZI_ROOT, by default /usr/share/blit/arabizi,
   read once on first use. The engine never deletes what it committed: it
   keeps a preedit (the Latin held so far) and hands out commits.

   tools/tests/arabizi/keys.py is the design prototype this follows; the
   corpus gate (arabizi_gate) holds the accuracy. */

#ifndef BLIT_ARABIZI_H
#define BLIT_ARABIZI_H

#include <math.h>
#include <ctype.h>

#define AZ_OPTS 8
#define AZ_OPT_BYTES 24
#define AZ_LATIN 8
#define AZ_WORD 48           /* a word's Latin letters, at most */
#define AZ_HELD 8            /* words held in one preedit, at most */
#define AZ_PHRASE 96
#define AZ_OUT 1024          /* one commit's bytes, at most */
#define AZ_CANDIDATES 20000  /* readings tried per word before defaults only */
#define AZ_PENALTY 0.3       /* each option index's cost */
#define AZ_ENGLISH_RATIO 3.0

enum { AZ_ANY, AZ_INITIAL, AZ_MEDIAL, AZ_FINAL };
enum { AZ_KIND_NONE, AZ_KIND_UNIT, AZ_KIND_LONG, AZ_KIND_ENDING };

typedef struct {
    char plain[AZ_OPT_BYTES];
    char marked[AZ_OPT_BYTES];
} AzOption;

typedef struct {
    char latin[AZ_LATIN];
    int kind, position, count;
    AzOption options[AZ_OPTS];
} AzUnit;

typedef struct {
    char latin[AZ_PHRASE];      /* lower case, words joined by one space */
    char arabic[AZ_PHRASE];
} AzPhrase;

typedef struct {
    uint64_t key;               /* 0: empty */
    uint32_t value;
} AzSlot;

typedef struct {
    AzSlot *slots;
    size_t mask;
    double total;
} AzTable;

static struct {
    int loaded;                 /* 0 not tried, 1 ready, -1 failed */
    char problem[256];
    AzUnit *units;
    int unit_count;
    char latins[64][AZ_LATIN];  /* distinct unit Latin, longest first */
    int latin_count;
    AzPhrase *phrases, *words;
    int phrase_count, word_count;
    char starts[256][AZ_PHRASE];  /* every phrase's proper word prefixes */
    int start_count;
    char article[16][AZ_LATIN], conj[16][AZ_LATIN];
    int article_count, conj_count;
    char punct[16][2][8];
    int punct_count;
    AzTable arabic, prefixes, english;
} az;

static uint64_t az_hash(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h ? h : 1;
}

static int az_table_init(AzTable *t, size_t entries)
{
    size_t size = 1024;
    while (size < entries * 2) size <<= 1;
    t->slots = calloc(size, sizeof(*t->slots));
    t->mask = size - 1;
    t->total = 0;
    return t->slots != NULL;
}

static void az_table_put(AzTable *t, uint64_t key, uint32_t value)
{
    size_t i = key & t->mask;
    while (t->slots[i].key != 0 && t->slots[i].key != key) i = (i + 1) & t->mask;
    t->slots[i].key = key;
    t->slots[i].value = value;
}

static uint32_t az_table_get(const AzTable *t, const char *s, size_t n)
{
    uint64_t key = az_hash(s, n);
    size_t i;
    if (t->slots == NULL) return 0;
    i = key & t->mask;
    while (t->slots[i].key != 0) {
        if (t->slots[i].key == key) return t->slots[i].value;
        i = (i + 1) & t->mask;
    }
    return 0;
}

/* "-" silent; "-:<mark>" silent, writing <mark> with tashkeel;
   "<text>:<mark>" <text>, writing <text><mark>. */
static void az_option(AzOption *o, const char *spec)
{
    const char *colon = strchr(spec, ':');
    memset(o, 0, sizeof(*o));
    if (spec[0] == '-') {
        if (spec[1] == ':') snprintf(o->marked, sizeof(o->marked), "%s", spec + 2);
        return;
    }
    if (colon == NULL) {
        snprintf(o->plain, sizeof(o->plain), "%s", spec);
        snprintf(o->marked, sizeof(o->marked), "%s", spec);
    } else {
        snprintf(o->plain, sizeof(o->plain), "%.*s", (int)(colon - spec), spec);
        snprintf(o->marked, sizeof(o->marked), "%.*s%s", (int)(colon - spec), spec, colon + 1);
    }
}

static int az_position(const char *word)
{
    if (strcmp(word, "initial") == 0) return AZ_INITIAL;
    if (strcmp(word, "medial") == 0) return AZ_MEDIAL;
    if (strcmp(word, "final") == 0) return AZ_FINAL;
    return strcmp(word, "any") == 0 ? AZ_ANY : -1;
}

static void az_lower(char *s)
{
    for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

/* One line of the rules file; 0 for a line it cannot read. */
static int az_rule(char *line)
{
    char kind[16], a[AZ_PHRASE], b[AZ_PHRASE], c[256];
    char *eq;
    if (sscanf(line, "%15s", kind) != 1) return 1;
    if (strcmp(kind, "unit") == 0 || strcmp(kind, "long") == 0 || strcmp(kind, "ending") == 0) {
        AzUnit *u;
        char *opt, *save = NULL;
        int i;
        if (sscanf(line, "%*s %7s %15s %255s", a, b, c) != 3 || az_position(b) < 0) return 0;
        az.units = realloc(az.units, (size_t)(az.unit_count + 1) * sizeof(*az.units));
        if (az.units == NULL) return 0;
        u = &az.units[az.unit_count++];
        memset(u, 0, sizeof(*u));
        snprintf(u->latin, sizeof(u->latin), "%s", a);
        u->kind = kind[0] == 'u' ? AZ_KIND_UNIT : kind[0] == 'l' ? AZ_KIND_LONG : AZ_KIND_ENDING;
        u->position = az_position(b);
        for (opt = strtok_r(c, ",", &save); opt && u->count < AZ_OPTS; opt = strtok_r(NULL, ",", &save))
            az_option(&u->options[u->count++], opt);
        for (i = 0; i < az.latin_count; ++i)
            if (strcmp(az.latins[i], a) == 0) return 1;
        if (az.latin_count < 64) snprintf(az.latins[az.latin_count++], AZ_LATIN, "%s", a);
        return 1;
    }
    if (strcmp(kind, "phrase") == 0 || strcmp(kind, "word") == 0) {
        AzPhrase **list = kind[0] == 'p' ? &az.phrases : &az.words;
        int *count = kind[0] == 'p' ? &az.phrase_count : &az.word_count;
        char *from = line + strlen(kind), *end;
        eq = strchr(line, '=');
        if (eq == NULL) return 0;
        while (*from == ' ' || *from == '\t') ++from;
        end = eq;
        while (end > from && (end[-1] == ' ' || end[-1] == '\t')) --end;
        snprintf(a, sizeof(a), "%.*s", (int)(end - from), from);
        from = eq + 1;
        while (*from == ' ' || *from == '\t') ++from;
        snprintf(b, sizeof(b), "%s", from);
        az_lower(a);
        *list = realloc(*list, (size_t)(*count + 1) * sizeof(**list));
        if (*list == NULL) return 0;
        snprintf((*list)[*count].latin, AZ_PHRASE, "%s", a);
        snprintf((*list)[*count].arabic, AZ_PHRASE, "%s", b);
        ++*count;
        if (kind[0] == 'p') {
            /* Its proper prefixes, by whole words: what a held word may begin. */
            char *space = a;
            while ((space = strchr(space, ' ')) != NULL) {
                if (az.start_count < 256)
                    snprintf(az.starts[az.start_count++], AZ_PHRASE, "%.*s", (int)(space - a), a);
                ++space;
            }
        }
        return 1;
    }
    if (strcmp(kind, "article") == 0 || strcmp(kind, "conj") == 0) {
        char *w, *save = NULL;
        int is_article = kind[0] == 'a';
        for (w = strtok_r(line + strlen(kind), " \t", &save); w; w = strtok_r(NULL, " \t", &save)) {
            if (is_article && az.article_count < 16) snprintf(az.article[az.article_count++], AZ_LATIN, "%s", w);
            if (!is_article && az.conj_count < 16) snprintf(az.conj[az.conj_count++], AZ_LATIN, "%s", w);
        }
        return 1;
    }
    if (strcmp(kind, "punct") == 0) {
        if (sscanf(line, "%*s %7s %7s", a, b) != 2 || az.punct_count >= 16) return 0;
        snprintf(az.punct[az.punct_count][0], 8, "%s", a);
        snprintf(az.punct[az.punct_count][1], 8, "%s", b);
        ++az.punct_count;
        return 1;
    }
    return 0;
}

static int az_by_length(const void *a, const void *b)
{
    return (int)strlen((const char *)b) - (int)strlen((const char *)a);
}

/* A word list, "<word> <count>" a line, into `t`; Arabic ones also give
   every prefix at a letter boundary to `prefixes`. */
static int az_words(const char *path, AzTable *t, AzTable *prefixes, size_t entries)
{
    FILE *f = fopen(path, "r");
    char line[256], word[200];
    unsigned long count;
    if (f == NULL) return 0;
    if (!az_table_init(t, entries) || (prefixes && !az_table_init(prefixes, entries * 6))) {
        fclose(f);
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        size_t n, i;
        if (sscanf(line, "%199s %lu", word, &count) != 2) continue;
        n = strlen(word);
        az_table_put(t, az_hash(word, n), (uint32_t)(count > 0xffffffffUL ? 0xffffffffUL : count));
        t->total += (double)count;
        if (prefixes)
            for (i = 1; i <= n; ++i)
                if (i == n || ((unsigned char)word[i] & 0xc0) != 0x80)
                    az_table_put(prefixes, az_hash(word, i), 1);
    }
    fclose(f);
    return 1;
}

static int az_load(void)
{
    const char *root = getenv("BLIT_ARABIZI_ROOT");
    char path[1024], line[512];
    FILE *f;
    int number = 0;
    if (az.loaded) return az.loaded > 0;
    az.loaded = -1;
    if (root == NULL || root[0] == '\0') root = "/usr/share/blit/arabizi";
    snprintf(path, sizeof(path), "%s/arabizi.rules", root);
    f = fopen(path, "r");
    if (f == NULL) {
        snprintf(az.problem, sizeof(az.problem), "no Arabizi rules at %s", path);
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        ++number;
        s[strcspn(s, "\r\n")] = '\0';
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\0' || *s == '#') continue;
        if (!az_rule(s)) {
            snprintf(az.problem, sizeof(az.problem), "%s line %d is not a rule", path, number);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    qsort(az.latins, (size_t)az.latin_count, AZ_LATIN, az_by_length);
    snprintf(path, sizeof(path), "%s/ar-words.txt", root);
    if (!az_words(path, &az.arabic, &az.prefixes, 100000)) {
        snprintf(az.problem, sizeof(az.problem), "no Arabic word list at %s", path);
        return 0;
    }
    snprintf(path, sizeof(path), "%s/en-words.txt", root);
    if (!az_words(path, &az.english, NULL, 50000)) {
        snprintf(az.problem, sizeof(az.problem), "no English word list at %s", path);
        return 0;
    }
    az.loaded = 1;
    return 1;
}

/* ---- a word ---- */

static const AzUnit *az_unit(const char *latin, int position)
{
    const AzUnit *any = NULL;
    int i;
    for (i = 0; i < az.unit_count; ++i) {
        if (strcmp(az.units[i].latin, latin) != 0) continue;
        if (az.units[i].position == position) return &az.units[i];
        if (az.units[i].position == AZ_ANY) any = &az.units[i];
    }
    return any;
}

static int az_kind(const char *latin)
{
    int i;
    for (i = 0; i < az.unit_count; ++i)
        if (strcmp(az.units[i].latin, latin) == 0) return az.units[i].kind;
    return AZ_KIND_NONE;
}

typedef struct {
    int count;
    int option_count[AZ_WORD];
    AzOption options[AZ_WORD][AZ_OPTS];
} AzWord;

/* `latin` (lower case) into units by longest match, each with its options
   at its position. 0 when it is longer than a word can be. */
static int az_word_options(const char *latin, AzWord *w)
{
    char units[AZ_WORD][AZ_LATIN];
    size_t n = strlen(latin), i = 0;
    int k, count = 0;
    while (i < n) {
        int matched = 0, j;
        if (count == AZ_WORD) return 0;
        for (j = 0; j < az.latin_count; ++j) {
            size_t len = strlen(az.latins[j]);
            if (strncmp(latin + i, az.latins[j], len) != 0) continue;
            if (az_kind(az.latins[j]) == AZ_KIND_ENDING && i + len != n) continue;
            snprintf(units[count++], AZ_LATIN, "%s", az.latins[j]);
            i += len;
            matched = 1;
            break;
        }
        if (!matched) {
            snprintf(units[count++], AZ_LATIN, "%c", latin[i]);
            ++i;
        }
    }
    memset(w, 0, sizeof(*w));
    w->count = count;
    for (k = 0; k < count; ++k) {
        int position = k == 0 ? AZ_INITIAL : k == count - 1 ? AZ_FINAL : AZ_MEDIAL;
        const AzUnit *u;
        if (k > 0 && strcmp(units[k], units[k - 1]) == 0 && az_kind(units[k]) == AZ_KIND_UNIT &&
            strchr("aeiou", units[k][0]) == NULL) {
            /* A doubled consonant: written once with shadda, or twice (الله). */
            const AzUnit *one = az_unit(units[k], AZ_MEDIAL);
            w->option_count[k] = 2;
            snprintf(w->options[k][0].marked, AZ_OPT_BYTES, "\xd9\x91");
            if (one) w->options[k][1] = one->options[0];
            continue;
        }
        u = az_kind(units[k]) ? az_unit(units[k], position) : NULL;
        if (u != NULL && u->count > 0) {
            w->option_count[k] = u->count;
            memcpy(w->options[k], u->options, sizeof(AzOption) * (size_t)u->count);
        } else {
            w->option_count[k] = 1;
            snprintf(w->options[k][0].plain, AZ_OPT_BYTES, "%s", units[k]);
            snprintf(w->options[k][0].marked, AZ_OPT_BYTES, "%s", units[k]);
        }
    }
    return 1;
}

typedef struct {
    const AzWord *w;
    int capped;
    char plain[AZ_WORD * AZ_OPT_BYTES], marked[AZ_WORD * AZ_OPT_BYTES];
    double best_score;
    uint32_t best_count;
    char best_plain[AZ_WORD * AZ_OPT_BYTES], best_marked[AZ_WORD * AZ_OPT_BYTES];
} AzSearch;

/* Every reading in option order, pruned where the Arabic so far begins no
   known word; the likeliest known word kept (the first of equals). */
static void az_search(AzSearch *s, int k, size_t pn, size_t mn, double cost)
{
    int i, count;
    if (k == s->w->count) {
        uint32_t f = az_table_get(&az.arabic, s->plain, pn);
        double score;
        if (f == 0) return;
        score = log((double)f) - cost;
        if (score > s->best_score) {
            s->best_score = score;
            s->best_count = f;
            memcpy(s->best_plain, s->plain, pn + 1);
            memcpy(s->best_marked, s->marked, mn + 1);
        }
        return;
    }
    count = s->capped ? 1 : s->w->option_count[k];
    for (i = 0; i < count; ++i) {
        const AzOption *o = &s->w->options[k][i];
        size_t pl = strlen(o->plain), ml = strlen(o->marked);
        memcpy(s->plain + pn, o->plain, pl + 1);
        memcpy(s->marked + mn, o->marked, ml + 1);
        if (pl && !az_table_get(&az.prefixes, s->plain, pn + pl)) continue;
        az_search(s, k + 1, pn + pl, mn + ml, cost + AZ_PENALTY * i);
    }
}

/* A word's reading: the likeliest known word among its readings, or the
   defaults when none is known. Returns its count in the word list. */
static uint32_t az_arabic(const char *latin, char *out, size_t capacity, int tashkeel)
{
    static AzWord w;
    static AzSearch s;
    char lower[AZ_WORD + 1];
    double product = 1;
    int k;
    snprintf(lower, sizeof(lower), "%s", latin);
    az_lower(lower);
    if (strlen(latin) > AZ_WORD || !az_word_options(lower, &w)) {
        snprintf(out, capacity, "%s", latin);
        return 0;
    }
    memset(&s, 0, sizeof(s));
    s.w = &w;
    s.best_score = -1e300;
    for (k = 0; k < w.count; ++k) product *= w.option_count[k];
    s.capped = product > AZ_CANDIDATES;
    az_search(&s, 0, 0, 0, 0);
    if (s.best_count) {
        snprintf(out, capacity, "%s", tashkeel ? s.best_marked : s.best_plain);
        return s.best_count;
    }
    out[0] = '\0';
    for (k = 0; k < w.count; ++k)
        strncat(out, tashkeel ? w.options[k][0].marked : w.options[k][0].plain,
                capacity - strlen(out) - 1);
    return 0;
}

static int az_in(char list[][AZ_LATIN], int n, const char *word)
{
    int i;
    for (i = 0; i < n; ++i)
        if (strcmp(list[i], word) == 0) return 1;
    return 0;
}

/* Kept as typed: a word of letters only, much likelier as English than as
   any Arabic reading. */
static int az_english(const char *latin)
{
    char lower[AZ_WORD + 1], arabic[AZ_OUT];
    uint32_t en, ar;
    size_t i;
    if (strlen(latin) > AZ_WORD) return 0;
    snprintf(lower, sizeof(lower), "%s", latin);
    az_lower(lower);
    for (i = 0; lower[i]; ++i)
        if (!isalpha((unsigned char)lower[i])) return 0;
    en = az_table_get(&az.english, lower, strlen(lower));
    if (!en) return 0;
    ar = az_arabic(lower, arabic, sizeof(arabic), 0);
    return (double)en / az.english.total > AZ_ENGLISH_RATIO * ((double)ar / az.arabic.total);
}

/* A vowel then a sun letter, as the article is written before one
   (esh shams, en-nas); `out` the letter. The digit forms (a9, e6') count
   only where a next word confirms them. */
static int az_sun_article(const char *word, char *letter, size_t capacity, int digits)
{
    static const char *const sun[] = { "sh", "th", "dh", "t", "d", "r", "z", "s", "l", "n", "9'", "6'", "9", "6" };
    size_t i, n = digits ? sizeof(sun) / sizeof(sun[0]) : 10;
    if (word[0] != 'a' && word[0] != 'e' && word[0] != 'i') return 0;
    for (i = 0; i < n; ++i)
        if (strcmp(word + 1, sun[i]) == 0) {
            snprintf(letter, capacity, "%s", sun[i]);
            return 1;
        }
    return 0;
}

static const char *az_phrase(AzPhrase *list, int n, const char *latin)
{
    int i;
    for (i = 0; i < n; ++i)
        if (strcmp(list[i].latin, latin) == 0) return list[i].arabic;
    return NULL;
}

/* One word, as typed: digits stay; the user's and the rules' words; a
   hyphenated article; English kept; else its reading. */
static void az_word_reading(const char *latin, int tashkeel, char *out, size_t capacity)
{
    char lower[AZ_WORD + 1], part[AZ_WORD + 1], letter[4], arabic[AZ_OUT];
    const char *found, *p = latin, *dash;
    size_t i;
    int digits = latin[0] != '\0';
    out[0] = '\0';
    for (i = 0; latin[i]; ++i) if (!isdigit((unsigned char)latin[i])) digits = 0;
    if (digits || strlen(latin) > AZ_WORD) {
        snprintf(out, capacity, "%s", latin);
        return;
    }
    snprintf(lower, sizeof(lower), "%s", latin);
    az_lower(lower);
    if ((found = az_phrase(az.words, az.word_count, lower)) != NULL) {
        snprintf(out, capacity, "%s", found);
        return;
    }
    dash = strchr(lower, '-');
    if (dash != NULL) {
        snprintf(part, sizeof(part), "%.*s", (int)(dash - lower), lower);
        if (az_in(az.article, az.article_count, part) || az_sun_article(part, letter, sizeof(letter), 0)) {
            snprintf(out, capacity, "\xd8\xa7\xd9\x84");    /* ال */
            p = latin + (dash - lower) + 1;
        }
    }
    while (*p) {
        const char *end = strchr(p, '-');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        snprintf(part, sizeof(part), "%.*s", (int)n, p);
        if (out[0] == '\0' && az_english(part))
            strncat(out, part, capacity - strlen(out) - 1);
        else {
            az_arabic(part, arabic, sizeof(arabic), tashkeel);
            strncat(out, arabic, capacity - strlen(out) - 1);
        }
        p += n;
        if (*p == '-') ++p;
    }
}

static int az_is_article(const char *word, const char *next)
{
    char lower[AZ_WORD + 1], letter[4], nlower[AZ_WORD + 1];
    snprintf(lower, sizeof(lower), "%s", word);
    az_lower(lower);
    if (az_in(az.article, az.article_count, lower)) return 1;
    if (!az_sun_article(lower, letter, sizeof(letter), 1)) return 0;
    if (next == NULL) return 1;
    snprintf(nlower, sizeof(nlower), "%s", next);
    az_lower(nlower);
    return strncmp(nlower, letter, strlen(letter)) == 0;
}

/* The held words (the current word last) as one commit. */
static void az_reading(char words[][AZ_WORD + 1], int n, int tashkeel, char *out, size_t capacity)
{
    char key[AZ_PHRASE * 2], piece[AZ_OUT], lower[AZ_WORD + 1];
    int i = 0;
    out[0] = '\0';
    while (i < n) {
        const char *phrase = NULL;
        int m, used = 0;
        for (m = n - i; m >= 1 && !phrase; --m) {
            int j;
            key[0] = '\0';
            for (j = i; j < i + m; ++j) {
                snprintf(lower, sizeof(lower), "%s", words[j]);
                az_lower(lower);
                if (j > i) strncat(key, " ", sizeof(key) - strlen(key) - 1);
                strncat(key, lower, sizeof(key) - strlen(key) - 1);
            }
            if ((phrase = az_phrase(az.phrases, az.phrase_count, key)) != NULL) used = m;
        }
        if (out[0] != '\0') strncat(out, " ", capacity - strlen(out) - 1);
        if (phrase) {
            strncat(out, phrase, capacity - strlen(out) - 1);
            i += used;
            continue;
        }
        snprintf(lower, sizeof(lower), "%s", words[i]);
        az_lower(lower);
        if (i + 1 < n && az_is_article(words[i], words[i + 1])) {
            const char *rest = piece;
            az_word_reading(words[i + 1], tashkeel, piece, sizeof(piece));
            if (strncmp(rest, "\xd8\xa7\xd9\x84", 4) == 0) rest += 4;
            strncat(out, "\xd8\xa7\xd9\x84", capacity - strlen(out) - 1);
            strncat(out, rest, capacity - strlen(out) - 1);
            i += 2;
            continue;
        }
        if (i + 1 < n && az_in(az.conj, az.conj_count, lower)) {
            strncat(out, "\xd9\x88", capacity - strlen(out) - 1);   /* و */
            az_reading(&words[i + 1], 1, tashkeel, piece, sizeof(piece));
            strncat(out, piece, capacity - strlen(out) - 1);
            i += 2;
            continue;
        }
        az_word_reading(words[i], tashkeel, piece, sizeof(piece));
        strncat(out, piece, capacity - strlen(out) - 1);
        ++i;
    }
}

/* ---- typing: one key at a time ---- */

typedef struct {
    char held[AZ_HELD][AZ_WORD + 1];
    int held_count;
    char word[AZ_WORD + 1];
    int tashkeel;
} AzEngine;

static int az_letter(int c)
{
    return isalnum(c) || c == '\'' || c == '`' || c == '-';
}

/* Whether a word ended by a space stays in the preedit. */
static int az_holds(const AzEngine *e)
{
    char lower[AZ_WORD + 1], key[AZ_PHRASE * 2], letter[4];
    int i;
    snprintf(lower, sizeof(lower), "%s", e->word);
    az_lower(lower);
    if (e->held_count == AZ_HELD) return 0;
    if (e->held_count > 0) {
        char last[AZ_WORD + 1];
        snprintf(last, sizeof(last), "%s", e->held[e->held_count - 1]);
        az_lower(last);
        if (az_in(az.conj, az.conj_count, last) || az_in(az.article, az.article_count, last))
            goto phrase;   /* its joining word ends that hold */
    }
    if (az_in(az.article, az.article_count, lower) || az_in(az.conj, az.conj_count, lower) ||
        az_sun_article(lower, letter, sizeof(letter), 0))
        return 1;
phrase:
    key[0] = '\0';
    for (i = 0; i < e->held_count; ++i) {
        char h[AZ_WORD + 1];
        snprintf(h, sizeof(h), "%s", e->held[i]);
        az_lower(h);
        strncat(key, h, sizeof(key) - strlen(key) - 1);
        strncat(key, " ", sizeof(key) - strlen(key) - 1);
    }
    strncat(key, lower, sizeof(key) - strlen(key) - 1);
    for (i = 0; i < az.start_count; ++i)
        if (strcmp(az.starts[i], key) == 0) return 1;
    return 0;
}

/* Everything held as one commit, with `tail` after it; "" when nothing is
   held. The engine is empty after. */
static void az_flush(AzEngine *e, const char *tail, char *out, size_t capacity)
{
    out[0] = '\0';
    if (e->word[0] != '\0' && e->held_count < AZ_HELD)
        snprintf(e->held[e->held_count++], AZ_WORD + 1, "%s", e->word);
    if (e->held_count > 0)
        az_reading(e->held, e->held_count, e->tashkeel, out, capacity);
    if (e->held_count > 0 || tail[0] != '\0')
        strncat(out, tail, capacity - strlen(out) - 1);
    e->held_count = 0;
    e->word[0] = '\0';
}

/* One key, an ASCII character (`\b` Backspace, `\n` Enter): `commit` gets
   what is committed ("" for nothing); returns 1 when the key itself
   passes on after the commit. */
static int az_feed(AzEngine *e, int key, char *commit, size_t capacity)
{
    size_t n = strlen(e->word);
    commit[0] = '\0';
    if (az_letter(key)) {
        if (n < AZ_WORD) { e->word[n] = (char)key; e->word[n + 1] = '\0'; }
        return 0;
    }
    if (key == ' ') {
        if (n > 0 && az_holds(e)) {
            snprintf(e->held[e->held_count++], AZ_WORD + 1, "%s", e->word);
            e->word[0] = '\0';
            return 0;
        }
        az_flush(e, " ", commit, capacity);
        return 0;
    }
    if (key == '\b') {
        if (n > 0) { e->word[n - 1] = '\0'; return 0; }
        if (e->held_count > 0) {
            snprintf(e->word, sizeof(e->word), "%s", e->held[--e->held_count]);
            return 0;
        }
        return 1;
    }
    if (key == '\n') {
        az_flush(e, "", commit, capacity);
        return 1;
    }
    {
        char mark[8];
        int i;
        snprintf(mark, sizeof(mark), "%c", key);
        for (i = 0; i < az.punct_count; ++i)
            if (strcmp(az.punct[i][0], mark) == 0) snprintf(mark, sizeof(mark), "%s", az.punct[i][1]);
        az_flush(e, mark, commit, capacity);
        return 0;
    }
}

/* The dry run (POST /v1/input/arabizi/convert): `text` fed key by key, as
   typing gives it, and what is held at the end flushed. */
static int az_convert(const char *text, int tashkeel, char *out, size_t capacity)
{
    AzEngine e;
    char commit[AZ_OUT];
    const char *p;
    if (!az_load()) return 0;
    memset(&e, 0, sizeof(e));
    e.tashkeel = tashkeel;
    out[0] = '\0';
    for (p = text; *p; ++p) {
        int passed = az_feed(&e, (unsigned char)*p, commit, sizeof(commit));
        strncat(out, commit, capacity - strlen(out) - 1);
        if (passed && *p == '\n') strncat(out, "\n", capacity - strlen(out) - 1);
    }
    az_flush(&e, "", commit, sizeof(commit));
    strncat(out, commit, capacity - strlen(out) - 1);
    return 1;
}

#endif
