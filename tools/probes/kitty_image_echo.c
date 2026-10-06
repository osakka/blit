/* Minimal Kitty graphics producer used only by deterministic parser gates. */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    static const char transmit[] = "\033_Ga=t,f=24,s=1,v=1,i=7;/wAA\033\\";
    static const char place[] = "\033_Ga=p,i=7;\033\\";
    fputs(transmit, stdout);
    fputs(place, stdout);
    fputs("OK\r\n", stdout);
    fflush(stdout);
    sleep(10);
    return 0;
}
