#ifndef BLIT_CELL_WORD_H
#define BLIT_CELL_WORD_H

/* ADR 0241: a run of Arabic cells, the key a word is shaped and cached by,
   and the letters it stands for. Header-only and FreeType-free, like
   blit_cell_cluster.h: the raster shapes words, the renderers find them.

   Hibr sends Arabic in visual order, one presentation form per cell
   (lam-alef is one cell), marks on the cell. A run is:
   - letters: adjacent cells whose base is a presentation form, the tatweel
     or a mark (a marks-only cell's marks go with the letter logically
     before it); shaped right to left;
   - digits: adjacent Arabic-Indic digits and their separators; left to
     right, as Hibr sent them;
   - one cell: any other Arabic cell (punctuation, a nominal letter).
   A run longer than BLIT_WORD_MAX_CELLS is cut into pieces of that many,
   counted from its right end (the word's logical start). */

#include "blit_cell_cluster.h"
#include "blit_arabic_forms.h"

#define BLIT_WORD_MAX_CELLS 32
/* Letters a word can stand for: every cell's scalars, lam-alef's second
   letter, and a joiner at each end and between cells. */
#define BLIT_WORD_MAX_TEXT (BLIT_WORD_MAX_CELLS * (BLIT_CLUSTER_MAX_SCALARS + 2) + 2)
#define BLIT_WORD_ZWJ 0x200dU
#define BLIT_WORD_ZWNJ 0x200cU

enum { BLIT_WORD_NONE = 0, BLIT_WORD_LETTERS, BLIT_WORD_DIGITS, BLIT_WORD_SINGLE };

typedef struct {
    int kind, count;
    BlitCellCluster cells[BLIT_WORD_MAX_CELLS];   /* visual order */
} BlitCellWord;

static const BlitArabicForm *blit_arabic_form(uint32_t scalar)
{
    int lo = 0, hi = BLIT_ARABIC_FORM_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (blit_arabic_forms[mid].scalar == scalar) return &blit_arabic_forms[mid];
        if (blit_arabic_forms[mid].scalar < scalar) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static int blit_arabic_mark(uint32_t c)
{
    return (c >= 0x0610 && c <= 0x061a) || (c >= 0x064b && c <= 0x065f) || c == 0x0670 ||
        (c >= 0x06d6 && c <= 0x06dc) || (c >= 0x06df && c <= 0x06e4) || c == 0x06e7 ||
        c == 0x06e8 || (c >= 0x06ea && c <= 0x06ed) || (c >= 0x08ca && c <= 0x08e1) ||
        (c >= 0x08e3 && c <= 0x08ff);
}

static int blit_arabic_digit(uint32_t c)
{ return (c >= 0x0660 && c <= 0x0669) || (c >= 0x06f0 && c <= 0x06f9) || c == 0x066b || c == 0x066c; }

static int blit_arabic_block(uint32_t c)
{
    return (c >= 0x0600 && c <= 0x06ff) || (c >= 0x0750 && c <= 0x077f) ||
        (c >= 0x08a0 && c <= 0x08ff) || (c >= 0xfb50 && c <= 0xfdff) || (c >= 0xfe70 && c <= 0xfefc);
}

/* The kind of run a cell belongs to, BLIT_WORD_NONE for a non-Arabic one. */
static int blit_cell_word_kind(const BlitCellCluster *cell)
{
    uint32_t base = cell->scalars[0];
    if (!cell->length || !blit_arabic_block(base)) return BLIT_WORD_NONE;
    if (blit_arabic_form(base) || base == 0x0640 || blit_arabic_mark(base)) return BLIT_WORD_LETTERS;
    if (blit_arabic_digit(base)) return BLIT_WORD_DIGITS;
    return BLIT_WORD_SINGLE;
}

static int blit_cell_word_equal(const BlitCellWord *a, const BlitCellWord *b)
{
    int i;
    if (a->count != b->count || a->kind != b->kind) return 0;
    for (i = 0; i < a->count; ++i)
        if (!blit_cell_cluster_equal(&a->cells[i], &b->cells[i])) return 0;
    return 1;
}

/* Callers MUST compare the complete key on a hash match. */
static uint32_t blit_cell_word_hash(const BlitCellWord *word)
{
    uint32_t hash = 2166136261U;
    int i;
    hash = (hash ^ (uint32_t)word->count) * 16777619U;
    hash = (hash ^ (uint32_t)word->kind) * 16777619U;
    for (i = 0; i < word->count; ++i)
        hash = (hash ^ blit_cell_cluster_hash(&word->cells[i])) * 16777619U;
    return hash;
}

/* Which neighbours a cell's form joins, logically: bit 0 the letter before,
   bit 1 the letter after; -1 for a cell with no presentation form. */
static int blit_cell_word_joins(const BlitCellCluster *cell)
{
    const BlitArabicForm *form = blit_arabic_form(cell->scalars[0]);
    if (!form) return -1;
    return form->form == 0 ? 0 : form->form == 1 ? 2 : form->form == 2 ? 3 : 1;
}

/* Whether a letter can join the next / the previous one at all: it has an
   initial / a final presentation form (the tatweel joins both). */
static int blit_arabic_joins_next(uint32_t letter)
{
    int i;
    if (letter == 0x0640) return 1;
    for (i = 0; i < BLIT_ARABIC_FORM_COUNT; ++i)
        if (blit_arabic_forms[i].count == 1 && blit_arabic_forms[i].letters[0] == letter &&
            blit_arabic_forms[i].form == 1) return 1;
    return 0;
}
static int blit_arabic_joins_prev(uint32_t letter)
{
    int i;
    if (letter == 0x0640) return 1;
    for (i = 0; i < BLIT_ARABIC_FORM_COUNT; ++i)
        if (blit_arabic_forms[i].count == 1 && blit_arabic_forms[i].letters[0] == letter &&
            blit_arabic_forms[i].form == 3) return 1;
    return 0;
}

/* The text a word stands for, logically: the letters right to left (digits
   left to right), each presentation form decomposed (lam-alef -> lam,
   alef), the cell's marks after its letters. Hibr's forms are kept: a ZWJ
   stands in for a neighbour the run does not have where a form joins one,
   and a ZWNJ keeps apart two letters Hibr left unjoined that would join.
   A whole word gets neither. cell[i] is the visual cell of text[i], -1 for
   a joiner. Returns the length, or -1 if it does not fit `cap`. */
static int blit_cell_word_text(const BlitCellWord *word, uint32_t *text, int *cell, int cap)
{
    int n = 0, k, previous_joins = -1, last_joins = -1;
    uint32_t previous_letter = 0;
    int rtl = word->kind != BLIT_WORD_DIGITS;
#define BLIT_WORD_PUT(scalar, at) do { if (n >= cap) return -1; \
        text[n] = (scalar); cell[n] = (at); ++n; } while (0)
    for (k = 0; k < word->count; ++k) {
        int visual = rtl ? word->count - 1 - k : k;
        const BlitCellCluster *c = &word->cells[visual];
        const BlitArabicForm *form = blit_arabic_form(c->scalars[0]);
        int joins = blit_cell_word_joins(c);
        size_t m;
        if (word->kind == BLIT_WORD_LETTERS && joins >= 0) {
            if (previous_joins < 0 && k == 0 && (joins & 1)) BLIT_WORD_PUT(BLIT_WORD_ZWJ, -1);
            if (previous_joins >= 0 && !(previous_joins & 2) && !(joins & 1) &&
                blit_arabic_joins_next(previous_letter) &&
                blit_arabic_joins_prev(form ? form->letters[0] : c->scalars[0]))
                BLIT_WORD_PUT(BLIT_WORD_ZWNJ, -1);
        }
        if (form) { int i; for (i = 0; i < form->count; ++i) BLIT_WORD_PUT(form->letters[i], visual); }
        else BLIT_WORD_PUT(c->scalars[0], visual);
        if (joins >= 0) previous_letter = form ? form->letters[form->count - 1] : c->scalars[0];
        for (m = 1; m < c->length; ++m) BLIT_WORD_PUT(c->scalars[m], visual);
        if (joins >= 0) previous_joins = last_joins = joins;
    }
    if (word->kind == BLIT_WORD_LETTERS && last_joins >= 0 && (last_joins & 2))
        BLIT_WORD_PUT(BLIT_WORD_ZWJ, -1);
#undef BLIT_WORD_PUT
    return n;
}

#endif
