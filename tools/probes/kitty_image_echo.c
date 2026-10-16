/* Minimal Kitty graphics producer used only by deterministic parser gates. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

static void put_be32(unsigned char *out, unsigned long value)
{
    out[0] = (unsigned char)((value >> 24) & 0xff);
    out[1] = (unsigned char)((value >> 16) & 0xff);
    out[2] = (unsigned char)((value >> 8) & 0xff);
    out[3] = (unsigned char)(value & 0xff);
}

static size_t append_png_chunk(unsigned char *png, size_t offset,
                               const char *type, const unsigned char *body,
                               size_t length)
{
    put_be32(png + offset, (unsigned long)length);
    memcpy(png + offset + 4, type, 4);
    memcpy(png + offset + 8, body, length);
    put_be32(png + offset + 8 + length,
             (unsigned long)crc32(crc32(0L, Z_NULL, 0),
                                  png + offset + 4, (uInt)(4 + length)));
    return offset + 12 + length;
}

static char base64_character(unsigned int value)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return alphabet[value & 63U];
}

static size_t encode_base64(const unsigned char *input, size_t length,
                            char *output)
{
    size_t in = 0, out = 0;
    while (in + 3 <= length) {
        unsigned int value = ((unsigned int)input[in] << 16) |
                             ((unsigned int)input[in + 1] << 8) |
                             (unsigned int)input[in + 2];
        output[out++] = base64_character(value >> 18);
        output[out++] = base64_character(value >> 12);
        output[out++] = base64_character(value >> 6);
        output[out++] = base64_character(value);
        in += 3;
    }
    if (in < length) {
        unsigned int value = (unsigned int)input[in] << 16;
        output[out++] = base64_character(value >> 18);
        if (in + 1 < length) {
            value |= (unsigned int)input[in + 1] << 8;
            output[out++] = base64_character(value >> 12);
            output[out++] = base64_character(value >> 6);
            output[out++] = '=';
        } else {
            output[out++] = base64_character(value >> 12);
            output[out++] = '=';
            output[out++] = '=';
        }
    }
    output[out] = '\0';
    return out;
}

static void emit_large_png(void)
{
    unsigned char raw[64 * (1 + 64 * 4)];
    unsigned char compressed[40000];
    unsigned char png[50000];
    char encoded[70000];
    unsigned char ihdr[13];
    size_t raw_offset = 0, png_length = 0, encoded_length;
    size_t offset, chunk_length;
    uLongf compressed_length = sizeof(compressed);
    int x, y;

    for (y = 0; y < 64; ++y) {
        raw[raw_offset++] = 0;
        for (x = 0; x < 64; ++x) {
            raw[raw_offset++] = (unsigned char)((x * 37 + y * 13) & 255);
            raw[raw_offset++] = (unsigned char)((x * 11 + y * 53) & 255);
            raw[raw_offset++] = (unsigned char)((x * 71 + y * 7) & 255);
            raw[raw_offset++] = 255;
        }
    }
    if (compress2(compressed, &compressed_length, raw, raw_offset,
                  Z_BEST_SPEED) != Z_OK)
        return;
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    png_length = 8;
    put_be32(ihdr, 64); put_be32(ihdr + 4, 64);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_length = append_png_chunk(png, png_length, "IHDR", ihdr, 13);
    png_length = append_png_chunk(png, png_length, "IDAT", compressed,
                                  (size_t)compressed_length);
    png_length = append_png_chunk(png, png_length, "IEND", NULL, 0);
    encoded_length = encode_base64(png, png_length, encoded);
    for (offset = 0; offset < encoded_length; offset += chunk_length) {
        int more;
        chunk_length = encoded_length - offset;
        if (chunk_length > 4096) chunk_length = 4096;
        more = offset + chunk_length < encoded_length;
        if (offset == 0)
            printf("\033_Ga=T,f=100,s=64,v=64,i=78,m=%d,q=2;", more);
        else
            printf("\033_Gq=2,m=%d;", more);
        fwrite(encoded + offset, 1, chunk_length, stdout);
        fputs("\033\\", stdout);
    }
    fputs("PNG-LARGE\r\n", stdout);
}

int main(int argc, char **argv)
{
    static const char transmit[] =
        "\033_Ga=T,f=24,s=1,v=1,i=7,m=1;/w\033\\"
        /* Kitty continuation chunks carry q/m but deliberately omit i. */
        "\033_Gq=2,m=0;AA\033\\";
    static const char place[] = "\033_Ga=p,i=7;\033\\";
    static const char rejected[] = "\033_Ga=t,f=24,s=513,v=1,i=8;AAAA\033\\";
    static const char deleted[] = "\033_Ga=d,i=7;\033\\";
    int index;
    int limits = argc > 1 && strcmp(argv[1], "--limits") == 0;
    int large = argc > 1 && strcmp(argv[1], "--large") == 0;
    int png_large = argc > 1 && strcmp(argv[1], "--png-large") == 0;
    if (png_large) {
        emit_large_png();
        fflush(stdout);
        sleep(10);
        return 0;
    }
    if (large) {
        /* Hibr's wallpaper encoder uses 4096 base64-byte chunks.  A 64x64
           RGB image is exactly 16384 base64 bytes, so this exercises the
           opening chunk plus two continuations and a final chunk without
           depending on a large API argument or a producer-specific fixture. */
        fputs("\033_Ga=T,f=24,s=64,v=64,i=77,m=1,q=2;", stdout);
        for (index = 0; index < 1024; ++index) fputs("/wAA", stdout);
        fputs("\033\\", stdout);
        for (index = 1; index < 4; ++index) {
            fprintf(stdout, "\033_Gq=2,m=%d;", index == 3 ? 0 : 1);
            for (int byte = 0; byte < 1024; ++byte) fputs("/wAA", stdout);
            fputs("\033\\", stdout);
        }
        fputs("LARGE\r\n", stdout);
        fflush(stdout);
        sleep(10);
        return 0;
    }
    if (limits) {
        for (index = 1; index <= 8; ++index)
            printf("\033_Ga=t,f=24,s=1,v=1,i=%d,m=1;/w\033\\", index);
        fputs("\033_Ga=t,f=24,s=1,v=1,i=9,m=1;/w\033\\", stdout);
        fputs("\033_Ga=t,f=24,s=513,v=1,i=10;AAAA\033\\", stdout);
        fputs("LIMITS\r\n", stdout);
        fflush(stdout);
        sleep(10);
        return 0;
    }
    fputs(transmit, stdout);
    fputs(place, stdout);
    if (argc > 1 && strcmp(argv[1], "--delete") == 0)
        fputs(deleted, stdout);
    fputs(rejected, stdout);
    fputs("OK\r\n", stdout);
    fflush(stdout);
    sleep(10);
    return 0;
}
