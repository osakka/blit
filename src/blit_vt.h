#ifndef BLIT_VT_H
#define BLIT_VT_H

/* One native Linux console owner per server. */
int blit_vt_open(char *error, unsigned capacity);
void blit_vt_close(void);
int blit_vt_owned(void);
int blit_vt_active(void);
int blit_vt_number(void);
/* 1=release requested, 2=acquire requested, -1=recovery process lost,
   -2=VT descriptor or keyboard isolation lost. */
int blit_vt_pending(void);
/* Caller relinquishes/reacquires DRM before acknowledging. */
int blit_vt_acknowledge(int acquire);
int blit_vt_switch(int number);
/* A privileged package install may legitimately block the loop longer than
   the normal heartbeat. This is a bounded lease, never watchdog disable. */
int blit_vt_update_lease(int enabled);
void blit_vt_json(char *body, unsigned capacity);
/* Name what the main loop is doing, for the watchdog's report if it stalls.
   A string literal only; cheap enough to call around every step. */
void blit_vt_phase(const char *phase);

#endif
