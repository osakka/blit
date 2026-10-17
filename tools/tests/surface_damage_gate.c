/* Exercise the actual terminal state's damage accumulators, not a copy. */
#define main blit_server_entry_for_damage_gate
#include "../../src/blit_server.c"
#undef main

static int failures;
static BlitSurface subject;
static BlitSession owner;

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
    kms_retry_deadline_gate();
    if (failures) return 1;
    puts("SURFACE_DAMAGE_PASS union=both-orders DEC=union scroll=union quiet=bounded join=bounded wait=nonspinning");
    return 0;
}
