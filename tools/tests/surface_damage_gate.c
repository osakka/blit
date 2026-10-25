/* Exercise the actual terminal state's damage accumulators, not a copy. */
#define main blit_server_entry_for_damage_gate
/* Supply cached adapter metadata for completion/recovery classification.
   Rendering and event-service policy remain the actual production code. */
#define blit_kms_display_info fixture_kms_display_info
#include "../../src/blit_server.c"
#undef main
#undef blit_kms_display_info

static int fixture_kms_info_enabled;
static BlitKmsDisplayInfo fixture_kms_info;
int fixture_kms_display_info(const BlitKms *kms, int index, BlitKmsDisplayInfo *info)
{
    (void)kms;
    if (!fixture_kms_info_enabled || index != 0) return -1;
    *info = fixture_kms_info;
    return 0;
}

static int failures;
static BlitSurface subject;
static BlitSession owner;

static void utf8_fragmentation_gate(void)
{
    static const size_t sizes[] = {1, 2, 3, 4, 127, 767, 768, 769, 4095, 65535};
    BlitSurface *expected = calloc(1, sizeof(*expected));
    BlitSession *session = calloc(1, sizeof(*session));
    unsigned char bytes[131072];
    size_t length = 0, i, j;
    if (expected == NULL || session == NULL) exit(2);
    memset(&subject, 0, sizeof(subject));
    subject.width = 80; subject.height = 40; subject.scroll_bottom = 39;
    terminal_reset_tabs(&subject);
    session->terminal_autowrap = 1;
    for (i = 0; i < 1800; ++i) {
        length += snprintf((char *)bytes + length, sizeof(bytes) - length,
            "\033[%zu;%zuH\033[38;2;%zu;17;91;48;2;23;55;99m"
            "\xe2\x96\x80\xc3\xa9\xf0\x9f\x9a\x80" "e\xcc\x81",
            i % 38 + 1, i % 65 + 1, i % 256);
    }
    length += snprintf((char *)bytes + length, sizeof(bytes) - length,
                       "\033]2;fragmentation-complete\007\033[40;70HEND");
    if (length >= sizeof(bytes)) exit(2);
    /* One continuous input to the production parser is the reference. The
       entry point must agree for every tested fragmentation of those bytes. */
    terminal_consume_chunk(NULL, session, &subject, bytes, length);
    *expected = subject;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        memset(&subject, 0, sizeof(subject));
        subject.width = 80; subject.height = 40; subject.scroll_bottom = 39;
        terminal_reset_tabs(&subject);
        memset(session, 0, sizeof(*session)); session->terminal_autowrap = 1;
        for (j = 0; j < length;) {
            size_t count = sizes[i] < length - j ? sizes[i] : length - j;
            terminal_consume_output(NULL, session, &subject, bytes + j, count);
            j += count;
        }
        if (memcmp(expected->codepoints, subject.codepoints, sizeof(subject.codepoints)) ||
            memcmp(expected->foreground, subject.foreground, sizeof(subject.foreground)) ||
            memcmp(expected->background, subject.background, sizeof(subject.background)) ||
            memcmp(expected->attributes, subject.attributes, sizeof(subject.attributes)) ||
            memcmp(expected->combining, subject.combining, sizeof(subject.combining)) ||
            memcmp(expected->combining_count, subject.combining_count, sizeof(subject.combining_count)) ||
            expected->cursor_x != subject.cursor_x || expected->cursor_y != subject.cursor_y ||
            session->terminal_utf8_pending_length || session->terminal_escape ||
            strcmp(session->terminal_title, "fragmentation-complete")) {
            fprintf(stderr, "UTF8_FRAGMENT_FAIL chunk=%zu length=%zu pending=%d\n",
                    sizes[i], length, session->terminal_utf8_pending_length);
            ++failures;
        }
    }
    free(expected); free(session);
}

static void utf8_large_pty_read_gate(void)
{
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    BlitSession *session;
    unsigned char bytes[4096];
    const char tail[] = "\033[31;70HEND";
    int fds[2];
    if (runtime == NULL || pipe(fds) != 0) exit(2);
    memset(&subject, 0, sizeof(subject));
    subject.used = 1; subject.id = 1; subject.owner_session_id = 1;
    subject.width = 80; subject.height = 40; subject.scroll_bottom = 39;
    runtime->surfaces[0] = &subject;
    session = &runtime->sessions[0];
    session->used = 1; session->id = 1; session->pty_fd = fds[0];
    session->terminal_autowrap = 1;
    snprintf(session->state, sizeof(session->state), "running");
    if (write(fds[1], "\xe2", 1) != 1) exit(2);
    service_session_output(runtime, fds[0]);
    memset(bytes, 0, sizeof(bytes)); /* NUL is ignored, not a string terminator. */
    bytes[0] = 0x96; bytes[1] = 0x80; /* finish U+2580 upper half block */
    memcpy(bytes + sizeof(bytes) - sizeof(tail) + 1, tail, sizeof(tail) - 1);
    if (write(fds[1], bytes, sizeof(bytes)) != sizeof(bytes)) exit(2);
    service_session_output(runtime, fds[0]);
    if (subject.codepoints[0][0] != 0x2580 ||
        subject.codepoints[30][69] != 'E' ||
        subject.codepoints[30][71] != 'D' ||
        session->terminal_utf8_pending_length != 0 ||
        runtime->diagnostic_terminal_bytes != sizeof(bytes) + 1) {
        fprintf(stderr, "UTF8_PTY_FAIL pending prefix caused large PTY read loss: "
                "first=%x tail=%x pending=%d bytes=%llu\n",
                subject.codepoints[0][0], subject.codepoints[30][69],
                session->terminal_utf8_pending_length,
                runtime->diagnostic_terminal_bytes);
        ++failures;
    }
    close(fds[0]); close(fds[1]); free(runtime);
}

static void http_accept_budget_gate(void)
{
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int listener, clients[BLIT_MAX_HTTP_CLIENTS + 5];
    int i, used, first_wave = BLIT_HTTP_ACCEPT_BUDGET + 3;
    if (!runtime) exit(2);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener < 0 || fcntl(listener, F_SETFL, O_NONBLOCK) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, BLIT_MAX_HTTP_CLIENTS) < 0 ||
        getsockname(listener, (struct sockaddr *)&address, &length) < 0) exit(2);
    runtime->render_pending = 1;
    runtime->render_next_us[0] = 12345;
    for (i = 0; i < first_wave; ++i) {
        clients[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (clients[i] < 0 || connect(clients[i], (struct sockaddr *)&address, length) < 0) exit(2);
    }
    if (!accept_http_clients(runtime, listener)) exit(2);
    for (used = i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i)
        if (runtime->http_clients[i].used) ++used;
    if (used != BLIT_HTTP_ACCEPT_BUDGET || !runtime->render_pending ||
        runtime->render_next_us[0] != 12345) {
        fprintf(stderr, "HTTP_ACCEPT_LIMIT_FAIL first_turn=%d budget=%d render_deadline=%llu\n",
            used, BLIT_HTTP_ACCEPT_BUDGET, (unsigned long long)runtime->render_next_us[0]);
        ++failures;
    }
    if (!accept_http_clients(runtime, listener) || !accept_http_clients(runtime, listener)) exit(2);
    for (used = i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i)
        if (runtime->http_clients[i].used) ++used;
    if (used != first_wave) { fprintf(stderr,"HTTP_ACCEPT_LIMIT_FAIL finite drain\n"); ++failures; }
    for (i = first_wave; i < BLIT_MAX_HTTP_CLIENTS + 5; ++i) {
        clients[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (clients[i] < 0 || connect(clients[i], (struct sockaddr *)&address, length) < 0) exit(2);
    }
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i)
        if (!accept_http_clients(runtime, listener)) exit(2);
    for (used = i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i)
        if (runtime->http_clients[i].used) {
            int descriptor_flags = fcntl(runtime->http_clients[i].fd,F_GETFD);
            int status_flags = fcntl(runtime->http_clients[i].fd,F_GETFL);
            ++used;
            if (descriptor_flags < 0 || !(descriptor_flags & FD_CLOEXEC)) {
                fprintf(stderr,"HTTP_ACCEPT_LIMIT_FAIL descriptor inheritance\n"); ++failures;
            }
            if (status_flags < 0 || !(status_flags & O_NONBLOCK)) {
                fprintf(stderr,"HTTP_ACCEPT_LIMIT_FAIL accepted descriptor can block\n"); ++failures;
            }
        }
    if (used != BLIT_MAX_HTTP_CLIENTS) { fprintf(stderr,"HTTP_ACCEPT_LIMIT_FAIL capacity=%d\n",used); ++failures; }
    for (i = BLIT_MAX_HTTP_CLIENTS; i < BLIT_MAX_HTTP_CLIENTS + 5; ++i) {
        char response[256];
        struct timeval timeout = {1,0};
        ssize_t received;
        setsockopt(clients[i],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        received = recv(clients[i],response,sizeof(response),0);
        if (received < 12 || strncmp(response,"HTTP/1.1 503",12) != 0) {
            fprintf(stderr,"HTTP_ACCEPT_LIMIT_FAIL overflow not rejected\n"); ++failures;
        }
    }
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS + 5; ++i) close(clients[i]);
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) remove_http_client(runtime,i);
    {
        static const char request[] = "GET /v1/version HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        char response[256];
        struct timeval timeout = {0,100000};
        fd_set readable;
        int client = socket(AF_INET,SOCK_STREAM,0);
        int ready;
        if (client < 0 || connect(client,(struct sockaddr *)&address,length) < 0 ||
            send(client,request,sizeof(request)-1,MSG_NOSIGNAL) != sizeof(request)-1 ||
            !accept_http_clients(runtime,listener)) exit(2);
        FD_ZERO(&readable); FD_SET(client,&readable);
        ready = select(client+1,&readable,NULL,NULL,&timeout);
        if (ready <= 0 || recv(client,response,sizeof(response),MSG_DONTWAIT) < 12 ||
            strncmp(response,"HTTP/1.1 200",12) != 0) {
            fprintf(stderr,"HTTP_INITIAL_READ_FAIL ready request was admitted but not serviced in its bounded turn\n");
            ++failures;
        }
        close(client);
        for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) remove_http_client(runtime,i);
    }
    close(listener);
    free(runtime);
}

static void kms_idle_completion_gate(void)
{
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) exit(2);
    runtime->display_count = runtime->output_count = 1;
    runtime->outputs[0].display_id = runtime->displays[0].id = 93;
    runtime->displays[0].refresh_hz = 60.0;
    runtime->render_interval_ms = 1000;
    runtime->render_next_us[0] = 16000;
    fixture_kms_info_enabled = 1;
    memset(&fixture_kms_info, 0, sizeof(fixture_kms_info));
    fixture_kms_info.presentation_state = BLIT_KMS_PRESENTATION_PRESENTING;
    /* No DRM device is needed: inventory is unchanged, no retry is pending,
       and completion bookkeeping must not fabricate a visual mutation. */
    service_kms_presentation_change(runtime, 16667);
    if (runtime->render_pending)
        advance_render_schedule(runtime, 16667, due_output_mask(runtime, 16667));
    if (runtime->render_pending || runtime->render_next_us[0] != 16000) {
        fprintf(stderr, "KMS_IDLE_COMPLETION_FAIL pending=%d deadline=%llu expected=16000\n",
            runtime->render_pending, (unsigned long long)runtime->render_next_us[0]);
        ++failures;
    }
    runtime->render_pending = runtime->pointer_dirty = 1;
    runtime->pointer_dirty_outputs = 1;
    service_kms_presentation_change(runtime, 17000);
    if (!runtime->render_pending || !runtime->pointer_dirty || runtime->pointer_dirty_outputs != 1) {
        fprintf(stderr, "KMS_IDLE_COMPLETION_FAIL existing pointer work was lost\n");
        ++failures;
    }
    runtime->render_pending = runtime->pointer_dirty = 0;
    runtime->pointer_dirty_outputs = 0;
    fixture_kms_info.presentation_state = BLIT_KMS_PRESENTATION_RECOVERING;
    service_kms_presentation_change(runtime, 18000);
    if (!runtime->render_pending) {
        fprintf(stderr, "KMS_IDLE_COMPLETION_FAIL timeout recovery request was suppressed\n");
        ++failures;
    }
    fixture_kms_info_enabled = 0;
    free(runtime);
}

static void kms_retry_deadline_gate(void)
{
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) exit(2);
    runtime->output_count = 2;
    runtime->kms_retry_outputs = 3;
    runtime->render_next_us[0] = 32000;
    runtime->render_next_us[1] = 64000;
    retry_ready_kms_outputs(runtime, 1, 17000);
    if (runtime->render_next_us[0] != 17000 ||
        runtime->render_next_us[1] != 64000 ||
        runtime->kms_retry_outputs != 2 || !runtime->render_pending) {
        fprintf(stderr, "KMS_RETRY_FAIL ready output not immediate or busy output rescheduled\n");
        ++failures;
    }
    retry_ready_kms_outputs(runtime, 1, 18000);
    if (runtime->render_next_us[0] != 17000 || runtime->kms_retry_outputs != 2) {
        fprintf(stderr, "KMS_RETRY_FAIL consumed retry was repeated\n");
        ++failures;
    }
    retry_ready_kms_outputs(runtime, 2, 19000);
    if (runtime->render_next_us[1] != 19000 || runtime->kms_retry_outputs != 0) {
        fprintf(stderr, "KMS_RETRY_FAIL second ready output not released\n");
        ++failures;
    }
    runtime->diagnostic_frame_rate_frames = 1;
    record_kms_frame_submission(runtime, 0);
    if (runtime->diagnostic_frame_rate_frames != 0 || runtime->diagnostic_present_calls != 0) {
        fprintf(stderr, "KMS_RETRY_FAIL deferred attempt counted as presentation\n");
        ++failures;
    }
    runtime->diagnostic_frame_rate_frames = 1;
    record_kms_frame_submission(runtime, 3);
    if (runtime->diagnostic_frame_rate_frames != 1 || runtime->diagnostic_present_calls != 2) {
        fprintf(stderr, "KMS_RETRY_FAIL actual output submissions not counted\n");
        ++failures;
    }
    free(runtime);
}

static void expired_batch_deadline_gate(void)
{
    BlitRuntime *runtime = calloc(1, sizeof(*runtime));
    BlitSession *session;
    int fds[2];
    const char bytes[] = "\033[2JX";
    if (runtime == NULL || pipe(fds) != 0) exit(2);
    session = &runtime->sessions[0];
    session->used = 1; session->id = 1; session->pty_fd = fds[0];
    snprintf(session->state, sizeof(session->state), "running");
    subject.id = 1; subject.used = 1; subject.owner_session_id = 1;
    subject.cursor_x = 0; subject.cursor_y = 0;
    subject.scroll_top = 0; subject.scroll_bottom = subject.height - 1;
    runtime->surfaces[0] = &subject;
    /* A quiet window that has expired is eligible for presentation. A new
       PTY chunk arriving in that loop turn must not reopen the window. */
    runtime->terminal_frame_quiet_until_us = 1;
    if (write(fds[1], bytes, sizeof(bytes) - 1) != sizeof(bytes) - 1) exit(2);
    service_session_output(runtime, fds[0]);
    if (runtime->terminal_frame_quiet_until_us != 1) {
        fprintf(stderr, "TERMINAL_BATCH_FAIL expired_deadline=1 rearmed=%llu\n",
                (unsigned long long)runtime->terminal_frame_quiet_until_us);
        ++failures;
    }
    if (runtime->diagnostic_terminal_reads != 1 || subject.codepoints[0][0] != 'X') {
        fprintf(stderr, "TERMINAL_BATCH_FAIL fixture did not parse its PTY chunk\n");
        ++failures;
    }
    runtime->render_pending = 1; runtime->output_count = 1;
    runtime->terminal_frame_quiet_until_us = 18000;
    runtime->render_next_us[0] = 9000;
    if (render_wait_ms(runtime, 10000) != 8) {
        fprintf(stderr, "TERMINAL_BATCH_FAIL active_quiet_window should wait 8ms, got=%d\n",
                render_wait_ms(runtime, 10000));
        ++failures;
    }
    runtime->pointer_dirty = 1;
    runtime->pointer_dirty_outputs = 1;
    if (render_wait_ms(runtime, 10000) != 0) {
        fprintf(stderr, "POINTER_FENCE_FAIL pointer deadline blocked by terminal batch\n");
        ++failures;
    }
    runtime->render_next_us[0] = 13000;
    if (render_wait_ms(runtime, 10000) != 3) {
        fprintf(stderr, "POINTER_FENCE_FAIL pointer did not retain output pacing\n");
        ++failures;
    }
    runtime->pointer_dirty = 0;
    runtime->pointer_dirty_outputs = 0;
    {
        BlitSurface *other = calloc(1, sizeof(*other));
        BlitSession *join = &runtime->sessions[1];
        if (other == NULL) exit(2);
        session->hibr_desktop_primary = 1;
        snprintf(session->hibr_desktop_session,
                 sizeof(session->hibr_desktop_session), "desktop");
        join->used = 1; join->id = 2; join->hibr_desktop_join = 1;
        snprintf(join->hibr_desktop_session,
                 sizeof(join->hibr_desktop_session), "desktop");
        other->id = 2; other->used = 1;
        runtime->surfaces[1] = other;
        runtime->hibr_frame_barrier_until_us = 1;
        arm_hibr_frame_barrier(runtime, session, 10000);
        if (runtime->hibr_frame_barrier_until_us != 1) {
            fprintf(stderr, "TERMINAL_BATCH_FAIL expired_join_barrier rearmed=%llu\n",
                    (unsigned long long)runtime->hibr_frame_barrier_until_us);
            ++failures;
        }
        free(other);
    }
    close(fds[0]); close(fds[1]);
    free(runtime);
}

static void expect_bounds(const char *name, int x, int y, int w, int h)
{
    if (!subject.damage_valid || subject.damage_x != x ||
        subject.damage_y != y || subject.damage_width != w ||
        subject.damage_height != h) {
        fprintf(stderr, "SURFACE_DAMAGE_FAIL %s got=%d,%d,%dx%d want=%d,%d,%dx%d\n",
                name, subject.damage_x, subject.damage_y,
                subject.damage_width, subject.damage_height, x, y, w, h);
        ++failures;
    }
}

static void nonvisual_terminal_bytes_gate(void)
{
    const unsigned char controls[] = "\033[31m\033[?1002h\033[?1006h\033[12;19H\033]2;title-only\007";
    const unsigned char upload[] = "\033_Ga=T,f=24,s=1,v=1,i=123,m=1;AAAA";
    int i;
    memset(&subject,0,sizeof(subject));memset(&owner,0,sizeof(owner));
    subject.width=80;subject.height=40;subject.scroll_bottom=39;
    subject.cursor_visible=0;
    if (terminal_consume_output(NULL,&owner,&subject,controls,sizeof(controls)-1) ||
        subject.damage_valid || subject.cursor_x!=18 || subject.cursor_y!=11 ||
        owner.terminal_mouse_mode!=1002 || strcmp(owner.terminal_title,"title-only")) {
        fprintf(stderr,"NONVISUAL_PTY_FAIL control-only bytes scheduled a visual revision\n");
        ++failures;
    }
    /* A fragmented APC upload is not an image until its final chunk. */
    if (terminal_consume_output(NULL,&owner,&subject,
        upload,sizeof(upload)-1) || subject.damage_valid) {
        fprintf(stderr,"NONVISUAL_PTY_FAIL incomplete image scheduled a repaint\n");++failures;
    }
    owner.terminal_escape=0; owner.terminal_string_length=0;
    if (!terminal_consume_output(NULL,&owner,&subject,(const unsigned char *)"X",1) ||
        !subject.damage_valid) { fprintf(stderr,"NONVISUAL_PTY_FAIL printable damage lost\n");++failures; }
    for (i=0;i<BLIT_GRAPHICS_MAX_IMAGES;++i) graphics_image_release(&owner.graphics[i]);
}

static void stable_client_names_gate(void)
{
    BlitRuntime *r = calloc(1, sizeof(*r));
    char a[64], b[64], long_name[128];
    int i;
    if (!r) exit(2);
#define NAME_CHECK(test) do { if (!(test)) { fprintf(stderr,"CLIENT_NAME_FAIL line=%d\n",__LINE__); ++failures; } } while (0)
    NAME_CHECK(blit_client_name_make("DP-1", 2, a, sizeof(a)) && !strcmp(a,"blit-DP-1-p2"));
    NAME_CHECK(blit_client_name_make("card:DP.1", 64, a, sizeof(a)) && !strcmp(a,"blit-card:DP.1-p64"));
    memset(long_name, 'a', sizeof(long_name)-1); long_name[127]=0;
    NAME_CHECK(blit_client_name_make(long_name, 64, a, sizeof(a)) && strlen(a)<=63);
    long_name[126]='b';
    NAME_CHECK(blit_client_name_make(long_name, 64, b, sizeof(b)) && strcmp(a,b));
    NAME_CHECK(blit_client_name_make("Display # 1", 1, a, sizeof(a)));
    for(i=0;a[i];++i) NAME_CHECK(blit_client_name_char(a[i]));
    NAME_CHECK(!blit_client_name_make("",1,a,sizeof(a)));
    NAME_CHECK(!blit_client_name_make("DP-1",0,a,sizeof(a)));
    NAME_CHECK(!blit_client_name_make("DP-1",65,a,sizeof(a)));
    NAME_CHECK(!blit_client_name_make("DP-1",1,a,4) && !a[0]);
    r->display_count=1; r->displays[0].id=93;
    snprintf(r->displays[0].connector,sizeof(r->displays[0].connector),"DP-1");
    r->pane_count=2;
    for(i=0;i<2;++i) {
        r->panes[i].used=1;
        snprintf(r->panes[i].id,sizeof(r->panes[i].id),"arbitrary-%d",i);
        snprintf(r->panes[i].display_id,sizeof(r->panes[i].display_id),"display-93");
    }
    NAME_CHECK(native_client_name_for_target(r,"arbitrary-1",NULL,a,sizeof(a)) && !strcmp(a,"blit-DP-1-p2"));
    r->displays[0].id=127; r->started_us=123; r->next_session_id=999;
    for(i=0;i<2;++i) snprintf(r->panes[i].display_id,sizeof(r->panes[i].display_id),"display-127");
    NAME_CHECK(native_client_name_for_target(r,"arbitrary-1",NULL,b,sizeof(b)) && !strcmp(a,b));
    r->display_count=2; r->displays[1]=r->displays[0]; r->displays[1].id=200;
    NAME_CHECK(!native_client_name_for_target(r,"arbitrary-1",NULL,b,sizeof(b)));
    r->display_count=1; r->sessions[0].used=1;
    snprintf(r->sessions[0].login_client_name,sizeof(r->sessions[0].login_client_name),"%s",a);
    NAME_CHECK(!native_client_name_for_target(r,"arbitrary-1",NULL,b,sizeof(b)));
    r->sessions[0].child_exited=1;
    NAME_CHECK(native_client_name_for_target(r,"arbitrary-1",NULL,b,sizeof(b)) && !strcmp(a,b));
    NAME_CHECK(native_client_name_for_target(r,"arbitrary-0",NULL,b,sizeof(b)) && !strcmp(b,"blit-DP-1-p1"));
    free(r);
#undef NAME_CHECK
}

int main(void)
{
    BlitRectangle rectangle;
    int direction;
    subject.width = 80;
    subject.height = 40;
    for (direction = 0; direction < 2; ++direction) {
        int first = direction ? 2 : 60;
        int last = direction ? 60 : 2;
        subject.damage_valid = 0;
        mark_surface_damage(&subject, first, first / 2);
        mark_surface_damage(&subject, last, last / 2);
        expect_bounds("point union in both orders", 2, 1, 59, 30);
        subject.damage_valid = 0;
        mark_surface_damage_width(&subject, first, first / 2, 4);
        mark_surface_damage_width(&subject, last, last / 2, 4);
        expect_bounds("wide union in both orders", 2, 1, 62, 30);
        subject.damage_valid = 0;
        mark_surface_damage_rect(&subject, first, first / 2, 4, 3);
        mark_surface_damage_rect(&subject, last, last / 2, 4, 3);
        expect_bounds("rectangle union in both orders", 2, 1, 62, 32);
    }
    subject.damage_valid = 0;
    mark_surface_damage(&subject, 70, 35);
    rectangle.left = 2; rectangle.top = 1;
    rectangle.right = 5; rectangle.bottom = 3;
    terminal_damage_rectangle(&subject, &rectangle);
    expect_bounds("DEC rectangle preserves prior damage", 2, 1, 69, 35);
    subject.damage_valid = 0;
    mark_surface_damage(&subject, 70, 35);
    subject.scroll_top = 2; subject.scroll_bottom = 8;
    terminal_scroll_up(&owner, &subject);
    expect_bounds("scroll preserves outside damage", 0, 2, 80, 34);
    subject.damage_valid = 0;
    mark_surface_full_damage(&subject);
    mark_surface_damage(&subject, 70, 35);
    mark_surface_damage(&subject, 2, 1);
    expect_bounds("full damage never shrinks", 0, 0, 80, 40);
    expired_batch_deadline_gate();
    http_accept_budget_gate();
    kms_idle_completion_gate();
    kms_retry_deadline_gate();
    utf8_large_pty_read_gate();
    utf8_fragmentation_gate();
    nonvisual_terminal_bytes_gate();
    stable_client_names_gate();
    if (failures) return 1;
    puts("SURFACE_DAMAGE_PASS union=both-orders DEC=union scroll=union quiet=bounded join=bounded wait=nonspinning UTF8=large-PTY+10-fragmentations stable_client_names=verified");
    return 0;
}
