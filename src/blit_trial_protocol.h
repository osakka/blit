#ifndef BLIT_TRIAL_PROTOCOL_H
#define BLIT_TRIAL_PROTOCOL_H
#define BLIT_TRIAL_MIN_SECONDS 30
#define BLIT_TRIAL_MAX_SECONDS 120
#define BLIT_TRIAL_MAX_DOCUMENT 65535
#define BLIT_TRIAL_API_PATH "/v1/control/presentation-trial"
static int blit_trial_id_valid(const char *id)
{
    int i;
    if (!id) return 0;
    for (i = 0; i < 32; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return 0;
    return id[32] == '\0';
}
#endif
