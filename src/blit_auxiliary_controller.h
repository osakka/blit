#ifndef BLIT_AUXILIARY_CONTROLLER_H
#define BLIT_AUXILIARY_CONTROLLER_H

/* ADR 0128. Sole controller; settings retain the durable default. Included
   after the native session implementation and canonical policy parser. */
static void auxiliary_state(BlitAuxiliaryBinding *b, const char *state, const char *reason)
{
    b->state = state;
    b->reason = reason;
}

static void cancel_auxiliary_worker(BlitRuntime *runtime)
{
    if (runtime->auxiliary_worker_pid > 0) {
        if (kill(-runtime->auxiliary_worker_pid, SIGKILL) < 0)
            kill(runtime->auxiliary_worker_pid, SIGKILL);
        runtime->auxiliary_worker_binding = -1;
    }
}

static void auxiliary_retire(BlitRuntime *runtime, BlitAuxiliaryBinding *b)
{
    BlitSession *s = find_session_id(runtime, b->session_id);
    if (s == NULL) return;
    if (s->child_exited) { destroy_session(runtime, s); return; }
    if (s->native_retire_due_us == 0) {
        if (kill(-s->process_id, SIGTERM) < 0) kill(s->process_id, SIGTERM);
        s->native_retire_due_us = monotonic_us() + 100000ULL;
    }
}

static BlitPane *auxiliary_resolve(BlitRuntime *runtime, BlitAuxiliaryBinding *b)
{
    int i, matches = 0, selected = -1;
    char pane_id[128];
    BlitPane *pane;
    b->pane_id[0] = '\0';
    for (i = 0; i < runtime->display_count; ++i) {
        const BlitDisplay *d = &runtime->displays[i];
        const char *selector = d->connector[0] ? d->connector : d->name;
        if (strcmp(selector, b->output) == 0) { ++matches; selected = i; }
    }
    if (matches != 1) {
        auxiliary_state(b, matches ? "conflict" : "waiting_output",
                        matches ? "ambiguous_output" : "output_unavailable");
        return NULL;
    }
    snprintf(pane_id, sizeof(pane_id), "pane-display-%llu-%d",
             runtime->displays[selected].id, b->pane_ordinal);
    pane = find_pane_id(runtime, pane_id);
    if (pane == NULL || pane->width < 1 || pane->height < 1) {
        auxiliary_state(b, "waiting_pane", "pane_unavailable");
        return NULL;
    }
    snprintf(b->pane_id, sizeof(b->pane_id), "%s", pane_id);
    return pane;
}

static int auxiliary_offset(BlitRuntime *runtime, const BlitSession *login,
                            const BlitPane *pane, int *row, int *column)
{
    BlitPane *anchor = find_pane_id(runtime, login->pane_id);
    int a, b;
    int64_t r, c;
    if (anchor == NULL) return 0;
    a = display_index_for_id(runtime, anchor->display_id);
    b = display_index_for_id(runtime, pane->display_id);
    if (a < 0 || b < 0) return 0;
    r = (int64_t)pane->y - anchor->y +
        ((int64_t)runtime->displays[b].bounds.y - runtime->displays[a].bounds.y) /
        runtime_terminal_cell_height(runtime);
    c = (int64_t)pane->x - anchor->x +
        ((int64_t)runtime->displays[b].bounds.x - runtime->displays[a].bounds.x) /
        runtime_terminal_cell_width(runtime);
    if (r < -32767 || r > 32767 || c < -32767 || c > 32767) return 0;
    *row = (int)r; *column = (int)c;
    return 1;
}

static void auxiliary_start_placement(BlitRuntime *runtime, int index, const BlitSession *login)
{
    BlitAuxiliaryBinding *b = &runtime->auxiliary_bindings[index];
    pid_t pid;
    if (runtime->auxiliary_worker_pid > 0) return;
    pid = fork();
    if (pid == 0) {
        struct passwd *account;
        DIR *directory;
        struct dirent *entry;
        const char *override = getenv("BLIT_HIBR_PLACER");
        char helper[512], account_home[512], account_name[128];
        char owner[32], ticks[32], row[32], column[32];
        int null_fd = open("/dev/null", O_RDWR);
        snprintf(helper, sizeof(helper), "%s", override && *override ? override :
                 "/usr/share/blit/hibr/blit-desktop-place.hibr");
        snprintf(owner, sizeof(owner), "%ld", (long)b->owner_pid);
        snprintf(ticks, sizeof(ticks), "%llu", b->owner_ticks);
        snprintf(row, sizeof(row), "%d", b->row_offset);
        snprintf(column, sizeof(column), "%d", b->column_offset);
        setpgid(0, 0);
        if (null_fd < 0 || dup2(null_fd, 0) < 0 || dup2(null_fd, 1) < 0 || dup2(null_fd, 2) < 0) _exit(126);
        directory = opendir("/proc/self/fd");
        if (directory == NULL) _exit(126);
        while ((entry = readdir(directory)) != NULL) {
            int fd = atoi(entry->d_name);
            if (fd > 2 && fd != dirfd(directory)) close(fd);
        }
        closedir(directory);
        account = getpwuid(login->login_identity_uid);
        if (account == NULL || strlen(account->pw_dir) >= sizeof(account_home) ||
            strlen(account->pw_name) >= sizeof(account_name)) _exit(126);
        snprintf(account_home, sizeof(account_home), "%s", account->pw_dir);
        snprintf(account_name, sizeof(account_name), "%s", account->pw_name);
        if (initgroups(account_name, login->login_identity_gid) != 0 ||
            setgid(login->login_identity_gid) != 0 || setuid(login->login_identity_uid) != 0) _exit(126);
        if (clearenv() != 0 || setenv("HOME", account_home, 1) != 0 ||
            setenv("USER", account_name, 1) != 0 || setenv("LOGNAME", account_name, 1) != 0 ||
            setenv("PATH", "/usr/bin:/bin", 1) != 0 || setenv("TERM", "xterm-kitty", 1) != 0) _exit(126);
        execl("/usr/bin/hibr", "hibr", helper, "desktop", login->login_client_name,
              b->client_name, owner, ticks, row, column, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        b->placement = -1;
        auxiliary_state(b, "conflict", "placement_spawn_failed");
        return;
    }
    setpgid(pid, pid);
    runtime->auxiliary_worker_pid = pid;
    runtime->auxiliary_worker_binding = index;
    runtime->auxiliary_worker_deadline_us = monotonic_us() + BLIT_NATIVE_QUERY_DEADLINE_US;
    b->placement = 1;
    auxiliary_state(b, "waiting_join", "initial_placement_running");
}

static void service_auxiliary_displays(BlitRuntime *runtime, uint64_t now)
{
    BlitSession *login = find_session_id(runtime, runtime->login_session_id);
    int ready = native_login_ready(login, now), i, started = 0;
    if (runtime->auxiliary_worker_pid > 0) {
        int index = runtime->auxiliary_worker_binding, status = 0;
        BlitAuxiliaryBinding *b = index >= 0 && index < runtime->auxiliary_count ?
            &runtime->auxiliary_bindings[index] : NULL;
        pid_t result;
        if (b != NULL && (login == NULL || login->child_exited || login->id != b->login_id ||
            (ready && (login->login_desktop_pid != b->owner_pid || login->login_desktop_start_ticks != b->owner_ticks)))) {
            cancel_auxiliary_worker(runtime);
            b = NULL;
        }
        if (b != NULL && now >= runtime->auxiliary_worker_deadline_us) {
            cancel_auxiliary_worker(runtime);
            b->placement = -1;
            auxiliary_state(b, "conflict", "placement_timeout");
            b = NULL;
        }
        result = waitpid(runtime->auxiliary_worker_pid, &status, WNOHANG);
        if (result > 0 || (result < 0 && errno == ECHILD)) {
            runtime->auxiliary_worker_pid = 0;
            if (b != NULL) {
                b->placement = result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 2 : -1;
                b->placement_observed_after_us = now;
                auxiliary_state(b, b->placement == 2 ? "waiting_join" : "conflict",
                                b->placement == 2 ? "initial_placement_confirming" : "placement_failed");
                runtime->login_observer_due_us = now;
            }
        }
    }
    for (i = 0; i < runtime->auxiliary_count; ++i) {
        BlitAuxiliaryBinding *b = &runtime->auxiliary_bindings[i];
        BlitSession *child = find_session_id(runtime, b->session_id);
        BlitPane *pane;
        BlitNativeDesktopClient *native = NULL;
        int j;
        if (b->login_id && (login == NULL || login->child_exited || login->id != b->login_id ||
            (ready && (login->login_desktop_pid != b->owner_pid || login->login_desktop_start_ticks != b->owner_ticks)))) {
            auxiliary_retire(runtime, b);
            b->session_id = b->login_id = 0;
            b->attempted = b->placement = 0;
            b->client_name[0] = '\0';
            child = NULL;
        }
        pane = auxiliary_resolve(runtime, b);
        if (pane == NULL) {
            if (child != NULL) {
                if (runtime->auxiliary_worker_pid > 0 && runtime->auxiliary_worker_binding == i)
                    cancel_auxiliary_worker(runtime);
                auxiliary_retire(runtime, b);
                b->session_id = 0;
                b->attempted = b->placement = 0;
            }
            continue;
        }
        if (login == NULL || login->child_exited || login->login_identity_state != 2) {
            auxiliary_state(b, "waiting_authentication", "authentication_required");
            continue;
        }
        if (!ready) { auxiliary_state(b, "waiting_desktop", "fresh_native_owner_required"); continue; }
        if (b->attempted && (child == NULL || child->child_exited)) {
            auxiliary_state(b, "stopped", "standby_process_exited");
            continue;
        }
        if (child != NULL && strcmp(child->pane_id, pane->id) != 0) {
            auxiliary_state(b, "conflict", "managed_pane_assignment_changed");
            continue;
        }
        if (child == NULL) {
            char request[2048], launcher[1024];
            const char *override = getenv("BLIT_AUXILIARY_LAUNCHER");
            BlitSession *created = NULL;
            if (pane_surface_id(runtime, pane) != NULL) { auxiliary_state(b, "conflict", "pane_occupied"); continue; }
            if (!auxiliary_offset(runtime, login, pane, &b->row_offset, &b->column_offset)) {
                auxiliary_state(b, "conflict", "placement_geometry_unavailable");
                continue;
            }
            if (started) { auxiliary_state(b, "starting", "start_queued"); continue; }
            b->login_id = login->id;
            b->owner_pid = login->login_desktop_pid;
            b->owner_ticks = login->login_desktop_start_ticks;
            b->attempted = 1;
            snprintf(b->client_name, sizeof(b->client_name), "blit-aux-%llx-%llu",
                     (unsigned long long)runtime->started_us, runtime->next_session_id);
            json_escape(override && *override ? override : "/usr/share/blit/hibr/blit-session.hibr", launcher, sizeof(launcher));
            snprintf(request, sizeof(request),
                     "{\"command\":\"/usr/bin/hibr\",\"arguments\":[\"%s\",\"--session\",\"desktop\","
                     "\"--standby\",\"--name\",\"%s\"],\"pane_id\":\"%s\"}", launcher, b->client_name, pane->id);
            started = 1;
            if (create_session(runtime, request, &created, -1) != 1) {
                auxiliary_state(b, "conflict", "standby_start_failed");
                continue;
            }
            b->session_id = created->id;
            auxiliary_state(b, "waiting_join", "awaiting_native_client");
            runtime->login_observer_due_us = now;
            continue;
        }
        if (b->placement >= 0 && b->placement < 3) {
            int row, column;
            if (!auxiliary_offset(runtime, login, pane, &row, &column)) {
                auxiliary_state(b, "conflict", "placement_geometry_unavailable");
                continue;
            }
            if (row != b->row_offset || column != b->column_offset) {
                if (runtime->auxiliary_worker_pid > 0 && runtime->auxiliary_worker_binding == i)
                    cancel_auxiliary_worker(runtime);
                b->placement = 0;
                b->row_offset = row;
                b->column_offset = column;
            }
        }
        for (j = 0; j < login->login_desktop_client_count; ++j)
            if (strcmp(login->login_desktop_clients[j].name, b->client_name) == 0) native = &login->login_desktop_clients[j];
        if (native == NULL) {
            auxiliary_state(b, "waiting_join", b->placement == 3 ? "native_client_absent_release_unknown" : "awaiting_native_client");
            continue;
        }
        if (b->placement == 3) { auxiliary_state(b, "joined", "native_client_verified"); continue; }
        if (b->placement < 0) { auxiliary_state(b, "conflict", "initial_placement_failed"); continue; }
        if (b->placement == 1) continue;
        if (b->placement == 2 && login->login_desktop_observed_us <= b->placement_observed_after_us) continue;
        if (native->row - login->login_desktop_geometry[2] == b->row_offset &&
            native->column - login->login_desktop_geometry[3] == b->column_offset) {
            b->placement = 3;
            auxiliary_state(b, "joined", "initial_placement_verified");
        } else if (b->placement == 2) {
            b->placement = -1;
            auxiliary_state(b, "conflict", "initial_placement_unconfirmed");
        } else auxiliary_start_placement(runtime, i, login);
    }
}

static void auxiliary_replace(BlitRuntime *runtime, BlitAuxiliaryBinding *bindings,
                               int count, int explicit_apply, int retry)
{
    int i, j, worker_index = -1, unchanged = count == runtime->auxiliary_count;
    for (i = 0; i < count && unchanged; ++i)
        unchanged = strcmp(bindings[i].output, runtime->auxiliary_bindings[i].output) == 0 &&
            bindings[i].pane_ordinal == runtime->auxiliary_bindings[i].pane_ordinal;
    if (unchanged && !retry && runtime->auxiliary_explicit == explicit_apply) return;
    for (i = 0; i < runtime->auxiliary_count; ++i) {
        BlitAuxiliaryBinding *old = &runtime->auxiliary_bindings[i];
        int retained = 0;
        for (j = 0; j < count; ++j) {
            if (strcmp(old->output, bindings[j].output) || old->pane_ordinal != bindings[j].pane_ordinal) continue;
            if (retry && old->attempted && (old->state == NULL ||
                strcmp(old->state, "stopped") == 0 || strcmp(old->state, "conflict") == 0)) break;
            bindings[j] = *old;
            if (runtime->auxiliary_worker_pid > 0 && runtime->auxiliary_worker_binding == i)
                worker_index = j;
            retained = 1;
            break;
        }
        if (!retained) auxiliary_retire(runtime, old);
    }
    if (runtime->auxiliary_worker_pid > 0) {
        if (worker_index < 0) cancel_auxiliary_worker(runtime);
        else runtime->auxiliary_worker_binding = worker_index;
    }
    memcpy(runtime->auxiliary_bindings, bindings, (size_t)count * sizeof(*bindings));
    runtime->auxiliary_count = count;
    runtime->auxiliary_explicit = explicit_apply;
    ++runtime->auxiliary_revision;
    ++runtime->auxiliary_operation;
    service_auxiliary_displays(runtime, monotonic_us());
}

static void auxiliary_json(BlitRuntime *runtime, char *body, size_t capacity)
{
    size_t used;
    int i;
    used = (size_t)snprintf(body, capacity,
        "{\"revision\":%llu,\"operation_id\":\"auxiliary-%llu\",\"source\":\"%s\",\"bindings\":[",
        runtime->auxiliary_revision, runtime->auxiliary_operation, runtime->auxiliary_explicit ? "apply" : "startup");
    for (i = 0; i < runtime->auxiliary_count && used + 1024 < capacity; ++i) {
        BlitAuxiliaryBinding *b = &runtime->auxiliary_bindings[i];
        char session[64] = "null", pane[140] = "null", client[80] = "null";
        if (b->session_id) snprintf(session, sizeof(session), "\"%llu\"", b->session_id);
        if (b->pane_id[0]) snprintf(pane, sizeof(pane), "\"%s\"", b->pane_id);
        if (b->client_name[0]) snprintf(client, sizeof(client), "\"%s\"", b->client_name);
        used += (size_t)snprintf(body + used, capacity - used,
            "%s{\"output\":\"%s\",\"pane_ordinal\":%d,\"pane_id\":%s,\"session_id\":%s,"
            "\"client_name\":%s,\"state\":\"%s\",\"reason\":\"%s\","
            "\"initial_placement\":{\"row_offset\":%d,\"column_offset\":%d,\"confirmed\":%s}}",
            i ? "," : "", b->output, b->pane_ordinal, pane, session, client,
            b->state ? b->state : "waiting_desktop", b->reason ? b->reason : "pending",
            b->row_offset, b->column_offset, b->placement == 3 ? "true" : "false");
    }
    snprintf(body + used, capacity - used, "]}");
}
#endif
