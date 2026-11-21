/* tools/release/blit-sign: Blit's release keys and signatures (security
   review SR-2, Gitea #184). A release tool, built by
   `tools/build-tcc.sh build/blit/blit-sign sign`; never packaged. It shares
   the statement, the key format and Ed25519 (Monocypher) with the update
   helper's verifier: src/blit_release_signature.h.

     blit-sign keygen SECRET PUBLIC
         a new key: the secret (the 32-byte seed) to SECRET, mode 0600, and
         the public half to PUBLIC. Refuses to overwrite either.
     blit-sign public SECRET
         the public key line of SECRET.
     blit-sign sign SECRET BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]
         the signature, 128 hex digits, of the statement for those packages.
         SECRET must be a regular file of the caller's that no one else can
         read or write (mode 0600).
     blit-sign verify KEYDIR SIGNATURE BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]
         exit 0, printing the key file that verifies it, or 1.
     blit-sign statement BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]
         the statement itself, as signed.

   Versions are the packages' Version fields (0.3.133-1), hashes their
   SHA-256: tools/release/sign-release.sh reads both from the .deb files. */
#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/blit_release_signature.h"

static int usage(void)
{
    fprintf(stderr,
            "usage: blit-sign keygen SECRET PUBLIC\n"
            "       blit-sign public SECRET\n"
            "       blit-sign sign SECRET BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]\n"
            "       blit-sign verify KEYDIR SIGNATURE BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]\n"
            "       blit-sign statement BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]\n");
    return 2;
}

/* The statement from argv: BLIT_VERSION BLIT_SHA256 [COMP_VERSION COMP_SHA256]. */
static size_t statement_from(char **argv, int argc, char *statement)
{
    size_t length;
    if (argc != 2 && argc != 4) return 0;
    length = blit_release_statement(statement, BLIT_RELEASE_STATEMENT_CAPACITY,
                                    argv[0], argv[1],
                                    argc == 4 ? argv[2] : NULL,
                                    argc == 4 ? argv[3] : NULL);
    if (length == 0)
        fprintf(stderr, "blit-sign: a version or SHA-256 is not one a statement carries\n");
    return length;
}

/* The secret key: the caller's own regular file, mode 0600 or tighter.
   The 64-byte Monocypher secret (seed and public half) into SECRET. */
static int secret_key(const char *path, uint8_t secret[64], uint8_t public_key[32])
{
    struct stat status;
    uint8_t seed[32];
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "blit-sign: cannot open the secret key %s: %s\n", path,
                strerror(errno));
        return 0;
    }
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 077) != 0) {
        close(fd);
        fprintf(stderr, "blit-sign: the secret key %s must be a regular file of "
                "yours that no one else can read or write (chmod 0600)\n", path);
        return 0;
    }
    close(fd);
    if (!blit_release_key_file(-1, path, BLIT_RELEASE_SECRET_TAG, 0, seed)) {
        fprintf(stderr, "blit-sign: %s is not a Blit release secret key\n", path);
        return 0;
    }
    crypto_ed25519_key_pair(secret, public_key, seed);   /* wipes the seed */
    return 1;
}

/* KEYLINE: TAG, the key in hex, LF. */
static int write_new(const char *path, int mode, const char *tag, const uint8_t key[32])
{
    char line[128];
    int length, fd;
    char hex[65];
    blit_release_hex_write(key, 32, hex);
    length = snprintf(line, sizeof(line), "%s%s\n", tag, hex);
    crypto_wipe(hex, sizeof(hex));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) {
        fprintf(stderr, "blit-sign: cannot create %s: %s\n", path, strerror(errno));
        crypto_wipe(line, sizeof(line));
        return 0;
    }
    if (fchmod(fd, (mode_t)mode) < 0 || write(fd, line, (size_t)length) != length ||
        fsync(fd) < 0) {
        fprintf(stderr, "blit-sign: cannot write %s: %s\n", path, strerror(errno));
        close(fd);
        unlink(path);
        crypto_wipe(line, sizeof(line));
        return 0;
    }
    crypto_wipe(line, sizeof(line));
    return close(fd) == 0;
}

static int keygen(const char *secret_path, const char *public_path)
{
    uint8_t seed[32], secret[64], public_key[32];
    size_t got = 0;
    struct stat status;
    char hex[65];
    if (lstat(secret_path, &status) == 0 || lstat(public_path, &status) == 0) {
        fprintf(stderr, "blit-sign: %s or %s already exists; a key is never "
                "overwritten\n", secret_path, public_path);
        return 1;
    }
    while (got < sizeof(seed)) {
        ssize_t count = getrandom(seed + got, sizeof(seed) - got, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            fprintf(stderr, "blit-sign: getrandom failed: %s\n", strerror(errno));
            return 1;
        }
        got += (size_t)count;
    }
    crypto_ed25519_key_pair(secret, public_key, seed);   /* wipes the seed */
    if (!write_new(secret_path, 0600, BLIT_RELEASE_SECRET_TAG, secret)) {
        crypto_wipe(secret, sizeof(secret));
        return 1;
    }
    crypto_wipe(secret, sizeof(secret));
    if (!write_new(public_path, 0644, BLIT_RELEASE_PUBLIC_TAG, public_key)) {
        unlink(secret_path);
        return 1;
    }
    blit_release_hex_write(public_key, 32, hex);
    printf("%s%s\n", BLIT_RELEASE_PUBLIC_TAG, hex);
    return 0;
}

int main(int argc, char **argv)
{
    char statement[BLIT_RELEASE_STATEMENT_CAPACITY];
    uint8_t secret[64], public_key[32], signature[64];
    char hex[BLIT_RELEASE_SIGNATURE_HEX + 1];
    size_t length;

    if (argc < 2) return usage();
    if (strcmp(argv[1], "keygen") == 0) {
        if (argc != 4) return usage();
        return keygen(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "public") == 0) {
        if (argc != 3) return usage();
        if (!secret_key(argv[2], secret, public_key)) return 1;
        crypto_wipe(secret, sizeof(secret));
        blit_release_hex_write(public_key, 32, hex);
        printf("%s%s\n", BLIT_RELEASE_PUBLIC_TAG, hex);
        return 0;
    }
    if (strcmp(argv[1], "sign") == 0) {
        if (argc < 3) return usage();
        length = statement_from(argv + 3, argc - 3, statement);
        if (length == 0) return usage();
        if (!secret_key(argv[2], secret, public_key)) return 1;
        crypto_ed25519_sign(signature, secret, (const uint8_t *)statement, length);
        crypto_wipe(secret, sizeof(secret));
        /* Never hand out a signature that does not verify. */
        if (crypto_ed25519_check(signature, public_key, (const uint8_t *)statement,
                                 length) != 0) {
            fprintf(stderr, "blit-sign: the signature does not verify\n");
            return 1;
        }
        blit_release_hex_write(signature, 64, hex);
        printf("%s\n", hex);
        return 0;
    }
    if (strcmp(argv[1], "verify") == 0) {
        char matched[256];
        int keys = 0;
        if (argc < 4) return usage();
        length = statement_from(argv + 4, argc - 4, statement);
        if (length == 0) return usage();
        if (!blit_release_signature_parse(argv[3], signature)) {
            fprintf(stderr, "blit-sign: the signature is not 128 lowercase hex digits\n");
            return 1;
        }
        if (blit_release_verify_directory(argv[2], 0, signature, statement, length,
                                          matched, sizeof(matched), &keys)) {
            printf("%s\n", matched);
            return 0;
        }
        fprintf(stderr, keys == 0 ? "blit-sign: no release key in %s\n" :
                "blit-sign: no release key in %s verifies the signature\n", argv[2]);
        return 1;
    }
    if (strcmp(argv[1], "statement") == 0) {
        length = statement_from(argv + 2, argc - 2, statement);
        if (length == 0) return usage();
        fwrite(statement, 1, length, stdout);
        return 0;
    }
    return usage();
}
