#ifndef BLIT_TOKEN_H
#define BLIT_TOKEN_H

#include <string.h>

/* A secret compared with what a client sent (security review SR-11, Gitea
   #193): the API bearer token, the update token, a session's frame and Hibr
   owner tokens, the compositor's activation tokens. strcmp stops at the first
   byte that differs, so its time tells a client how much of a guess was
   right. Here every byte of the secret is read whatever was given, and the
   time depends only on the two lengths -- the secret's, which is not a
   secret worth more than the token, and the caller's own. Every secret is
   compared here, never with strcmp (tools/tests/token_compare_gate.sh). */
static inline int blit_token_equal(const char *given, const char *secret)
{
    size_t given_length = strlen(given);
    size_t secret_length = strlen(secret);
    size_t i;
    unsigned char diff = (unsigned char)(given_length != secret_length);

    for (i = 0; i < secret_length; ++i) {
        /* Past the end of a shorter guess its terminator stands in: the
           lengths already differ, and the loop still runs to the end. */
        unsigned char byte = (unsigned char)given[i < given_length ? i : given_length];
        diff |= (unsigned char)(byte ^ (unsigned char)secret[i]);
    }
    return diff == 0;
}

#endif
