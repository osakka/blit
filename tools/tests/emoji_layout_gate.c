#define main blit_emoji_layout_server_entry
#include "../../src/blit_server.c"
#undef main
#define CHECK(t) do { if (!(t)) { fprintf(stderr, "EMOJI_LAYOUT_FAIL row=%u line=%d\n", row, __LINE__); return 1; } } while (0)
static void reset_pty(BlitSession *session, BlitSurface *surface, int policy)
{
    int x, y;
    memset(session, 0, sizeof(*session));
    session->pty_fd = -1;
    session->terminal_autowrap = 1;
    session->terminal_emoji_layout = policy;
    surface->width = 16; surface->height = 4;
    surface->cursor_x = surface->cursor_y = 0;
    surface->scroll_top = 0; surface->scroll_bottom = 3;
    surface->lrmm_enabled = 0; surface->left_margin = 0; surface->right_margin = 15;
    surface->damage_valid = 0;
    for (y = 0; y < 4; ++y) for (x = 0; x < 16; ++x) terminal_clear_cell(surface, x, y);
}

static void feed(BlitRuntime *runtime, BlitSession *session, BlitSurface *surface, const char *s)
{ terminal_consume_output(runtime, session, surface, (const unsigned char *)s, strlen(s)); }

static int pty_edges(BlitRuntime *runtime, BlitSession *session, BlitSurface *surface)
{
    unsigned row = BLIT_EMOJI_SEQUENCE_COUNT;
    reset_pty(session, surface, 1); surface->cursor_x = 15;
    feed(runtime, session, surface, "1\xef\xb8\x8f\xe2\x83\xa3X");
    CHECK(!surface->codepoints[0][15] && surface->codepoints[1][0] == '1');
    CHECK(surface->combining_count[1][0] == 2 && surface->codepoints[1][2] == 'X');
    reset_pty(session, surface, 1); surface->cursor_x = 15; surface->cursor_y = 3;
    feed(runtime, session, surface, "1\xef\xb8\x8f\xe2\x83\xa3");
    CHECK(surface->codepoints[3][0] == '1' && surface->combining_count[3][0] == 2 && !surface->codepoints[2][15]);
    reset_pty(session, surface, 1); surface->cursor_x = 15; session->terminal_autowrap = 0;
    feed(runtime, session, surface, "1\xef\xb8\x8f\xe2\x83\xa3");
    CHECK(surface->combining_count[0][15] == 2 && !(surface->attributes[0][15] & BLIT_ATTR_WIDE));
    reset_pty(session, surface, 1); surface->width = 1; surface->right_margin = 0;
    feed(runtime, session, surface, "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
    CHECK(surface->cursor_y == 0 && surface->combining_count[0][0] == 2 && !(surface->attributes[0][0] & BLIT_ATTR_WIDE));
    reset_pty(session, surface, 1); surface->lrmm_enabled = 1;
    surface->left_margin = 4; surface->right_margin = 7; surface->cursor_x = 7;
    feed(runtime, session, surface, "1\xef\xb8\x8f\xe2\x83\xa3X");
    CHECK(surface->codepoints[1][4] == '1' && surface->codepoints[1][6] == 'X');
    reset_pty(session, surface, 1);
    feed(runtime, session, surface, "abc\r\033[4h1\xef\xb8\x8f\xe2\x83\xa3");
    CHECK(surface->codepoints[0][2] == 'a' && surface->codepoints[0][3] == 'b' && surface->codepoints[0][4] == 'c');
    reset_pty(session, surface, 1);
    feed(runtime, session, surface, "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\033[2b");
    CHECK(surface->combining_count[0][0] == 2 && surface->combining_count[0][2] == 2 && surface->combining_count[0][4] == 2 && surface->cursor_x == 6);
    feed(runtime, session, surface, "\033[1;2HZ");
    CHECK(!surface->codepoints[0][0] && surface->codepoints[0][1] == 'Z' && surface->combining_count[0][0] == 0);
    reset_pty(session, surface, 1);
    feed(runtime, session, surface, "\xf0\x9f\x91\xa9\033[0m\xe2\x80\x8d\xf0\x9f\x92\xbb");
    CHECK(surface->combining_count[0][0] == 1 && surface->codepoints[0][2] == 0x1f4bb);
    reset_pty(session, surface, 1);
    feed(runtime, session, surface, "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb\033[?1049hA\033[?1049l");
    CHECK(surface->combining_count[0][0] == 2 && surface->codepoints[0][0] == 0x1f469);
    feed(runtime, session, surface, "\033[2J");
    CHECK(!surface->codepoints[0][0] && !surface->combining_count[0][0]);
    feed(runtime, session, surface, "\033c\033[b");
    CHECK(!session->terminal_last_codepoint && !surface->codepoints[0][0] && session->terminal_emoji_layout);
    terminal_scrollback_release(surface);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned row = 0, n, i, j, policy;
    uint32_t values[BLIT_CLUSTER_MAX_SCALARS];
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    BlitSession *session = calloc(1, sizeof(*session));
    BlitSurface *surface = calloc(1, sizeof(*surface));
    CHECK(runtime && session && surface);
    {
        int layout = -1;
        CHECK(text_layout_from_json("{\"environment\":{\"text_layout\":\"emoji\"}}", &layout) && layout == 0);
    }
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
        for (policy = 0; policy <= 1; ++policy) {
            unsigned fragmented;
            int expected_x = 0;
            if (policy && n > 1) expected_x = 2;
            else for (i = 0; i < n; ++i) expected_x += unicode_cell_width(values[i]);
            for (fragmented = 0; fragmented <= 1; ++fragmented) {
                reset_pty(session, surface, policy);
                if (fragmented) for (i = 0; text[i]; ++i)
                    terminal_consume_output(runtime, session, surface, (const unsigned char *)text + i, 1);
                else feed(runtime, session, surface, text);
                CHECK(surface->codepoints[0][expected_x] == 'X' && surface->cursor_x == expected_x + 1);
                offset = 0;
                for (i = 0; i < (unsigned)expected_x; ++i) {
                    if (surface->attributes[0][i] & BLIT_ATTR_CONTINUATION) continue;
                    CHECK(offset < n && surface->codepoints[0][i] == values[offset++]);
                    for (j = 0; j < surface->combining_count[0][i]; ++j)
                        CHECK(offset < n && surface->combining[0][i][j] == values[offset++]);
                }
                CHECK(offset == n && session->terminal_utf8_pending_length == 0);
                if (policy) CHECK(surface->combining_count[0][0] == n - 1);
            }
        }
        ++row;
    }
    CHECK(argc == 2 && row == (unsigned)atoi(argv[1]) && row == BLIT_EMOJI_SEQUENCE_COUNT);
    CHECK(pty_edges(runtime, session, surface) == 0);
    free(runtime); free(session); free(surface);
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
    printf("EMOJI_LAYOUT_PASS corpus=%u policies=scalar+emoji PTY=contiguous+byte_fragmented exact_scalars=all edges=wrap+scroll+margins+insert+REP+erase+alternate+barriers invalid_policy=atomic scope=parser_not_physical\n", row);
    return 0;
}
