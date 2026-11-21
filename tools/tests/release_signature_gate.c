/* tools/tests/release_signature_gate.c: the release signature's verifier
   (security review SR-2), as the update helper uses it --
   src/blit_release_signature.h over Monocypher's crypto_ed25519_*. Built
   and run by release_signature_gate.sh with WORK, an empty scratch
   directory. Prints RELEASE_SIGNATURE_UNIT_PASS cases=N. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/blit_release_signature.h"

static int cases;
static int failures;

static void check(int condition, const char *what)
{
    ++cases;
    if (!condition) {
        ++failures;
        fprintf(stderr, "RELEASE_SIGNATURE_FAIL %s\n", what);
    }
}

static void unhex(const char *hex, uint8_t *out)
{
    size_t i, n = strlen(hex) / 2;
    for (i = 0; i < n; ++i) {
        unsigned value;
        sscanf(hex + 2 * i, "%2x", &value);
        out[i] = (uint8_t)value;
    }
}

/* RFC 8032, 7.1: Ed25519 TEST 1, 2, 3 and SHA(abc). */
static const struct {
    const char *name, *secret, *public_key, *message, *signature;
} vectors[] = {
    {"TEST 1",
     "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
     "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
     "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
    {"TEST 2",
     "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
     "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
     "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
    {"TEST 3",
     "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
     "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
     "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    {"TEST SHA(abc)",
     "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
     "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
     "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
     "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b589"
     "09351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"},
};

/* L, the group order, little-endian: S + L is the same signature made
   malleable, which Ed25519 (RFC 8032 5.1.7) and Monocypher refuse. */
static const uint8_t group_order[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2,
    0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

static void rfc_vectors(void)
{
    size_t v;
    for (v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
        uint8_t seed[32], secret[64], public_key[32], expected_public[32];
        uint8_t message[64], signature[64], expected[64], bad[64];
        size_t length = strlen(vectors[v].message) / 2;
        char what[128];
        int i, carry;
        unhex(vectors[v].secret, seed);
        unhex(vectors[v].public_key, expected_public);
        unhex(vectors[v].message, message);
        unhex(vectors[v].signature, expected);
        crypto_ed25519_key_pair(secret, public_key, seed);
        snprintf(what, sizeof(what), "%s: public key", vectors[v].name);
        check(memcmp(public_key, expected_public, 32) == 0, what);
        crypto_ed25519_sign(signature, secret, message, length);
        snprintf(what, sizeof(what), "%s: signature", vectors[v].name);
        check(memcmp(signature, expected, 64) == 0, what);
        snprintf(what, sizeof(what), "%s: verifies", vectors[v].name);
        check(crypto_ed25519_check(expected, expected_public, message, length) == 0, what);
        /* A flipped bit anywhere in the signature: R's first and last
           byte, S's first and last. */
        for (i = 0; i < 4; ++i) {
            static const int at[4] = {0, 31, 32, 63};
            memcpy(bad, expected, 64);
            bad[at[i]] ^= 0x01;
            snprintf(what, sizeof(what), "%s: flipped bit in byte %d refused",
                     vectors[v].name, at[i]);
            check(crypto_ed25519_check(bad, expected_public, message, length) != 0, what);
        }
        if (length > 0) {
            message[length - 1] ^= 0x80;
            snprintf(what, sizeof(what), "%s: flipped message bit refused", vectors[v].name);
            check(crypto_ed25519_check(expected, expected_public, message, length) != 0, what);
            message[length - 1] ^= 0x80;
            snprintf(what, sizeof(what), "%s: truncated message refused", vectors[v].name);
            check(crypto_ed25519_check(expected, expected_public, message, length - 1) != 0, what);
        }
        /* Another vector's key. */
        unhex(vectors[(v + 1) % 4].public_key, public_key);
        snprintf(what, sizeof(what), "%s: another key refused", vectors[v].name);
        check(crypto_ed25519_check(expected, public_key, message, length) != 0, what);
        /* S + L. */
        memcpy(bad, expected, 64);
        for (i = 0, carry = 0; i < 32; ++i) {
            int sum = bad[32 + i] + group_order[i] + carry;
            bad[32 + i] = (uint8_t)sum;
            carry = sum >> 8;
        }
        snprintf(what, sizeof(what), "%s: S + L (malleated) refused", vectors[v].name);
        check(crypto_ed25519_check(bad, expected_public, message, length) != 0, what);
    }
}

static const char *work;

static void write_file(const char *name, const char *text, int mode)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", work, name);
    unlink(path);
    f = fopen(path, "w");
    if (f == NULL) { perror(path); exit(1); }
    fputs(text, f);
    fclose(f);
    chmod(path, (mode_t)mode);
}

static void key_line(const char *tag, const uint8_t key[32], char *out)
{
    char hex[65];
    blit_release_hex_write(key, 32, hex);
    sprintf(out, "%s%s\n", tag, hex);
}

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"

/* VERIFIES: does SIGNATURE verify the statement for these packages under
   the keys in DIRECTORY (strict, as the helper reads them)? */
static int verifies(const char *directory, const uint8_t signature[64],
                    const char *bv, const char *bs, const char *cv, const char *cs,
                    char *matched, int *keys)
{
    char statement[BLIT_RELEASE_STATEMENT_CAPACITY];
    char path[512];
    size_t length = blit_release_statement(statement, sizeof(statement), bv, bs, cv, cs);
    if (length == 0) return 0;
    snprintf(path, sizeof(path), "%s/%s", work, directory);
    return blit_release_verify_directory(path, 1, signature, statement, length,
                                         matched, 256, keys);
}

static void statements_and_keys(void)
{
    uint8_t seed1[32], seed2[32], secret1[64], secret2[64], public1[32], public2[32];
    uint8_t signature[64], other[64];
    char statement[BLIT_RELEASE_STATEMENT_CAPACITY], line[128], matched[256];
    char hex[BLIT_RELEASE_SIGNATURE_HEX + 2], path[512], target[512];
    size_t length;
    int keys = 0;

    memset(seed1, 0x11, 32);
    memset(seed2, 0x22, 32);
    crypto_ed25519_key_pair(secret1, public1, seed1);
    crypto_ed25519_key_pair(secret2, public2, seed2);

    /* The statement, byte for byte. */
    length = blit_release_statement(statement, sizeof(statement),
                                    "0.3.133-1", SHA_A, "0.3.133-1", SHA_B);
    check(length > 0 && strcmp(statement,
        "blit-release-signature 1\n"
        "blit 0.3.133-1 " SHA_A "\n"
        "blit-compositor 0.3.133-1 " SHA_B "\n") == 0, "the pair's statement");
    check(blit_release_statement(statement, sizeof(statement), "0.3.133-1",
                                 SHA_A, NULL, NULL) > 0 &&
          strcmp(statement, "blit-release-signature 1\nblit 0.3.133-1 " SHA_A "\n") == 0,
          "blit alone's statement");
    check(blit_release_statement(statement, sizeof(statement), "0.3.133-1 x",
                                 SHA_A, NULL, NULL) == 0, "a version with a space refused");
    check(blit_release_statement(statement, sizeof(statement), "0.3.133-1",
                                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                 NULL, NULL) == 0, "an upper-case hash refused");
    check(blit_release_statement(statement, sizeof(statement), "0.3.133-1",
                                 SHA_A "a", NULL, NULL) == 0, "a 65-digit hash refused");

    /* The release's signature over the pair, under key 1. */
    length = blit_release_statement(statement, sizeof(statement),
                                    "0.3.133-1", SHA_A, "0.3.133-1", SHA_B);
    crypto_ed25519_sign(signature, secret1, (const uint8_t *)statement, length);

    snprintf(path, sizeof(path), "%s/one", work); mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/two", work); mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/both", work); mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/none", work); mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/odd", work); mkdir(path, 0755);
    key_line(BLIT_RELEASE_PUBLIC_TAG, public1, line);
    write_file("one/blit-release-1.pub", line, 0644);
    write_file("both/blit-release-1.pub", line, 0644);
    key_line(BLIT_RELEASE_PUBLIC_TAG, public2, line);
    write_file("two/blit-release-2.pub", line, 0644);
    write_file("both/blit-release-2.pub", line, 0644);

    check(verifies("one", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                   matched, &keys) && keys == 1 &&
          strcmp(matched, "blit-release-1.pub") == 0, "signed: verifies under its key");
    check(!verifies("two", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys) && keys == 1, "another key's directory: refused");
    check(verifies("both", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                   matched, &keys) && strcmp(matched, "blit-release-1.pub") == 0,
          "rotation: key 1 still verifies beside key 2");
    length = blit_release_statement(statement, sizeof(statement),
                                    "0.3.134-1", SHA_B, "0.3.134-1", SHA_A);
    crypto_ed25519_sign(other, secret2, (const uint8_t *)statement, length);
    check(verifies("both", other, "0.3.134-1", SHA_B, "0.3.134-1", SHA_A,
                   matched, &keys) &&
          strcmp(matched, "blit-release-2.pub") == 0,
          "rotation: a release signed with key 2 verifies beside key 1");
    check(!verifies("one", other, "0.3.134-1", SHA_B, "0.3.134-1", SHA_A,
                    matched, &keys), "key 2's release refused where only key 1 is");

    /* The same signature, another statement. */
    check(!verifies("one", signature, "0.3.134-1", SHA_A, "0.3.134-1", SHA_B,
                    matched, &keys), "another version: refused");
    check(!verifies("one", signature, "0.3.132-1", SHA_A, "0.3.132-1", SHA_B,
                    matched, &keys), "an older version: refused");
    check(!verifies("one", signature, "0.3.133-1", SHA_B, "0.3.133-1", SHA_B,
                    matched, &keys), "another blit hash: refused");
    check(!verifies("one", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_A,
                    matched, &keys), "another compositor hash: refused");
    check(!verifies("one", signature, "0.3.133-1", SHA_A, NULL, NULL,
                    matched, &keys), "the pair's signature for blit alone: refused");
    check(!verifies("one", signature, "0.3.133-1", SHA_B, "0.3.133-1", SHA_A,
                    matched, &keys), "the packages swapped: refused");

    /* The signature as it travels. */
    blit_release_hex_write(signature, 64, hex);
    check(blit_release_signature_parse(hex, other) && memcmp(other, signature, 64) == 0,
          "the hex signature parses");
    hex[127] = '\0';
    check(!blit_release_signature_parse(hex, other), "a truncated signature (127 digits) refused");
    hex[126] = '\0';
    check(!blit_release_signature_parse(hex, other), "a truncated signature (126 digits) refused");
    blit_release_hex_write(signature, 64, hex);
    hex[128] = '0'; hex[129] = '\0';
    check(!blit_release_signature_parse(hex, other), "a 129-digit signature refused");
    blit_release_hex_write(signature, 64, hex);
    hex[5] = hex[5] >= 'a' ? (char)(hex[5] - 'a' + 'A') : 'G';
    check(!blit_release_signature_parse(hex, other), "a signature not lower-case hex refused");

    /* What a key directory may hold. */
    check(!verifies("none", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys) && keys == 0, "no key: refused, and said so");
    write_file("odd/README.pub", "# not yet generated: run tools/release/keygen\n", 0644);
    key_line(BLIT_RELEASE_SECRET_TAG, secret1, line);   /* the seed */
    write_file("odd/secret.pub", line, 0644);
    key_line(BLIT_RELEASE_PUBLIC_TAG, public1, line);
    write_file("odd/blit-release-1.key", line, 0644);
    snprintf(target, sizeof(target), "%s/one/blit-release-1.pub", work);
    snprintf(path, sizeof(path), "%s/odd/link.pub", work);
    symlink(target, path);
    check(!verifies("odd", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys) && keys == 0,
          "a placeholder, a secret key, a file not named .pub and a link are no keys");
    key_line(BLIT_RELEASE_PUBLIC_TAG, public1, line);
    write_file("odd/writable.pub", line, 0664);
    check(!verifies("odd", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys) && keys == 0,
          "strict: a key file others may write is no key");
    write_file("odd/writable.pub", line, 0644);
    check(verifies("odd", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                   matched, &keys) && keys == 1, "the same key, 0644: verifies");
    line[strlen(line) - 1] = ' ';
    write_file("odd/writable.pub", line, 0644);
    check(!verifies("odd", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys) && keys == 0, "a key line not ending in LF is no key");
    snprintf(path, sizeof(path), "%s/one", work);
    chmod(path, 0775);
    check(!verifies("one", signature, "0.3.133-1", SHA_A, "0.3.133-1", SHA_B,
                    matched, &keys), "strict: a key directory others may write is refused");
    chmod(path, 0755);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: release_signature_gate WORK\n");
        return 2;
    }
    work = argv[1];
    umask(022);
    rfc_vectors();
    statements_and_keys();
    if (failures != 0) {
        fprintf(stderr, "RELEASE_SIGNATURE_FAIL %d of %d cases\n", failures, cases);
        return 1;
    }
    printf("RELEASE_SIGNATURE_UNIT_PASS cases=%d\n", cases);
    return 0;
}
