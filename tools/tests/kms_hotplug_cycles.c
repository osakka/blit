/* Exact production output lifecycle and cookies; only libdrm is substituted. */
#include "../../src/blit_kms.c"
#include "fixtures/drm_hotplug_mock.c"

static void topology(BlitKms *kms, const char *path, int second, int third)
{
    KmsSnapshot snapshot;
    FILE *file = fopen(path, "w");
    fprintf(file, "%d 0 0 0\n93 320 192\n", 1 + second + third);
    if (third) fprintf(file, "95 320 192\n");
    if (second) fprintf(file, "94 320 192\n");
    fclose(file);
    if (kms_read_snapshot(kms, &snapshot) < 0) exit(2);
    kms_reconcile_snapshot(kms, &snapshot);
}

int main(int argc, char **argv)
{
    BlitKms *kms;
    char state_path[512], trace_path[512], device_path[512], line[128];
    unsigned primary_fb;
    int cycle, i, allocated = 0, released = 0;
    FILE *trace_file;
    if (argc != 2) return 2;
    snprintf(state_path, sizeof(state_path), "%s/state", argv[1]);
    snprintf(trace_path, sizeof(trace_path), "%s/trace", argv[1]);
    snprintf(device_path, sizeof(device_path), "%s/device", argv[1]);
    setenv("BLIT_DRM_MOCK_STATE", state_path, 1);
    setenv("BLIT_DRM_MOCK_TRACE", trace_path, 1);
    kms = blit_kms_new(NULL, NULL);
    if (!kms) return 2;
    kms->fd = open(device_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (kms->fd < 0) return 2;
    topology(kms, state_path, 0, 0);
    primary_fb = kms_output(kms, 0)->front.fb;
    for (cycle = 0; cycle < 64; ++cycle) {
        KmsFlipCookie *late, *current;
        KmsOutput *auxiliary = NULL;
        topology(kms, state_path, 1, 0);
        for (i = 0; i < kms->output_count; ++i)
            if (kms_output(kms, i)->connector == 94) auxiliary = kms_output(kms, i);
        if (!auxiliary) return 3;
        late = kms_flip_cookie(kms, auxiliary);
        if (!late) return 3;
        auxiliary->flip_pending = 1;
        topology(kms, state_path, 0, 0);
        topology(kms, state_path, 1, 1);
        kms_flip_event(-1, cycle, 1, 0, late);
        for (i = 0; i < kms->output_count; ++i) {
            KmsOutput *output = kms_output(kms, i);
            if (output->info.flip_completions) return 4;
            if (output->connector == 94) auxiliary = output;
        }
        current = kms_flip_cookie(kms, auxiliary);
        if (!current) return 4;
        auxiliary->flip_pending = 1;
        kms_flip_event(-1, cycle, 1, 1000, current);
        if (auxiliary->flip_pending || auxiliary->info.flip_completions != 1) return 5;
        topology(kms, state_path, 0, 0);
        if (kms->output_count != 1 || kms_output(kms, 0)->front.fb != primary_fb) return 6;
        for (i = 0; i < KMS_MAX_FLIP_COOKIES; ++i)
            if (kms->flip_cookies[i].used) return 7;
    }
    blit_kms_close(kms);
    trace_file = fopen(trace_path, "r");
    if (!trace_file) return 8;
    while (fgets(line, sizeof(line), trace_file)) {
        if (strncmp(line, "allocate ", 9) == 0) ++allocated;
        if (strncmp(line, "free ", 5) == 0) ++released;
    }
    fclose(trace_file);
    if (allocated != released) return 8;
    printf("KMS_HOTPLUG_CYCLES_PASS cycles=64 late_events=ignored current_events=accepted primary_buffers=unchanged buffers=%d/%d\n", allocated, released);
    return 0;
}
