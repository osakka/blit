/* A deliberate fault in a program built as Blit's are -- tcc -c -g, then
   tools/tcc-link.sh -- with Blit's crash handler (src/blit_crash.h): the
   fault is in a static function called from another, as most of Blit's
   functions are. */
#define _GNU_SOURCE 1
#include "../../src/blit_crash.h"
#include <string.h>
static int deliberate_fault(volatile int *address)
{
    return *address;
}
static int caller_of_the_fault(volatile int *address)
{
    return deliberate_fault(address) + 1;
}
int main(int argc, char **argv)
{
    blit_crash_install();
    if (argc == 2 && !strcmp(argv[1], "--fault"))
        return caller_of_the_fault((volatile int *)0);
    return 0;
}
