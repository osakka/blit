#ifndef BLIT_IME_ROOT_H
#define BLIT_IME_ROOT_H

/* ADR 0233: where hibr-ime's method data is read from, for blit-server and
   blit-compositor alike: <root>/<method>/. The hibr-ime-<method> packages
   install it under /usr/share/hibr-ime; BLIT_IME_DATA points elsewhere (a
   gate, a checkout's data). */

#include <stdlib.h>

#define BLIT_IME_DATA_DEFAULT "/usr/share/hibr-ime"

static const char *blit_ime_root(void)
{
    const char *root = getenv("BLIT_IME_DATA");
    return root && root[0] ? root : BLIT_IME_DATA_DEFAULT;
}

#endif
