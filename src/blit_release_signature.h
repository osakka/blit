#ifndef BLIT_RELEASE_SIGNATURE_H
#define BLIT_RELEASE_SIGNATURE_H

/* Release signatures (security review SR-2, Gitea #184; ADR 0232).

   A release is signed with a Blit Ed25519 key (RFC 8032, Monocypher's
   crypto_ed25519_*, vendor/monocypher). What is signed is a statement the
   update helper recomputes from the packages it was handed -- each one's
   Package and Version fields and the SHA-256 of the helper's own copy --
   so a signature cannot be replayed for another package or version:

       blit-release-signature 1
       blit <version> <sha256>
       blit-compositor <version> <sha256>

   blit first, then blit-compositor when the update carries it, each line
   ending in LF. The first line keeps a release signature from meaning
   anything else signed with the key.

   Key files hold one line, tagged by kind, so a secret key can never be
   taken for a public one (a seed and a public key are both 32 bytes):

       blit-release-ed25519-public <64 lowercase hex>
       blit-release-ed25519-secret <64 lowercase hex: the 32-byte seed>

   The helper, tools/release/blit-sign.c and
   tools/tests/release_signature_gate.sh share this file: one statement,
   one key format, one verifier.

   ADR 0245 section 7: the same keys and the same verifier sign glyph sets,
   over a manifest of the set's three files whose first line is its own
   label, so neither signature can pass as the other:

       blit-glyph-set-signature 1
       set <name> <version>
       font <sha256>
       names <sha256>
       licence <sha256>

   (blit_glyph_manifest_write, blit_glyph_manifest_parse; the server's
   glyph store and blit-sign's glyph-set mode). */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../vendor/monocypher/monocypher.h"
#include "../vendor/monocypher/monocypher-ed25519.h"

#define BLIT_RELEASE_STATEMENT_HEADER "blit-release-signature 1\n"
#define BLIT_RELEASE_PUBLIC_TAG "blit-release-ed25519-public "
#define BLIT_RELEASE_SECRET_TAG "blit-release-ed25519-secret "
#define BLIT_RELEASE_SIGNATURE_HEX 128
/* The header, and two lines of a name, a version of at most 127
   characters and a hash. */
#define BLIT_RELEASE_STATEMENT_CAPACITY 512
#define BLIT_RELEASE_KEY_FILE_MAX 256

/* The first 2 * BYTES characters of HEX are lowercase hex digits, decoded
   into OUT unless it is NULL. The caller checks what follows them. */
static int blit_release_hex(const char *hex, size_t bytes, uint8_t *out)
{
    size_t i;
    for (i = 0; i < 2 * bytes; ++i) {
        char c = hex[i];
        int nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else return 0;
        if (out != NULL) {
            if (i % 2 == 0) out[i / 2] = (uint8_t)(nibble << 4);
            else out[i / 2] |= (uint8_t)nibble;
        }
    }
    return 1;
}

static void blit_release_hex_write(const uint8_t *bytes, size_t count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < count; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * count] = '\0';
}

/* A signature as it travels (X-Blit-Signature, the helper's request line,
   the release's .sig file): exactly 128 lowercase hex digits. */
static int blit_release_signature_parse(const char *text, uint8_t signature[64])
{
    return strlen(text) == BLIT_RELEASE_SIGNATURE_HEX &&
        blit_release_hex(text, 64, signature);
}

/* A Debian version as dpkg allows it, at most 127 characters. */
static int blit_release_version_valid(const char *version)
{
    size_t i, length = strlen(version);
    if (length == 0 || length > 127 || version[0] < '0' || version[0] > '9')
        return 0;
    for (i = 0; i < length; ++i) {
        char c = version[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '+' || c == '~' ||
              c == '-' || c == ':'))
            return 0;
    }
    return 1;
}

/* The statement for an update carrying blit and, when COMPOSITOR_VERSION
   is not NULL, blit-compositor. Its length, or 0 when a field is not one
   a statement can carry. */
static size_t blit_release_statement(char *out, size_t capacity,
                                     const char *blit_version,
                                     const char *blit_sha256,
                                     const char *compositor_version,
                                     const char *compositor_sha256)
{
    int length;
    if (!blit_release_version_valid(blit_version) ||
        strlen(blit_sha256) != 64 || !blit_release_hex(blit_sha256, 32, NULL))
        return 0;
    if (compositor_version != NULL &&
        (!blit_release_version_valid(compositor_version) ||
         strlen(compositor_sha256) != 64 ||
         !blit_release_hex(compositor_sha256, 32, NULL)))
        return 0;
    if (compositor_version != NULL)
        length = snprintf(out, capacity,
                          BLIT_RELEASE_STATEMENT_HEADER "blit %s %s\n"
                          "blit-compositor %s %s\n", blit_version,
                          blit_sha256, compositor_version, compositor_sha256);
    else
        length = snprintf(out, capacity,
                          BLIT_RELEASE_STATEMENT_HEADER "blit %s %s\n",
                          blit_version, blit_sha256);
    return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}

/* ---- ADR 0245 section 7: the glyph-set manifest. ---- */

#define BLIT_GLYPH_MANIFEST_HEADER "blit-glyph-set-signature 1\n"
#define BLIT_GLYPH_MANIFEST_MAX 512
#define BLIT_GLYPH_SIGNATURE_BYTES 64
#define BLIT_GLYPH_MANIFEST_NAME_MAX 30

typedef struct {
    char name[BLIT_GLYPH_MANIFEST_NAME_MAX + 1];  /* the set's id without x- */
    char version[128];                            /* the publisher's release */
    char font[65], names[65], licence[65];        /* lowercase hex SHA-256 */
} BlitGlyphManifest;

/* A set's name in a manifest: 1-30 octets that make x-<name> a set id of
   the glyph-set grammar (a-z, 0-9, single hyphens, none at either end). */
static int blit_glyph_manifest_name_valid(const char *name, size_t length)
{
    size_t i;
    if (length < 1 || length > BLIT_GLYPH_MANIFEST_NAME_MAX ||
        name[0] == '-' || name[length - 1] == '-')
        return 0;
    for (i = 0; i < length; ++i) {
        char c = name[i];
        if (c == '-') {
            if (name[i - 1] == '-') return 0;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return 0;
        }
    }
    return 1;
}

/* The manifest for these fields, or 0 when one is not a field a manifest
   carries: blit-sign writes it, never a person. */
static size_t blit_glyph_manifest_write(char *out, size_t capacity, const char *name,
                                        const char *version, const char *font_sha256,
                                        const char *names_sha256, const char *licence_sha256)
{
    int length;
    if (!blit_glyph_manifest_name_valid(name, strlen(name)) ||
        !blit_release_version_valid(version) ||
        strlen(font_sha256) != 64 || !blit_release_hex(font_sha256, 32, NULL) ||
        strlen(names_sha256) != 64 || !blit_release_hex(names_sha256, 32, NULL) ||
        strlen(licence_sha256) != 64 || !blit_release_hex(licence_sha256, 32, NULL))
        return 0;
    length = snprintf(out, capacity, BLIT_GLYPH_MANIFEST_HEADER "set %s %s\nfont %s\n"
                      "names %s\nlicence %s\n", name, version, font_sha256,
                      names_sha256, licence_sha256);
    return length > 0 && (size_t)length < capacity &&
        (size_t)length <= BLIT_GLYPH_MANIFEST_MAX ? (size_t)length : 0;
}

/* One line of a manifest at *CURSOR: KEYWORD and a space, then the rest up
   to the line's LF, which must be there; the rest (without the LF) into
   VALUE. */
static int glyph_manifest_line(const char **cursor, const char *end, const char *keyword,
                               char *value, size_t capacity)
{
    size_t keyword_length = strlen(keyword), length;
    const char *line = *cursor, *lf;
    if ((size_t)(end - line) < keyword_length + 1 ||
        memcmp(line, keyword, keyword_length) != 0 || line[keyword_length] != ' ')
        return 0;
    line += keyword_length + 1;
    lf = memchr(line, '\n', (size_t)(end - line));
    if (lf == NULL) return 0;
    length = (size_t)(lf - line);
    if (length + 1 > capacity || memchr(line, '\0', length) != NULL) return 0;
    memcpy(value, line, length);
    value[length] = '\0';
    *cursor = lf + 1;
    return 1;
}

static int glyph_manifest_sha256(const char *value)
{
    return strlen(value) == 64 && blit_release_hex(value, 32, NULL);
}

/* The manifest's grammar, exactly: its label first, then the four lines in
   order, each ending in one LF, nothing before or after, at most 512 bytes.
   1 with its fields in OUT. A release statement is refused here by its
   first line, whatever key signed it. */
static int blit_glyph_manifest_parse(const char *text, size_t length, BlitGlyphManifest *out)
{
    const char *cursor = text, *end = text + length;
    size_t header = strlen(BLIT_GLYPH_MANIFEST_HEADER);
    char set_line[BLIT_GLYPH_MANIFEST_NAME_MAX + 1 + 128 + 1];
    char *space;
    memset(out, 0, sizeof(*out));
    if (length > BLIT_GLYPH_MANIFEST_MAX || length < header ||
        memcmp(text, BLIT_GLYPH_MANIFEST_HEADER, header) != 0)
        return 0;
    cursor += header;
    if (!glyph_manifest_line(&cursor, end, "set", set_line, sizeof(set_line)))
        return 0;
    space = strchr(set_line, ' ');
    if (space == NULL || strchr(space + 1, ' ') != NULL ||
        !blit_glyph_manifest_name_valid(set_line, (size_t)(space - set_line)))
        return 0;
    *space = '\0';
    if (!blit_release_version_valid(space + 1)) return 0;
    snprintf(out->name, sizeof(out->name), "%s", set_line);
    snprintf(out->version, sizeof(out->version), "%s", space + 1);
    if (!glyph_manifest_line(&cursor, end, "font", out->font, sizeof(out->font)) ||
        !glyph_manifest_sha256(out->font) ||
        !glyph_manifest_line(&cursor, end, "names", out->names, sizeof(out->names)) ||
        !glyph_manifest_sha256(out->names) ||
        !glyph_manifest_line(&cursor, end, "licence", out->licence, sizeof(out->licence)) ||
        !glyph_manifest_sha256(out->licence) || cursor != end)
        return 0;
    return 1;
}

/* A key file's contents: TAG, 64 lowercase hex digits, one LF, nothing
   more. */
static int blit_release_key_text(const char *text, size_t length,
                                 const char *tag, uint8_t key[32])
{
    size_t tag_length = strlen(tag);
    return length == tag_length + 65 && memcmp(text, tag, tag_length) == 0 &&
        text[length - 1] == '\n' && blit_release_hex(text + tag_length, 32, key);
}

/* Read a key file: a regular file, not a link, at most
   BLIT_RELEASE_KEY_FILE_MAX bytes; when STRICT (the helper), also owned by
   root or by the reader, and writable by no one else. With DIRECTORY_FD
   >= 0, NAME is opened in that directory. */
static int blit_release_key_file(int directory_fd, const char *name,
                                 const char *tag, int strict, uint8_t key[32])
{
    char text[BLIT_RELEASE_KEY_FILE_MAX + 1];
    struct stat status;
    ssize_t count;
    size_t used = 0;
    int ok, fd = directory_fd >= 0 ?
        openat(directory_fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK) :
        open(name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return 0;
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) ||
        status.st_size > BLIT_RELEASE_KEY_FILE_MAX ||
        (strict && ((status.st_uid != 0 && status.st_uid != geteuid()) ||
                    (status.st_mode & 022) != 0))) {
        close(fd);
        return 0;
    }
    while (used < sizeof(text) - 1) {
        count = read(fd, text + used, sizeof(text) - 1 - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    close(fd);
    ok = blit_release_key_text(text, used, tag, key);
    crypto_wipe(text, sizeof(text));
    return ok;
}

/* Does SIGNATURE verify STATEMENT under any public key in DIRECTORY (each
   *.pub file in it; a file that is not a key is skipped)? 1 if so, with
   the key file's name in MATCHED; 0 if not. *KEYS is the number of keys
   found, so a caller can tell "no key" from "no key verifies". STRICT (the
   helper): the directory, like each key file, is writable by no one but
   its owner. */
static int blit_release_verify_directory(const char *directory, int strict,
                                         const uint8_t signature[64],
                                         const char *statement, size_t length,
                                         char *matched, size_t matched_capacity,
                                         int *keys)
{
    struct stat status;
    struct dirent *entry;
    DIR *listing;
    int verified = 0;
    *keys = 0;
    if (matched_capacity > 0) matched[0] = '\0';
    if (lstat(directory, &status) < 0 || !S_ISDIR(status.st_mode) ||
        (strict && (status.st_mode & 022) != 0))
        return 0;
    listing = opendir(directory);
    if (listing == NULL) return 0;
    while (!verified && (entry = readdir(listing)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        uint8_t key[32];
        if (entry->d_name[0] == '.' || name_length < 5 ||
            strcmp(entry->d_name + name_length - 4, ".pub") != 0)
            continue;
        if (!blit_release_key_file(dirfd(listing), entry->d_name,
                                   BLIT_RELEASE_PUBLIC_TAG, strict, key))
            continue;
        ++*keys;
        if (crypto_ed25519_check(signature, key, (const uint8_t *)statement,
                                 length) == 0) {
            verified = 1;
            snprintf(matched, matched_capacity, "%s", entry->d_name);
        }
    }
    closedir(listing);
    return verified;
}

#endif
