#define _GNU_SOURCE 1
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int commands, fail_timer, fail_kill, fail_restart, restarts;
static int fixture_command(const char *path, char *const argv[], char *detail, size_t capacity, int timeout)
{
    struct stat st;
    (void)timeout; ++commands;
    if (strstr(path,"systemd-run")) {
        if (lstat(BLIT_TRIAL_DROPIN_DIR "/90-blit-presentation-trial.conf",&st) == 0) abort();
        if (fail_timer) { snprintf(detail,capacity,"injected timer failure"); return 0; }
    }
    if (!strcmp(argv[1],"kill") && fail_kill) { snprintf(detail,capacity,"injected kill failure"); return 0; }
    if (!strcmp(argv[1],"show")) { snprintf(detail,capacity,"active"); return 1; }
    if (!strcmp(argv[1],"restart")) {
        ++restarts;
        if (fail_restart) { snprintf(detail,capacity,"injected restart failure"); return 0; }
    }
    detail[0] = 0; return 1;
}
#define BLIT_TRIAL_RUN fixture_command
#include "../../src/blit_presentation_trial.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"TRIAL_FAIL line=%d %s\n",__LINE__,#test); return 1; } } while(0)
int main(void)
{
    const char document[] = "{\"version\":1,\"server\":{\"display_backend\":\"sdl\"},\"login\":{\"enabled\":false}}";
    BlitTrialState state, observed;
    char detail[384] = "", first_id[33], second_id[33], saved[1024], path[512];
    struct stat st;
    int count, before, fd;
    CHECK(trial_state_read(&state) && !state.active && !strcmp(state.phase,"idle"));
    CHECK(!trial_prepare(document,strlen(document),"software",29,&state,detail,sizeof(detail)));
    CHECK(!trial_prepare(document,strlen(document),"arbitrary",60,&state,detail,sizeof(detail)));
    fail_timer = 1;
    CHECK(!trial_prepare(document,strlen(document),"software",60,&state,detail,sizeof(detail)));
    CHECK(trial_state_read(&observed) && !observed.active);
    CHECK(lstat(TRIAL_DROPIN,&st) < 0 && errno == ENOENT);
    fail_timer = 0;
    CHECK(trial_prepare(document,strlen(document),"software",60,&state,detail,sizeof(detail)));
    snprintf(first_id,sizeof(first_id),"%s",state.id);
    CHECK(state.active && blit_trial_id_valid(first_id));
    CHECK(lstat(TRIAL_DROPIN,&st) == 0 && S_ISREG(st.st_mode) && !(st.st_mode & 0022));
    CHECK(!trial_prepare(document,strlen(document),"opengles2",60,&observed,detail,sizeof(detail)));
    CHECK(trial_activate(first_id,detail,sizeof(detail)) && restarts == 1);
    CHECK(trial_state_read(&observed) && !strcmp(observed.phase,"running"));
    before = commands;
    CHECK(trial_restore("00000000000000000000000000000000",detail,sizeof(detail)) && commands == before);
    CHECK(trial_restore(first_id,detail,sizeof(detail)));
    CHECK(trial_state_read(&observed) && !observed.active && !strcmp(observed.phase,"restored"));
    CHECK(lstat(TRIAL_DROPIN,&st) < 0 && errno == ENOENT);
    snprintf(path,sizeof(path),BLIT_TRIAL_ROOT "/%s.json",first_id);
    CHECK(lstat(path,&st) < 0 && errno == ENOENT);
    CHECK(trial_prepare(document,strlen(document),"software",60,&state,detail,sizeof(detail)));
    fail_kill = 1; before = restarts;
    CHECK(!trial_restore(state.id,detail,sizeof(detail)) && restarts == before);
    CHECK(trial_state_read(&observed) && observed.active && !strcmp(observed.phase,"failed"));
    fail_kill = 0; fail_restart = 1;
    CHECK(!trial_restore(state.id,detail,sizeof(detail)));
    CHECK(trial_state_read(&observed) && observed.active && !strcmp(observed.phase,"failed"));
    fail_restart = 0;
    CHECK(trial_restore(state.id,detail,sizeof(detail)));
    CHECK(trial_prepare(document,strlen(document),"opengles2",60,&state,detail,sizeof(detail)));
    snprintf(second_id,sizeof(second_id),"%s",state.id);
    before = commands;
    CHECK(trial_restore(first_id,detail,sizeof(detail)) && commands == before);
    CHECK(trial_state_read(&observed) && observed.active && !strcmp(observed.id,second_id));
    fd = open(TRIAL_DROPIN,O_RDONLY); CHECK(fd >= 0);
    count = read(fd,saved,sizeof(saved)); close(fd); CHECK(count > 0);
    CHECK(trial_bytes(TRIAL_DROPIN,"# foreign override\n",19,0));
    CHECK(!trial_restore(second_id,detail,sizeof(detail)));
    CHECK(trial_state_read(&observed) && observed.active && !strcmp(observed.phase,"failed"));
    CHECK(trial_bytes(TRIAL_DROPIN,saved,(size_t)count,0));
    CHECK(trial_restore(second_id,detail,sizeof(detail)));
    CHECK(symlink("foreign-target",TRIAL_DROPIN) == 0);
    before = commands;
    CHECK(!trial_prepare(document,strlen(document),"software",60,&state,detail,sizeof(detail)) && commands == before);
    CHECK(lstat(TRIAL_DROPIN,&st) == 0 && S_ISLNK(st.st_mode));
    unlink(TRIAL_DROPIN);
    CHECK(trial_state_read(&observed));
    puts("PRESENTATION_TRIAL_GATE_PASS timer_before_override=1 invalid=2 active_conflict=1 stale=2 restore_without_frontend=1 cleanup=exact foreign_preserved=1 symlink_preserved=1 kill_and_restart_failures=retained scope=mock_systemd");
    return 0;
}
