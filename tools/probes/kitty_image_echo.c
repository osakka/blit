/* Minimal Kitty graphics producer used only by deterministic parser gates. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    static const char transmit[] =
        "\033_Ga=t,f=24,s=1,v=1,i=7,m=1;/w\033\\"
        "\033_Ga=t,i=7,m=0;AA\033\\";
    static const char place[] = "\033_Ga=p,i=7;\033\\";
    static const char rejected[] = "\033_Ga=t,f=24,s=513,v=1,i=8;AAAA\033\\";
    static const char deleted[] = "\033_Ga=d,i=7;\033\\";
    int index;
    int limits = argc > 1 && strcmp(argv[1], "--limits") == 0;
    if (limits) {
        for (index = 1; index <= 8; ++index)
            printf("\033_Ga=t,f=24,s=1,v=1,i=%d,m=1;/w\033\\", index);
        fputs("\033_Ga=t,f=24,s=1,v=1,i=9,m=1;/w\033\\", stdout);
        fputs("\033_Ga=t,f=24,s=513,v=1,i=10;AAAA\033\\", stdout);
        fputs("LIMITS\r\n", stdout);
        fflush(stdout);
        sleep(10);
        return 0;
    }
    fputs(transmit, stdout);
    fputs(place, stdout);
    if (argc > 1 && strcmp(argv[1], "--delete") == 0)
        fputs(deleted, stdout);
    fputs(rejected, stdout);
    fputs("OK\r\n", stdout);
    fflush(stdout);
    sleep(10);
    return 0;
}
