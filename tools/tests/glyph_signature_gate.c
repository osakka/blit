/* tools/tests/glyph_signature_gate.c: ADR 0245 section 7, the glyph-set
   manifest and its signature, as src/blit_release_signature.h and the
   glyph store (src/blit_glyph_store.h) use them. Built and run by
   glyph_signature_gate.rb with WORK (an empty scratch directory), a font
   FONT, another font OTHER and a names file NAMES for FONT; signs with a
   throwaway key it makes itself (never a release key). Prints
   GLYPH_SIGNATURE_UNIT_PASS cases=N.

     - the grammar: the manifest blit-sign writes parses; a CR, a byte after
       the last LF, upper-case hex, a missing, repeated or reordered line,
       over 512 bytes, a name or version outside its grammar, and the
       release statement's label are each refused;
     - the label: the release statement and its genuine signature verify
       under the key and are still no manifest; a manifest's genuine
       signature does not verify as a release statement;
     - a flipped bit, another key, no key: refused;
     - the bytes hashed are the bytes parsed: a hook replaces the staged
       font after the store hashed it (as update_pair_gate's swapped); the
       import goes through, and the stored font and FreeType's face are the
       hashed bytes, not the replacement. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int gate_swaps;
static char gate_swap_target[4096], gate_swap_with[4096];
static void gate_swap(void);
#define BLIT_GLYPH_GATE_AFTER_HASH gate_swap()

#include "../../src/blit_glyph_store.h"

static int cases, failures;
static const char *work;

static void check(int condition, const char *what)
{
    ++cases;
    if (!condition) {
        ++failures;
        fprintf(stderr, "GLYPH_SIGNATURE_FAIL %s\n", what);
    }
}

static unsigned char *slurp(const char *path, size_t *length)
{
    int missing;
    return glyph_read_part(path, 64u << 20, length, &missing);
}

static void spill(const char *path, const void *data, size_t length, int mode)
{
    FILE *f;
    unlink(path);
    f = fopen(path, "wb");
    if (!f || fwrite(data, 1, length, f) != length) { perror(path); exit(1); }
    fclose(f);
    chmod(path, (mode_t)mode);
}

/* After the store hashed the parts: the staged font becomes OTHER. */
static void gate_swap(void)
{
    size_t length;
    unsigned char *other;
    if (!gate_swap_target[0]) return;
    other = slurp(gate_swap_with, &length);
    if (!other) return;
    spill(gate_swap_target, other, length, 0600);
    free(other);
    ++gate_swaps;
}

static uint8_t secret_a[64], public_a[32], secret_b[64], public_b[32];

static void keys(void)
{
    uint8_t seed[32];
    char line[160], hex[65], path[4096];
    memset(seed, 0x5a, sizeof(seed));
    crypto_ed25519_key_pair(secret_a, public_a, seed);
    memset(seed, 0xa5, sizeof(seed));
    crypto_ed25519_key_pair(secret_b, public_b, seed);
    snprintf(path, sizeof(path), "%s/keys", work);
    mkdir(path, 0755);
    blit_release_hex_write(public_a, 32, hex);
    snprintf(line, sizeof(line), "%s%s\n", BLIT_RELEASE_PUBLIC_TAG, hex);
    snprintf(path, sizeof(path), "%s/keys/gate-a.pub", work);
    spill(path, line, strlen(line), 0644);
    snprintf(path, sizeof(path), "%s/other", work);
    mkdir(path, 0755);
    blit_release_hex_write(public_b, 32, hex);
    snprintf(line, sizeof(line), "%s%s\n", BLIT_RELEASE_PUBLIC_TAG, hex);
    snprintf(path, sizeof(path), "%s/other/gate-b.pub", work);
    spill(path, line, strlen(line), 0644);
}

static int verifies(const char *directory, const uint8_t signature[64], const char *text, size_t length)
{
    char path[4096], matched[256];
    int count = 0;
    snprintf(path, sizeof(path), "%s/%s", work, directory);
    return blit_release_verify_directory(path, 1, signature, text, length, matched, sizeof(matched), &count);
}

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define SHA_C "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"

static int parses(const char *text)
{
    BlitGlyphManifest m;
    return blit_glyph_manifest_parse(text, strlen(text), &m);
}

static void grammar(void)
{
    char good[600], bad[1200];
    BlitGlyphManifest m;
    size_t length = blit_glyph_manifest_write(good, sizeof(good), "fontawesome", "6.7.2", SHA_A, SHA_B, SHA_C);
    check(length > 0 && strcmp(good, "blit-glyph-set-signature 1\nset fontawesome 6.7.2\nfont " SHA_A
                               "\nnames " SHA_B "\nlicence " SHA_C "\n") == 0,
          "the manifest, byte for byte");
    check(blit_glyph_manifest_parse(good, length, &m) && strcmp(m.name, "fontawesome") == 0 &&
          strcmp(m.version, "6.7.2") == 0 && strcmp(m.font, SHA_A) == 0 && strcmp(m.names, SHA_B) == 0 &&
          strcmp(m.licence, SHA_C) == 0, "the manifest parses into its fields");
    check(!blit_glyph_manifest_write(bad, sizeof(bad), "Font", "1", SHA_A, SHA_B, SHA_C) &&
          !blit_glyph_manifest_write(bad, sizeof(bad), "a--b", "1", SHA_A, SHA_B, SHA_C) &&
          !blit_glyph_manifest_write(bad, sizeof(bad), "abcdefghijklmnopqrstuvwxyzabcde", "1", SHA_A, SHA_B, SHA_C) &&
          !blit_glyph_manifest_write(bad, sizeof(bad), "a", "v3.4.0", SHA_A, SHA_B, SHA_C) &&
          !blit_glyph_manifest_write(bad, sizeof(bad), "a", "1", "AA", SHA_B, SHA_C),
          "nothing outside the grammar is written");
#define REFUSED(text, what) check(!parses(text), "refused: " what)
    REFUSED("blit-glyph-set-signature 1\r\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a CR");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\r\n", "a CR at the end");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\nx", "a byte after the last LF");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n\n", "an empty line after");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C, "no LF on the last line");
    REFUSED(" blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a byte before");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nnames " SHA_B "\nlicence " SHA_C "\n", "upper-case hex");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nlicence " SHA_C "\n", "a missing line");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nnames " SHA_B "\nfont " SHA_A "\nlicence " SHA_C "\n", "reordered lines");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a repeated line");
    REFUSED("blit-glyph-set-signature 1\nset a 1\nfont " SHA_A "a\nnames " SHA_B "\nlicence " SHA_C "\n", "a hash of 65 digits");
    REFUSED("blit-glyph-set-signature 1\nset  a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "two spaces");
    REFUSED("blit-glyph-set-signature 1\nset a 1 2\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a third field");
    REFUSED("blit-glyph-set-signature 1\nset Fa 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "an upper-case name");
    REFUSED("blit-glyph-set-signature 1\nset a- 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a name ending in a hyphen");
    REFUSED("blit-glyph-set-signature 1\nset a v3.4.0\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "a version starting with a letter");
    REFUSED("blit-glyph-set-signature 2\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "another label version");
    REFUSED("blit-release-signature 1\nset a 1\nfont " SHA_A "\nnames " SHA_B "\nlicence " SHA_C "\n", "the release statement's label");
    REFUSED("blit-release-signature 1\nblit 0.3.133-1 " SHA_A "\n", "a release statement");
    snprintf(bad, sizeof(bad), "%s", good);
    memset(bad + length, 'a', 600 - length);
    bad[600] = '\0';
    check(!blit_glyph_manifest_parse(bad, 600, &m), "refused: over 512 bytes");
    memcpy(bad, good, length);
    bad[length - 1] = '\0';
    bad[length - 1] = '\n';
    check(!blit_glyph_manifest_parse(good, length - 1, &m), "refused: cut short by a byte");
    check(!blit_glyph_manifest_parse("", 0, &m), "refused: empty");
#undef REFUSED
}

static void signatures(void)
{
    char manifest[600], statement[BLIT_RELEASE_STATEMENT_CAPACITY];
    uint8_t signature[64], other[64];
    BlitGlyphManifest m;
    size_t length = blit_glyph_manifest_write(manifest, sizeof(manifest), "gate", "1.0", SHA_A, SHA_B, SHA_C);
    size_t statement_length = blit_release_statement(statement, sizeof(statement), "0.3.133-1", SHA_A, NULL, NULL);
    crypto_ed25519_sign(signature, secret_a, (const uint8_t *)manifest, length);
    check(verifies("keys", signature, manifest, length), "a manifest signed by the key verifies");
    check(!verifies("other", signature, manifest, length), "refused: another key");
    memcpy(other, signature, 64);
    other[10] ^= 1;
    check(!verifies("keys", other, manifest, length), "refused: a flipped bit in the signature");
    manifest[40] ^= 1;
    check(!verifies("keys", signature, manifest, length), "refused: a flipped bit in the manifest");
    manifest[40] ^= 1;
    check(!verifies("nokeys", signature, manifest, length), "refused: no key directory");
    /* The label, both ways: one key makes both signatures. */
    crypto_ed25519_sign(other, secret_a, (const uint8_t *)statement, statement_length);
    check(verifies("keys", other, statement, statement_length) &&
          !blit_glyph_manifest_parse(statement, statement_length, &m),
          "the release statement and its genuine signature verify, and are no glyph manifest");
    check(!verifies("keys", signature, statement, statement_length),
          "a glyph manifest's genuine signature does not verify a release statement");
}

/* The whole store import, with the hook swapping the staged font after
   the hash. */
static void swapped(const char *font, const char *other_font, const char *names)
{
    char store[4096], upload[4096], path[4096], manifest[600], hex[3][65];
    unsigned char *font_bytes, *names_bytes, *stored, *other;
    size_t font_length, names_length, stored_length, other_length, allocated = 0;
    const char *licence = "Gate licence text.\n";
    uint8_t signature[64];
    BlitGlyphUpload up;
    BlitGlyphResult r;
    BlitGlyphInstalled *set;
    FT_Face original = NULL;
    int ok, p;

    snprintf(store, sizeof(store), "%s/store", work);
    mkdir(store, 0700);
    setenv("BLIT_GLYPH_STORE", store, 1);
    snprintf(path, sizeof(path), "%s/keys", work);
    setenv("BLIT_GLYPH_KEYS_DIR", path, 1);
    check(blit_glyph_store_open(), blit_glyph_store.reason[0] ? blit_glyph_store.reason : "the store opens");
    font_bytes = slurp(font, &font_length);
    names_bytes = slurp(names, &names_length);
    other = slurp(other_font, &other_length);
    if (!font_bytes || !names_bytes || !other) { check(0, "the fixtures read"); return; }
    blit_sha256_hex(font_bytes, font_length, hex[0]);
    blit_sha256_hex(names_bytes, names_length, hex[1]);
    blit_sha256_hex(licence, strlen(licence), hex[2]);
    {
        GlyphStoreNames n;
        BlitGlyphResult pr;
        check(glyph_names_parse((const char *)names_bytes, names_length, &n, &pr), "the gate's names file parses");
        ok = (int)blit_glyph_manifest_write(manifest, sizeof(manifest), "gate", n.release, hex[0], hex[1], hex[2]);
        glyph_names_free(&n);
    }
    crypto_ed25519_sign(signature, secret_a, (const uint8_t *)manifest, strlen(manifest));
    snprintf(upload, sizeof(upload), "%s/store/staging/upload-gate", work);
    mkdir(upload, 0700);
    memset(&up, 0, sizeof(up));
    for (p = 0; p < BLIT_GLYPH_PART_COUNT; ++p) {
        const void *data = p == 0 ? (const void *)manifest : p == 1 ? (const void *)signature :
            p == 2 ? (const void *)names_bytes : p == 3 ? (const void *)font_bytes : (const void *)licence;
        size_t length = p == 0 ? strlen(manifest) : p == 1 ? 64 : p == 2 ? names_length :
            p == 3 ? font_length : strlen(licence);
        snprintf(path, sizeof(path), "%s/%s", upload, blit_glyph_part_names[p]);
        spill(path, data, length, 0600);
        up.present[p] = 1;
        up.lengths[p] = length;
        blit_sha256_hex(data, length, up.sha256[p]);
    }
    snprintf(gate_swap_target, sizeof(gate_swap_target), "%s/font", upload);
    snprintf(gate_swap_with, sizeof(gate_swap_with), "%s", other_font);
    ok = ok && blit_glyph_store_import("x-gate", upload, &up, &r, &allocated);
    if (!ok) fprintf(stderr, "GLYPH_SIGNATURE_FAIL import: %d %s %s\n", r.status, r.code ? r.code : "", r.detail);
    check(ok && gate_swaps == 1, "the import passes with the staged font replaced after its hash");
    stored = slurp((snprintf(path, sizeof(path), "%s/system/x-gate/font", store), path), &stored_length);
    check(stored && stored_length == font_length && memcmp(stored, font_bytes, font_length) == 0,
          "the stored font is the hashed bytes, not the replacement");
    set = blit_glyph_store_find("system", "x-gate", 6);
    check(set && set->font_length == font_length && memcmp(set->font, font_bytes, font_length) == 0,
          "FreeType's face holds the hashed bytes");
    if (set && !FT_New_Memory_Face(blit_glyph_store.library, font_bytes, (FT_Long)font_length, 0, &original)) {
        check(set->face->num_glyphs == original->num_glyphs, "the face is the hashed font's (its glyph count)");
        FT_Done_Face(original);
    }
    check(allocated > 0 && blit_glyph_store.remap_count == allocated,
          "every glyph has its ledger codepoint in the remap table");
    free(font_bytes);
    free(names_bytes);
    free(other);
    free(stored);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: glyph_signature_gate WORK FONT OTHER NAMES\n");
        return 2;
    }
    work = argv[1];
    keys();
    grammar();
    signatures();
    swapped(argv[2], argv[3], argv[4]);
    if (failures) {
        fprintf(stderr, "GLYPH_SIGNATURE_UNIT_FAIL %d of %d\n", failures, cases);
        return 1;
    }
    printf("GLYPH_SIGNATURE_UNIT_PASS cases=%d\n", cases);
    return 0;
}
