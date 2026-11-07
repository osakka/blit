/* blit-gamescope's reading of its own terminal (ADR 0225, phase 1).

   The bridge does not forward keys or the mouse: Blit sends the pane's input
   to gamescope's EIS socket itself (the INPUT request, blit_gamescope_frames.h).
   The terminal tells the bridge only three things, and this unit recognises
   exactly those in the bytes Blit writes to the PTY, everything else being
   skipped whole (so a Kitty key, a mouse report or a pointer-lock delta is
   never misread):

   - `CSI 4 ; <height> ; <width> t`: the pane's size in pixels (XTWINOPS 14);
   - `CSI ? 7701 ; 2 $ y`: Blit ended a pointer lock (focus moved, the
     desktop's Release Pointer, a reset) or refused one;
   - `CSI O` / `CSI I`: focus left or reached the pane (mode 1004).

   The bridge never captures the mouse and turns on no mouse reporting.
   Capture belongs to the desktop, which locks the pointer and releases it
   with its own Release Pointer (ADR 0198); a bare Blit pane has no release
   key, so phase 1 has no capture. A lock a desktop takes still ends with the
   ?7701;2$y notice, which the bridge only logs. */
#ifndef BLIT_GAMESCOPE_TTY_H
#define BLIT_GAMESCOPE_TTY_H

#include <stddef.h>
#include <string.h>

enum gs_tty_event {
    GS_TTY_NONE = 0,
    GS_TTY_SIZE,         /* a, b: width, height in pixels */
    GS_TTY_LOCK_ENDED,   /* the ?7701;2$y notice */
    GS_TTY_FOCUS_OUT,
    GS_TTY_FOCUS_IN
};

typedef struct {
    unsigned char seq[64]; /* the escape sequence being read */
    size_t length;
    int in_escape;         /* 0 none, 1 after ESC, 2 in CSI, 3 in a string (APC/OSC/DCS) */
    int string_esc;        /* saw ESC inside a string */
} GsTty;

typedef void (*gs_tty_emit)(void *context, int event, int a, int b);

static void gs_tty_init(GsTty *t) { memset(t, 0, sizeof(*t)); }

static void gs_tty_csi(GsTty *t, gs_tty_emit emit, void *context)
{
    const char *s = (const char *)t->seq;   /* without ESC [, ends with the final */
    size_t n = t->length;
    char final = n ? s[n - 1] : 0;
    int p[4] = { 0, 0, 0, 0 }, count = 0;
    size_t i = 0;
    char lead = 0;
    if (n && (s[0] == '<' || s[0] == '?' || s[0] == '>' || s[0] == '=')) lead = s[i++];
    for (; i + 1 < n && count < 4; ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            if (p[count] < 100000) p[count] = p[count] * 10 + (c - '0');
        } else if (c == ';') {
            ++count;
        } else if (c == '-' || c == ':') {
            return; /* a signed pointer-lock delta or Kitty key sub-fields: not ours */
        }
    }
    ++count;
    if (!lead && final == 't' && p[0] == 4 && count >= 3) {
        emit(context, GS_TTY_SIZE, p[2], p[1]);
    } else if (!lead && final == 'O' && n == 1) {
        emit(context, GS_TTY_FOCUS_OUT, 0, 0);
    } else if (!lead && final == 'I' && n == 1) {
        emit(context, GS_TTY_FOCUS_IN, 0, 0);
    } else if (lead == '?' && final == 'y' && n >= 2 && s[n - 2] == '$' && p[0] == 7701) {
        if (p[1] == 2 || p[1] == 0)
            emit(context, GS_TTY_LOCK_ENDED, 0, 0);
    }
    /* Mouse reports (`CSI <`), should a desktop's lock send any, are skipped:
       Blit sends the pointer to gamescope itself. */
}

/* Feeds bytes read from the terminal; sequences may be split across calls. */
static void gs_tty_feed(GsTty *t, const unsigned char *data, size_t size,
                        gs_tty_emit emit, void *context)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        unsigned char c = data[i];
        switch (t->in_escape) {
        case 0:
            if (c == 0x1b) t->in_escape = 1;
            break;
        case 1:
            if (c == '[') { t->in_escape = 2; t->length = 0; }
            else if (c == '_' || c == ']' || c == 'P') { t->in_escape = 3; t->string_esc = 0; }
            else if (c == 0x1b) t->in_escape = 1;
            else t->in_escape = 0; /* ESC x: Alt+key in legacy mode, skipped */
            break;
        case 2:
            if (t->length < sizeof(t->seq)) t->seq[t->length++] = c;
            if (c >= 0x40 && c <= 0x7e) {
                if (t->length <= sizeof(t->seq)) gs_tty_csi(t, emit, context);
                t->in_escape = 0;
                t->length = 0;
            } else if (c == 0x1b) {
                t->in_escape = 1;
            }
            break;
        case 3:
            if (c == 0x07) t->in_escape = 0;
            else if (t->string_esc && c == '\\') t->in_escape = 0;
            t->string_esc = c == 0x1b;
            break;
        }
    }
}

#endif
