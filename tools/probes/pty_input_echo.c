/* PTY input sink for the deterministic Blit keyboard/mouse gate. */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
    struct termios saved;
    struct termios raw;
    unsigned char byte;
    if (tcgetattr(STDIN_FILENO, &saved) == 0) {
        raw = saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    /* Any-event reporting plus SGR encoding makes pointer bytes unambiguous. */
    /* Exercise the legacy VT application-keypad selector as well as the
       modern private mouse modes. */
    fputs("\033=\033[?1003h\033[?1006hREADY\r\n", stdout);
    fflush(stdout);
    while (read(STDIN_FILENO, &byte, 1) == 1) {
        printf("BYTE %02X\r\n", (unsigned int)byte);
        fflush(stdout);
    }
    if (tcgetattr(STDIN_FILENO, &saved) == 0)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return 0;
}
