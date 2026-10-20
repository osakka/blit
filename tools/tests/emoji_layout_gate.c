#define main blit_emoji_layout_server_entry
#include "../../src/blit_server.c"
#undef main
#define CHECK(t) do { if (!(t)) { fprintf(stderr, "EMOJI_LAYOUT_FAIL row=%u line=%d\n", row, __LINE__); return 1; } } while (0)
int main(int argc, char **argv)
{
    unsigned row = 0, n, i, j, policy;
    uint32_t values[BLIT_CLUSTER_MAX_SCALARS];
    while (scanf("%u", &n) == 1) {
        char text[BLIT_CLUSTER_MAX_SCALARS * 4 + 2], *p = text;
        uint32_t cp[BLIT_MAX_CELL_COLS], marks[BLIT_MAX_CELL_COLS * BLIT_MAX_COMBINING];
        unsigned char widths[BLIT_MAX_CELL_COLS], counts[BLIT_MAX_CELL_COLS];
        size_t count, k, offset;
        CHECK(n && n <= BLIT_CLUSTER_MAX_SCALARS);
        for (i = 0; i < n; ++i) {
            CHECK(scanf("%x", &values[i]) == 1);
            p += utf8_encode(values[i], p);
        }
        for (policy = 0; policy <= 1; ++policy) {
            memset(counts, 0, sizeof(counts));
            CHECK(decode_text_cells(text, cp, widths, marks, counts, &count, policy));
            offset = 0;
            for (k = 0; k < count; ++k) {
                CHECK(offset < n && cp[k] == values[offset++]);
                for (j = 0; j < counts[k]; ++j)
                    CHECK(offset < n && marks[k * BLIT_MAX_COMBINING + j] == values[offset++]);
                if (!policy) CHECK(widths[k] == unicode_cell_width(cp[k]));
            }
            CHECK(offset == n);
            if (policy) CHECK(count == 1 && counts[0] == n - 1 &&
                widths[0] == (n > 1 ? 2 : unicode_cell_width(values[0])));
        }
        *p++ = 'X'; *p = 0;
        CHECK(decode_text_cells(text, cp, widths, marks, counts, &count, 1));
        CHECK(count == 2 && cp[1] == 'X' && counts[0] == n - 1);
        ++row;
    }
    CHECK(argc == 2 && row == (unsigned)atoi(argv[1]) && row == BLIT_EMOJI_SEQUENCE_COUNT);
    {
        const char *bad[] = {"null", "true", "0", "[]", "{}", "\"\"", "\"bad\""};
        BlitSurface *surface = calloc(1, sizeof(*surface));
        char json[256];
        CHECK(surface); surface->width = 8; surface->height = 1;
        for (i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
            snprintf(json, sizeof(json), "{\"cells\":[{\"x\":0,\"y\":0,\"text\":\"A\"}],\"text_layout\":%s}", bad[i]);
            CHECK(apply_cell_update(surface, json, NULL) == -2);
            CHECK(!surface->codepoints[0][0] && !surface->damage_valid);
        }
        free(surface);
    }
    printf("EMOJI_LAYOUT_PASS corpus=%u policies=scalar+emoji exact_scalars=all adjacent_text=all invalid_policy=atomic scope=API_decoder_not_PTY\n", row);
    return 0;
}
