/* PTY query probe for terminal status replies. */
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(void)
{
    struct termios saved;
    struct termios raw;
    unsigned char byte;
    int i;
    if (tcgetattr(STDIN_FILENO, &saved) == 0) {
        raw = saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    fputs("\033[?25$p", stdout);
    fflush(stdout);
    for (i = 0; i < 9; ++i) {
        if (read(STDIN_FILENO, &byte, 1) != 1)
            return 1;
        printf("Q%02X%s", (unsigned int)byte, i == 8 ? "\r\n" : " ");
        fflush(stdout);
    }
    if (tcgetattr(STDIN_FILENO, &saved) == 0)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return 0;
}
