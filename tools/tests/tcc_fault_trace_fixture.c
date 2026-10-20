/* Deliberate subprocess fault: verify the compiler's real diagnostic runtime. */
#include <string.h>
static int deliberate_fault(volatile int *address)
{
    return *address;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--fault"))
        return deliberate_fault((volatile int *)0);
    return 0;
}
