/* ADR 0205: restart after an update, at a safe moment.

   The package does not restart Blit (ADR 0008), and an `apt upgrade` typed
   into a terminal inside Blit must not be killed half done. So every 5 s,
   off the frame path, Blit looks at the binary it was started from and at
   the install root's VERSION: a restart is pending when the installed
   version is not the running one. Under `at-login-screen` it restarts,
   through the orderly path of POST /v1/control/restart, once the login
   screen shows with nobody signed in, no other session runs, no input has
   come for 10 s and no API update is in progress -- and only once for an
   installed version, remembered across the exec, so a package whose
   VERSION and binary disagree cannot make it restart for ever. Under
   `manual` it only reports. Included into blit_server.c after blit_hud.h. */

#define BLIT_UPDATE_CHECK_US 5000000ULL
#define BLIT_UPDATE_IDLE_US 10000000ULL

/* What the safe moment still lacks, in waiting_for's order. */
#define BLIT_UPDATE_WAIT_LOGIN_SCREEN 1u
#define BLIT_UPDATE_WAIT_SIGNED_IN 2u
#define BLIT_UPDATE_WAIT_USER_SESSION 4u
#define BLIT_UPDATE_WAIT_INPUT 8u
#define BLIT_UPDATE_WAIT_UPDATE 16u
#define BLIT_UPDATE_WAIT_ATTEMPTED 32u

static const char *const update_waiting_names[] = {
    "login-screen", "signed-in", "user-session", "input", "update",
    "restart-attempted"
};

/* At start, before any session is spawned: the install root, the version
   a restart was attempted for (taken out of the environment, so no child
   inherits it), and whether this process is the installed binary. A
   development build elsewhere is never restarted into the package. */
static void update_init(BlitRuntime *runtime)
{
    const char *test = getenv("BLIT_TEST_MODE");
    const char *root = getenv("BLIT_INSTALL_ROOT");
    const char *attempted = getenv("BLIT_UPDATE_RESTARTED");
    const char *version = getenv("BLIT_TEST_VERSION");
    int testing = test != NULL && strcmp(test, "1") == 0;
    char binary[PATH_MAX + 32];
    struct stat self, installed;

    snprintf(runtime->install_root, sizeof(runtime->install_root), "%s",
             testing && root != NULL && root[0] != '\0' ? root : "/usr/lib/blit");
    if (attempted != NULL)
        snprintf(runtime->update_attempted_version,
                 sizeof(runtime->update_attempted_version), "%s", attempted);
    if (testing && version != NULL && version[0] != '\0')
        snprintf(blit_version, sizeof(blit_version), "%s", version);
    unsetenv("BLIT_UPDATE_RESTARTED");
    unsetenv("BLIT_TEST_VERSION");
    snprintf(binary, sizeof(binary), "%s/bin/blit-server", runtime->install_root);
    if (stat("/proc/self/exe", &self) == 0 && stat(binary, &installed) == 0 &&
        self.st_dev == installed.st_dev && self.st_ino == installed.st_ino) {
        runtime->update_watched = 1;
        runtime->update_exe_dev = installed.st_dev;
        runtime->update_exe_ino = installed.st_ino;
        runtime->update_exe_mtime = installed.st_mtim;
    }
}

static int update_in_progress(const BlitRuntime *runtime)
{
    int i;
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i)
        if (runtime->http_clients[i].used && runtime->http_clients[i].update_fd >= 0)
            return 1;
    return blit_update_exit_requested ||
        strcmp(runtime->update_state, "receiving") == 0 ||
        strcmp(runtime->update_state, "installing") == 0 ||
        strcmp(runtime->update_state, "restarting") == 0;
}

/* The safe moment's missing parts. The login screen is the login manager's
   session, running and still waiting for its authenticated hook: an
   identity verified, rejected or unavailable (a custom session command
   cannot say) counts as somebody signed in. */
static unsigned update_waiting_for(BlitRuntime *runtime, uint64_t now)
{
    BlitSession *login = runtime->login_session_id != 0 ?
        find_session_id(runtime, runtime->login_session_id) : NULL;
    unsigned waiting = 0;
    int i;
    if (login == NULL || !login->used || login->child_exited || !login->login_manager)
        waiting |= BLIT_UPDATE_WAIT_LOGIN_SCREEN;
    else if (login->login_identity_state != 1)
        waiting |= BLIT_UPDATE_WAIT_SIGNED_IN;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        const BlitSession *session = &runtime->sessions[i];
        if (session->used && !session->child_exited && session != login)
            waiting |= BLIT_UPDATE_WAIT_USER_SESSION;
    }
    if (runtime->last_input_us != 0 && now - runtime->last_input_us < BLIT_UPDATE_IDLE_US)
        waiting |= BLIT_UPDATE_WAIT_INPUT;
    if (update_in_progress(runtime))
        waiting |= BLIT_UPDATE_WAIT_UPDATE;
    if (strcmp(runtime->update_attempted_version, runtime->update_installed_version) == 0)
        waiting |= BLIT_UPDATE_WAIT_ATTEMPTED;
    return waiting;
}

static void update_tick(BlitRuntime *runtime, uint64_t now)
{
    char path[PATH_MAX + 32], executable[PATH_MAX], version[64] = "";
    struct stat installed;
    ssize_t length;
    FILE *file;

    if (runtime->update_checked_us != 0 && now - runtime->update_checked_us < BLIT_UPDATE_CHECK_US)
        return;
    runtime->update_checked_us = now;
    snprintf(path, sizeof(path), "%s/VERSION", runtime->install_root);
    if ((file = fopen(path, "r")) != NULL) {
        if (fgets(version, sizeof(version), file) == NULL) version[0] = '\0';
        version[strcspn(version, " \t\r\n")] = '\0';
        fclose(file);
    }
    snprintf(runtime->update_installed_version, sizeof(runtime->update_installed_version),
             "%s", version);
    if (runtime->update_watched && !runtime->update_replaced) {
        int deleted = 0;
        length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length > 10) {
            executable[length] = '\0';
            deleted = strcmp(executable + length - 10, " (deleted)") == 0;
        }
        snprintf(path, sizeof(path), "%s/bin/blit-server", runtime->install_root);
        if (deleted || stat(path, &installed) != 0 ||
            installed.st_dev != runtime->update_exe_dev ||
            installed.st_ino != runtime->update_exe_ino ||
            installed.st_mtim.tv_sec != runtime->update_exe_mtime.tv_sec ||
            installed.st_mtim.tv_nsec != runtime->update_exe_mtime.tv_nsec)
            runtime->update_replaced = 1;
    }
    runtime->update_restart_pending = runtime->update_watched && version[0] != '\0' &&
        strcmp(version, blit_version) != 0;
    runtime->update_waiting = runtime->update_restart_pending ?
        update_waiting_for(runtime, now) : 0;
    if (!runtime->update_restart_pending || runtime->restart_after_update_manual ||
        runtime->update_waiting != 0)
        return;
    {
        char detail[192];
        snprintf(detail, sizeof(detail), "%s installed under %s; restarting at the login screen",
                 version, blit_version);
        diagnostic_log(runtime, "info", "update_restart", detail);
        fprintf(stderr, "blit: %s\n", detail);
    }
    setenv("BLIT_UPDATE_RESTARTED", version, 1);
    blit_restart_requested = 1;
    blit_running = 0;
}

/* /v1/diagnostics "update". */
static void update_json(const BlitRuntime *runtime, char *out, size_t capacity)
{
    char running[160], installed[160] = "null";
    size_t used = 0;
    unsigned i;
    int first = 1;
    json_escape(blit_version, running, sizeof(running));
    if (runtime->update_installed_version[0] != '\0') {
        char escaped[144];
        json_escape(runtime->update_installed_version, escaped, sizeof(escaped));
        snprintf(installed, sizeof(installed), "\"%s\"", escaped);
    }
    out[0] = '\0';
    json_appendf(out, capacity, &used,
                 "{\"running_version\":\"%s\",\"installed_version\":%s,"
                 "\"executable_replaced\":%s,\"restart_pending\":%s,"
                 "\"restart_after_update\":\"%s\",\"waiting_for\":[",
                 running, installed, runtime->update_replaced ? "true" : "false",
                 runtime->update_restart_pending ? "true" : "false",
                 runtime->restart_after_update_manual ? "manual" : "at-login-screen");
    for (i = 0; i < sizeof(update_waiting_names) / sizeof(update_waiting_names[0]); ++i)
        if (runtime->update_waiting & (1u << i)) {
            json_appendf(out, capacity, &used, "%s\"%s\"", first ? "" : ",",
                         update_waiting_names[i]);
            first = 0;
        }
    json_appendf(out, capacity, &used, "]}");
}
