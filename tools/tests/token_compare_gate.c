/* Security review SR-11 (Gitea #193): every secret Blit is sent -- the API
   bearer token, the update token, a session's frame and Hibr owner tokens,
   the compositor's activation tokens -- is compared by blit_token_equal
   (src/blit_token.h), whose time does not depend on where a guess first
   differs. This pins what it answers; token_compare_gate.sh also checks that
   no source compares a token with strcmp. */
#include <stdio.h>
#include <string.h>
#include "blit_token.h"

static int failures;

static void expect(const char *given, const char *secret, int equal)
{
    if (blit_token_equal(given, secret) != equal) {
        fprintf(stderr, "TOKEN_COMPARE_FAIL \"%s\" vs \"%s\": expected %s\n",
                given, secret, equal ? "equal" : "different");
        ++failures;
    }
}

int main(void)
{
    const char *secret = "0123456789abcdef0123456789abcdef";

    expect(secret, secret, 1);
    expect("0123456789abcdef0123456789abcdef", secret, 1);
    expect("x123456789abcdef0123456789abcdef", secret, 0);  /* first byte */
    expect("0123456789abcdef0123456789abcdex", secret, 0);  /* last byte */
    expect("0123456789abcdef0123456789abcde", secret, 0);   /* one short */
    expect("0123456789abcdef0123456789abcdef0", secret, 0); /* one long */
    expect("", secret, 0);
    expect(secret, "", 0);
    expect("", "", 1);
    expect("a", "b", 0);
    if (failures) return 1;
    puts("TOKEN_COMPARE_PASS cases=10");
    return 0;
}
