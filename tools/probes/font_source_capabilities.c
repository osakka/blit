/* Build-time dependency experiment, not a Blit glyph renderer. Verifies that
   TCC can use the selected FreeType installation with candidate source fonts.
   Does not change the server, install fonts or claim emoji sequence support. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>

static int outline(FT_Library library, const char *path)
{
    FT_Face face;
    int sizes[] = {17, 23, 32};
    int i;
    if (FT_New_Face(library, path, 0, &face)) return 0;
    if (!FT_IS_SCALABLE(face)) { FT_Done_Face(face); return 0; }
    printf("\"outline\":[");
    for (i = 0; i < 3; ++i) {
        FT_Bitmap *bitmap;
        unsigned row, col, partial = 0;
        if (FT_Set_Pixel_Sizes(face, 0, sizes[i]) ||
            !FT_Get_Char_Index(face, 'A') ||
            FT_Load_Char(face, 'A', FT_LOAD_RENDER)) {
            FT_Done_Face(face); return 0;
        }
        bitmap = &face->glyph->bitmap;
        if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->pitch < 0) {
            FT_Done_Face(face); return 0;
        }
        for (row = 0; row < bitmap->rows; ++row)
            for (col = 0; col < bitmap->width; ++col) {
                unsigned value = bitmap->buffer[row * bitmap->pitch + col];
                if (value && value < 255) ++partial;
            }
        if (!partial) { FT_Done_Face(face); return 0; }
        printf("%s{\"requested_em_px\":%d,\"line_height_px\":%ld,"
               "\"ascender_px\":%ld,\"descender_px\":%ld,"
               "\"width\":%u,\"height\":%u,\"antialiased_pixels\":%u}",
               i ? "," : "", sizes[i], face->size->metrics.height / 64,
               face->size->metrics.ascender / 64, face->size->metrics.descender / 64,
               bitmap->width, bitmap->rows, partial);
    }
    printf("]");
    FT_Done_Face(face);
    return 1;
}

static int colour(FT_Library library, const char *path)
{
    FT_Face face;
    FT_Bitmap *bitmap;
    unsigned row, col, coloured = 0;
    if (FT_New_Face(library, path, 0, &face)) return 0;
    if (!FT_HAS_COLOR(face) || face->num_fixed_sizes < 1 ||
        FT_Select_Size(face, 0) || !FT_Get_Char_Index(face, 0x1f600) ||
        FT_Load_Char(face, 0x1f600, FT_LOAD_COLOR | FT_LOAD_RENDER)) {
        FT_Done_Face(face); return 0;
    }
    bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_BGRA || bitmap->pitch < 0) {
        FT_Done_Face(face); return 0;
    }
    for (row = 0; row < bitmap->rows; ++row)
        for (col = 0; col < bitmap->width; ++col) {
            const unsigned char *p = bitmap->buffer + row * bitmap->pitch + col * 4;
            /* FreeType promises premultiplied BGRA. Check that contract,
               without implementing a second compositor in this probe. */
            if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) {
                FT_Done_Face(face); return 0;
            }
            if (p[3] && (p[0] != p[1] || p[1] != p[2])) ++coloured;
        }
    if (!coloured) { FT_Done_Face(face); return 0; }
    printf(",\"emoji\":{\"scalar\":128512,\"format\":\"premultiplied-bgra\","
           "\"width\":%u,\"height\":%u,\"coloured_pixels\":%u}",
           bitmap->width, bitmap->rows, coloured);
    FT_Done_Face(face);
    return 1;
}

int main(int argc, char **argv)
{
    FT_Library library;
    int major, minor, patch;
    if (argc != 3) {
        fprintf(stderr, "usage: font-source-capabilities OUTLINE.ttf COLOUR.ttf\n");
        return 2;
    }
    if (FT_Init_FreeType(&library)) return 1;
    FT_Library_Version(library, &major, &minor, &patch);
    printf("{\"freetype\":\"%d.%d.%d\",", major, minor, patch);
    if (!outline(library, argv[1]) || !colour(library, argv[2])) {
        fprintf(stderr, "FONT_SOURCE_CAPABILITIES_FAIL\n");
        FT_Done_FreeType(library); return 1;
    }
    printf(",\"scope\":\"source-rasterizer-only-not-server-acceptance\"}\n");
    FT_Done_FreeType(library);
    return 0;
}
