/* ADR 0241: the letters a run of Hibr's cells stands for. Reads lines of
   cells ("hex,hex;hex;..." per line, visual order), splits each into runs,
   and prints each run's kind and logical text as "kind hex hex ...".
   With an argument "keys", checks the key's equality and hash instead. */
#include "../../src/blit_cell_word.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cell_of(const char *spec, BlitCellCluster *cell)
{
    uint32_t scalars[BLIT_CLUSTER_MAX_SCALARS];
    int n = 0;
    char copy[512], *save, *t;
    snprintf(copy, sizeof(copy), "%s", spec);
    for (t = strtok_r(copy, ",", &save); t && n < BLIT_CLUSTER_MAX_SCALARS; t = strtok_r(NULL, ",", &save))
        scalars[n++] = (uint32_t)strtoul(t, NULL, 16);
    return n && blit_cell_cluster_init(cell, scalars[0], scalars + 1, (size_t)(n - 1), 1);
}

static int keys(void)
{
    BlitCellWord a, b;
    uint32_t fatha = 0x064e;
    memset(&a, 0, sizeof(a));
    a.kind = BLIT_WORD_LETTERS; a.count = 2;
    if (!blit_cell_cluster_init(&a.cells[0], 0xfeae, NULL, 0, 1) ||
        !blit_cell_cluster_init(&a.cells[1], 0xfe92, NULL, 0, 1)) return 1;
    b = a;
    if (!blit_cell_word_equal(&a, &b) || blit_cell_word_hash(&a) != blit_cell_word_hash(&b)) return 1;
    if (!blit_cell_cluster_init(&b.cells[1], 0xfe92, &fatha, 1, 1)) return 1;
    if (blit_cell_word_equal(&a, &b)) return 1;            /* one mark more */
    b = a; b.count = 1;
    if (blit_cell_word_equal(&a, &b)) return 1;            /* shorter */
    b = a; b.kind = BLIT_WORD_DIGITS;
    if (blit_cell_word_equal(&a, &b)) return 1;            /* another kind */
    puts("WORD_KEYS ok");
    return 0;
}

int main(int argc, char **argv)
{
    char line[16384];
    if (argc > 1 && !strcmp(argv[1], "keys")) return keys();
    while (fgets(line, sizeof(line), stdin)) {
        char *save, *spec;
        BlitCellWord word;
        memset(&word, 0, sizeof(word));
        line[strcspn(line, "\n")] = 0;
        for (spec = strtok_r(line, ";", &save);; spec = strtok_r(NULL, ";", &save)) {
            BlitCellCluster cell;
            int kind = BLIT_WORD_NONE;
            if (spec && cell_of(spec, &cell)) kind = blit_cell_word_kind(&cell);
            if (word.count && (kind != word.kind || kind == BLIT_WORD_SINGLE ||
                               word.count == BLIT_WORD_MAX_CELLS)) {
                uint32_t text[BLIT_WORD_MAX_TEXT];
                int at[BLIT_WORD_MAX_TEXT], n = blit_cell_word_text(&word, text, at, BLIT_WORD_MAX_TEXT), i;
                if (n < 0) return 2;
                printf("%d", word.kind);
                for (i = 0; i < n; ++i) printf(" %x", text[i]);
                putchar('\n');
                word.count = 0;
            }
            if (!spec) break;
            if (kind != BLIT_WORD_NONE) { word.kind = kind; word.cells[word.count++] = cell; }
        }
        puts("--");
    }
    return 0;
}
