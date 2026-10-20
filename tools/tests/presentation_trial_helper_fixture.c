#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
static int fixture_manager(const char *, char *const [], char *, size_t, int);
#define BLIT_TRIAL_RUN fixture_manager
#define main helper_entry_for_trial_fixture
#include "../../src/blit_update_helper.c"
#undef main
static int fixture_manager(const char *path, char *const args[], char *detail, size_t capacity, int timeout)
{
    int fd = open(BLIT_TRIAL_ROOT "/commands",O_WRONLY|O_CREAT|O_APPEND,0600), i;
    (void)timeout;
    if (fd >= 0) {
        dprintf(fd,"%s",path);
        for (i=1;args[i];++i) dprintf(fd," %s",args[i]);
        dprintf(fd,"\n"); close(fd);
    }
    if (!strcmp(args[1],"kill")) {
        FILE *file=fopen(BLIT_TRIAL_ROOT "/frontend.pid","r");
        long pid=0;
        if (file) { if (fscanf(file,"%ld",&pid)==1 && pid>1) kill((pid_t)pid,SIGKILL); fclose(file); }
    }
    if (!strcmp(args[1],"restart")) {
        BlitTrialState state;
        if (access(TRIAL_DROPIN,F_OK)==0 && trial_state_read(&state) && state.active) {
            char path[512], bytes[65536]; ssize_t count;
            snprintf(path,sizeof(path),BLIT_TRIAL_ROOT "/%s.json",state.id);
            fd=open(path,O_RDONLY);
            if (fd>=0) {
                count=read(fd,bytes,sizeof(bytes)); close(fd);
                if (count>0) {
                    fd=open(BLIT_TRIAL_ROOT "/received-snapshot.json",O_WRONLY|O_CREAT|O_TRUNC,0600);
                    if (fd>=0) { write(fd,bytes,(size_t)count); close(fd); }
                }
            }
        }
        fd=open(BLIT_TRIAL_ROOT "/normal-restart-queued",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if (fd>=0) { write(fd,"queued",6); close(fd); }
    }
    detail[0]=0; (void)capacity; return 1;
}
int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--bounded-command-test")) {
        char detail[384]; struct timespec before,after;
        clock_gettime(CLOCK_MONOTONIC,&before);
        if (run_command_bounded("/usr/bin/sleep",(char *const[]){"sleep","3",NULL},detail,sizeof(detail),1)) return 1;
        clock_gettime(CLOCK_MONOTONIC,&after);
        if (after.tv_sec-before.tv_sec>2 || strcmp(detail,"command timed out")) return 1;
        if (waitpid(-1,NULL,WNOHANG)!=-1 || errno!=ECHILD) return 1;
        puts("HELPER_COMMAND_TIMEOUT_PASS userspace_child=killed_and_reaped"); return 0;
    }
    return helper_entry_for_trial_fixture(argc,argv);
}
