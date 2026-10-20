/* Root-helper implementation only. Same helper executable performs timer
   recovery; no frontend process or canonical settings write is involved. */
#include "blit_trial_protocol.h"
#include <sys/file.h>
#include <stdint.h>
#include <time.h>

#ifndef BLIT_TRIAL_ROOT
#define BLIT_TRIAL_ROOT "/run/blit/presentation-trial"
#endif
#ifndef BLIT_TRIAL_DROPIN_DIR
#define BLIT_TRIAL_DROPIN_DIR "/run/systemd/system/blit.service.d"
#endif
#ifndef BLIT_TRIAL_HELPER
#define BLIT_TRIAL_HELPER "/usr/lib/blit/bin/blit-update-helper"
#endif
#ifndef BLIT_TRIAL_RUN
#define BLIT_TRIAL_RUN run_command_bounded
#endif
#define TRIAL_DROPIN BLIT_TRIAL_DROPIN_DIR "/90-blit-presentation-trial.conf"
#define TRIAL_MAGIC 0x42545231U
typedef struct {
    uint32_t magic;
    int active, seconds;
    long long expires;
    char id[33], phase[16], renderer[16], detail[384];
} BlitTrialState;

static int trial_directory(const char *path, mode_t mode)
{
    struct stat st;
    if (mkdir(path, mode) < 0 && errno != EEXIST) return 0;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) &&
        st.st_uid == geteuid() && !(st.st_mode & 0022);
}
static int trial_lock(void)
{
    int fd;
    struct stat st;
    if (!trial_directory(BLIT_TRIAL_ROOT, 0750)) return -1;
    fd = open(BLIT_TRIAL_ROOT "/lock", O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0077) || flock(fd, LOCK_EX) < 0) { close(fd); return -1; }
    return fd;
}
static int trial_bytes(const char *path, const void *bytes, size_t size, int exclusive)
{
    int fd;
    size_t used = 0;
    fd = open(path, O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC|
        (exclusive ? O_EXCL : O_TRUNC), 0640);
    if (fd < 0) return 0;
    while (used < size) {
        ssize_t count = write(fd, (const char *)bytes + used, size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    if (used != size || fsync(fd) < 0) {
        close(fd);
        if (exclusive) unlink(path); /* Only a file this call created. */
        return 0;
    }
    if (close(fd) < 0) { if (exclusive) unlink(path); return 0; }
    return 1;
}
static int trial_state_write(BlitTrialState *state)
{
    int fd;
    state->magic = TRIAL_MAGIC;
    if (!trial_bytes(BLIT_TRIAL_ROOT "/state.new", state, sizeof(*state), 0) ||
        rename(BLIT_TRIAL_ROOT "/state.new", BLIT_TRIAL_ROOT "/state") < 0) return 0;
    fd = open(BLIT_TRIAL_ROOT, O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (fd < 0) return 0;
    if (fsync(fd) < 0) { close(fd); return 0; }
    close(fd); return 1;
}
static int trial_override_create(const char *text)
{
    /* Publish a complete marker atomically, never a partial live override. */
    if (!trial_bytes(TRIAL_DROPIN ".new", text, strlen(text), 1)) return 0;
    if (link(TRIAL_DROPIN ".new", TRIAL_DROPIN) < 0) {
        unlink(TRIAL_DROPIN ".new"); return 0;
    }
    unlink(TRIAL_DROPIN ".new"); return 1;
}
static int trial_state_read(BlitTrialState *state)
{
    struct stat st;
    int fd;
    ssize_t count;
    memset(state, 0, sizeof(*state));
    snprintf(state->phase, sizeof(state->phase), "idle");
    fd = open(BLIT_TRIAL_ROOT "/state", O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd < 0) return errno == ENOENT;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0022) || st.st_size != sizeof(*state)) { close(fd); return 0; }
    count = read(fd, state, sizeof(*state)); close(fd);
    return count == sizeof(*state) && state->magic == TRIAL_MAGIC &&
        state->id[32] == 0 && blit_trial_id_valid(state->id) &&
        memchr(state->phase, 0, sizeof(state->phase)) &&
        memchr(state->renderer, 0, sizeof(state->renderer)) &&
        memchr(state->detail, 0, sizeof(state->detail)) &&
        (state->active == 0 || state->active == 1) &&
        state->seconds >= BLIT_TRIAL_MIN_SECONDS && state->seconds <= BLIT_TRIAL_MAX_SECONDS &&
        (!strcmp(state->renderer, "software") || !strcmp(state->renderer, "opengles2")) &&
        (!strcmp(state->phase, "armed") || !strcmp(state->phase, "running") ||
         !strcmp(state->phase, "restoring") || !strcmp(state->phase, "restored") || !strcmp(state->phase, "failed"));
}
static void trial_json(const BlitTrialState *state, char *out, size_t capacity)
{
    char id[40], renderer[24];
    if (state->id[0]) snprintf(id, sizeof(id), "\"%s\"", state->id);
    else snprintf(id, sizeof(id), "null");
    if (state->renderer[0]) snprintf(renderer, sizeof(renderer), "\"%s\"", state->renderer);
    else snprintf(renderer, sizeof(renderer), "null");
    snprintf(out, capacity, "{\"id\":%s,\"state\":\"%s\",\"active\":%s,"
        "\"renderer\":%s,\"timeout_seconds\":%d,\"expires_unix_seconds\":%lld,\"detail\":\"%s\"}",
        id, state->phase, state->active ? "true" : "false", renderer,
        state->seconds, state->expires, state->detail);
}
static int trial_manager(const char *verb, const char *unit, char *detail, size_t capacity)
{
    char *args[] = {"systemctl", (char *)verb, (char *)unit, NULL};
    if (!unit) args[2] = NULL;
    return BLIT_TRIAL_RUN("/usr/bin/systemctl", args, detail, capacity, 5);
}
static int trial_remove_override(const BlitTrialState *state)
{
    char expected[80], actual[2048];
    struct stat st;
    ssize_t count;
    int fd = open(TRIAL_DROPIN, O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd < 0) return errno == ENOENT;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0022)) { close(fd); return 0; }
    count = read(fd, actual, sizeof(actual) - 1); close(fd);
    if (count < 0) return 0;
    actual[count] = 0;
    snprintf(expected, sizeof(expected), "# blit-presentation-trial:%s\n", state->id);
    if (strncmp(actual, expected, strlen(expected)) != 0) return 0;
    return unlink(TRIAL_DROPIN) == 0;
}
static int trial_restore(const char *id, char *detail, size_t capacity)
{
    BlitTrialState state;
    char path[512], unit[128], command_detail[384];
    int lock, ok = 0;
    if (!blit_trial_id_valid(id) || (lock = trial_lock()) < 0) return 0;
    if (!trial_state_read(&state)) goto done;
    if (strcmp(state.id, id) != 0 || !state.active) { ok = 1; goto done; }
    snprintf(state.phase, sizeof(state.phase), "restoring");
    if (!trial_state_write(&state) || !trial_remove_override(&state)) {
        snprintf(state.detail, sizeof(state.detail), "cannot remove owned trial override"); goto failed;
    }
    if (!trial_manager("daemon-reload", NULL, state.detail, sizeof(state.detail))) goto failed;
    /* The trial may be stuck before the listener. Do not wait for that event
       loop or graceful signal handling to make progress. Target this unit only. */
    if (!BLIT_TRIAL_RUN("/usr/bin/systemctl", (char *const[]){"systemctl", "kill",
        "--kill-whom=all", "--signal=KILL", "blit.service", NULL}, state.detail, sizeof(state.detail), 5)) {
        /* An inactive service has no processes to kill. Any other failure is
           not evidence of recovery: retain the active failed state. */
        if (!BLIT_TRIAL_RUN("/usr/bin/systemctl", (char *const[]){"systemctl", "show",
            "--property=ActiveState", "--value", "blit.service", NULL}, command_detail, sizeof(command_detail), 5) ||
            (strcmp(command_detail, "inactive") && strcmp(command_detail, "failed"))) goto failed;
    }
    (void)trial_manager("reset-failed", "blit.service", command_detail, sizeof(command_detail));
    if (!BLIT_TRIAL_RUN("/usr/bin/systemctl", (char *const[]){"systemctl", "restart",
        "--no-block", "blit.service", NULL}, state.detail, sizeof(state.detail), 5)) goto failed;
    state.active = 0;
    snprintf(state.phase, sizeof(state.phase), "restored");
    snprintf(state.detail, sizeof(state.detail), "normal service restart queued; verify native readiness through the API");
    if (!trial_state_write(&state)) goto done;
    snprintf(unit, sizeof(unit), "blit-presentation-recovery-%s.timer", id);
    (void)BLIT_TRIAL_RUN("/usr/bin/systemctl", (char *const[]){"systemctl", "stop",
        "--no-block", unit, NULL}, command_detail, sizeof(command_detail), 5);
    snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.json", id); unlink(path);
    snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.env", id); unlink(path);
    ok = 1; goto done;
failed:
    snprintf(state.phase, sizeof(state.phase), "failed");
    (void)trial_state_write(&state);
done:
    if (!ok) snprintf(detail, capacity, "%s", state.detail[0] ? state.detail : "trial recovery failed");
    close(lock); return ok;
}
static int trial_prepare(const char *document, size_t length, const char *renderer,
                         int seconds, BlitTrialState *state, char *detail, size_t capacity)
{
    unsigned char random[16];
    char path[512], environment[1024], override[1024], timer[128], interval[64];
    int lock, random_fd, i, ok = 0, armed = 0;
    struct stat st;
    if (seconds < BLIT_TRIAL_MIN_SECONDS || seconds > BLIT_TRIAL_MAX_SECONDS ||
        (strcmp(renderer, "software") && strcmp(renderer, "opengles2")) ||
        length < 2 || length > BLIT_TRIAL_MAX_DOCUMENT || memchr(document, 0, length) ||
        document[0] != '{' || document[length - 1] != '}') {
        snprintf(detail, capacity, "invalid trial parameters"); return 0;
    }
    lock = trial_lock(); if (lock < 0) return 0;
    if (!trial_state_read(state) || state->active) {
        snprintf(detail, capacity, "trial already active or state unreadable"); goto done;
    }
    if (!trial_directory(BLIT_TRIAL_DROPIN_DIR, 0755) ||
        lstat(TRIAL_DROPIN, &st) == 0 || errno != ENOENT) {
        snprintf(detail, capacity, "reserved override already exists or unsafe directory"); goto done;
    }
    if (lstat(BLIT_TRIAL_HELPER, &st) < 0 || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() || (st.st_mode & 0022) || !(st.st_mode & 0100)) {
        snprintf(detail, capacity, "recovery executable missing or unsafe"); goto done;
    }
    memset(state, 0, sizeof(*state));
    random_fd = open("/dev/urandom", O_RDONLY|O_CLOEXEC);
    if (random_fd < 0) goto done;
    i = read(random_fd, random, sizeof(random)); close(random_fd);
    if (i != sizeof(random)) goto done;
    for (i = 0; i < 16; ++i) snprintf(state->id + i * 2, 3, "%02x", random[i]);
    state->seconds = seconds; state->expires = (long long)time(NULL) + seconds;
    state->active = 1;
    snprintf(state->phase, sizeof(state->phase), "armed");
    snprintf(state->renderer, sizeof(state->renderer), "%s", renderer);
    snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.json", state->id);
    if (!trial_bytes(path, document, length, 1)) goto failed;
    snprintf(environment, sizeof(environment), "BLIT_SETTINGS_PATH=" BLIT_TRIAL_ROOT "/%s.json\n"
        "BLIT_SETTINGS=load\nSDL_VIDEODRIVER=kmsdrm\nBLIT_PRESENTATION_TRIAL_ID=%s\n", state->id, state->id);
    snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.env", state->id);
    if (!trial_bytes(path, environment, strlen(environment), 1) || !trial_state_write(state)) goto failed;
    snprintf(timer, sizeof(timer), "--unit=blit-presentation-recovery-%s", state->id);
    snprintf(interval, sizeof(interval), "--on-active=%ds", seconds);
    if (!BLIT_TRIAL_RUN("/usr/bin/systemd-run", (char *const[]){"systemd-run", "--quiet",
        "--collect", "--property=Type=exec", "--timer-property=AccuracySec=1s", timer,
        interval, BLIT_TRIAL_HELPER, "--recover-trial", state->id, NULL}, detail, capacity, 5)) goto failed;
    armed = 1;
    snprintf(override, sizeof(override), "# blit-presentation-trial:%s\n[Service]\n"
        "EnvironmentFile=" BLIT_TRIAL_ROOT "/%s.env\nTimeoutStopSec=5s\nRestart=no\n", state->id, state->id);
    if (!trial_override_create(override) ||
        !trial_manager("daemon-reload", NULL, detail, capacity)) goto failed;
    snprintf(state->detail, sizeof(state->detail), "independent recovery armed; canonical settings untouched");
    ok = trial_state_write(state); goto done;
failed:
    snprintf(state->phase, sizeof(state->phase), "failed");
    snprintf(state->detail, sizeof(state->detail), "%s", detail[0] ? detail : "trial preparation failed");
    if (!armed) state->active = 0;
    (void)trial_state_write(state);
    if (!armed) {
        snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.json", state->id); unlink(path);
        snprintf(path, sizeof(path), BLIT_TRIAL_ROOT "/%s.env", state->id); unlink(path);
    }
done:
    close(lock);
    if (!ok && armed) (void)trial_restore(state->id, detail, capacity);
    return ok;
}
static int trial_activate(const char *id, char *detail, size_t capacity)
{
    BlitTrialState state;
    int lock = trial_lock(), ok = 0;
    if (lock < 0) return 0;
    if (trial_state_read(&state) && state.active && strcmp(state.id, id) == 0 &&
        strcmp(state.phase, "armed") == 0 && time(NULL) < state.expires) {
        snprintf(state.phase, sizeof(state.phase), "running");
        ok = trial_state_write(&state);
    }
    close(lock);
    if (!ok) { snprintf(detail, capacity, "trial is stale or not armed"); return 0; }
    return BLIT_TRIAL_RUN("/usr/bin/systemctl", (char *const[]){"systemctl", "restart",
        "--no-block", "blit.service", NULL}, detail, capacity, 5);
}
