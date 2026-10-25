/* The console watchdog kills a main loop that stops answering (blit #146).
   Before it does, it must say where the loop was: the step the server last
   named with blit_vt_phase, and what each thread waits in. Exercised on a
   live process standing in for a stuck server; no VT needed. */
#include "../../src/blit_vt.c"
#include <pthread.h>
#include <stdlib.h>

static void *idle(void *argument) { (void)argument; for (;;) pause(); return NULL; }

int main(void)
{
    char output[16384];
    int report[2];
    size_t length = 0;
    ssize_t count;
    pthread_t thread;
    pid_t child;
    mark = mmap(NULL, sizeof(*mark), PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mark == MAP_FAILED || pipe(report) < 0 ||
        pthread_create(&thread, NULL, idle, NULL) != 0) return 2;
    blit_vt_phase("render: compose and present outputs");
    child = fork();
    if (child == 0) {
        dup2(report[1], 2);
        usleep(200000);
        report_stuck(getppid());
        _exit(0);
    }
    close(report[1]);
    while ((count = read(report[0], output + length,
                         sizeof(output) - 1 - length)) > 0)
        length += (size_t)count;
    output[length] = '\0';
    waitpid(child, NULL, 0);
    if (!strstr(output, "main loop in \"render: compose and present outputs\" for ") ||
        !strstr(output, " wchan: ") || !strstr(output, " comm: ")) {
        fprintf(stderr, "VT_WATCHDOG_REPORT_FAIL\n%s", output);
        return 1;
    }
    /* The main thread and the idle one: every thread is reported. */
    {
        const char *at = output;
        int threads = 0;
        while ((at = strstr(at, " comm: ")) != NULL) { ++threads; ++at; }
        if (threads < 2) {
            fprintf(stderr, "VT_WATCHDOG_REPORT_FAIL threads=%d\n%s", threads, output);
            return 1;
        }
        printf("VT_WATCHDOG_REPORT_PASS phase=named threads=%d wchan=reported\n", threads);
    }
    return 0;
}
