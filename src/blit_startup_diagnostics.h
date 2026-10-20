/* Startup-only, protected-trial diagnostics. No signal-handler allocation or
   unwinding: record module load addresses before a possible library fault. */
static void blit_startup_stage(const char *stage, unsigned long long display)
{
    if (blit_trial_id_valid(getenv("BLIT_PRESENTATION_TRIAL_ID")))
        fprintf(stderr,"blit: startup stage=%s display=%llu\n",stage,display);
}
static void blit_startup_modules(void)
{
    FILE *maps;
    char line[4096], permissions[8];
    if (!blit_trial_id_valid(getenv("BLIT_PRESENTATION_TRIAL_ID"))) return;
    maps=fopen("/proc/self/maps","r");
    if (!maps) { fprintf(stderr,"blit: startup maps unavailable\n"); return; }
    while (fgets(line,sizeof(line),maps))
        if (sscanf(line,"%*s %7s",permissions)==1 && strchr(permissions,'x'))
            fprintf(stderr,"blit: startup map=%s",line);
    fclose(maps);
}
