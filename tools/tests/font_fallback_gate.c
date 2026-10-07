/* Regression gate for visible fallback of unmapped Unicode scalars. */
#include "blit_font.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    BlitFont font;
    uint32_t codepoints[4] = {0, 1, 2, 3};
    int i;

    memset(&font, 0, sizeof(font));
    font.glyph_count = 4;
    font.codepoints = codepoints;
    for (i = 0; i < 256; ++i)
        font.ascii_glyph[i] = -1;
    font.ascii_glyph[(unsigned char)'?'] = 2;
    if (blit_font_glyph_index(&font, 0x1f600U) != 2) {
        fprintf(stderr, "FONT_FALLBACK_FAIL: missing scalar was not mapped to '?'");
        return 1;
    }
    if (blit_font_glyph_index(&font, 2U) != 2) {
        fprintf(stderr, "FONT_FALLBACK_FAIL: mapped scalar changed");
        return 1;
    }
    printf("FONT_FALLBACK_PASS replacement_glyph=2\n");
    return 0;
}
