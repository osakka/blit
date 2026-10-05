/* PTY input sink for the deterministic Blit keyboard/mouse gate. */
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct termios saved;
    struct termios raw;
    unsigned char byte;
    int mouse = argc > 1 && strcmp(argv[1], "--mouse") == 0;
    if (tcgetattr(STDIN_FILENO, &saved) == 0) {
        raw = saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    /* Any-event reporting plus SGR encoding makes pointer bytes unambiguous. */
    /* Exercise the legacy VT application-keypad selector as well as the
       modern private mouse modes. */
    fputs("\033=", stdout);
    if (mouse)
        fputs("\033[?1003h\033[?1006h", stdout);
    fputs("READY\r\n", stdout);
    fflush(stdout);
    while (read(STDIN_FILENO, &byte, 1) == 1) {
        printf("BYTE %02X\r\n", (unsigned int)byte);
        fflush(stdout);
    }
    if (tcgetattr(STDIN_FILENO, &saved) == 0)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return 0;
}
