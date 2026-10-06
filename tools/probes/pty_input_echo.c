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
    int mouse = 0;
    int focus = 0;
    int wide = 0;
    int kitty = 0;
    int argument;
    for (argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--mouse") == 0) mouse = 1;
        if (strcmp(argv[argument], "--focus") == 0) focus = 1;
        if (strcmp(argv[argument], "--wide") == 0) wide = 1;
        if (strcmp(argv[argument], "--kitty") == 0) kitty = 1;
    }
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
    if (focus)
        fputs("\033[?1004h", stdout);
    if (kitty)
        fputs("\033_Ga=t,f=24,s=1,v=1,i=7;/wAA\033\\"
              "\033_Ga=p,i=7;\033\\", stdout);
    fputs("READY", stdout);
    if (wide)
        fputs("\342\224\200", stdout);
    fputs("\r\n", stdout);
    fflush(stdout);
    while (read(STDIN_FILENO, &byte, 1) == 1) {
        printf("BYTE %02X\r\n", (unsigned int)byte);
        fflush(stdout);
    }
    if (tcgetattr(STDIN_FILENO, &saved) == 0)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return 0;
}
