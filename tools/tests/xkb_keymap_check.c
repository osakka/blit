/* ADR 0228 phase 1, milestone B: compiles a keymap from GET
   /v1/input/keymap with libxkbcommon, as blit-compositor and every Wayland
   client do, and checks what keys type.

     xkb_keymap_check <keymap file> [<code> <held> <expect>]...

   <code> is an evdev key code; <held> is a comma list of evdev codes
   pressed first, in order, and left down (`-` for none); <expect> is the
   UTF-8 the key types then, or `mod:<modifier>` (an xkb modifier name:
   Shift, Lock, Control, Mod1 ... Mod5) for one that must be in effect once
   the key is down, or `sym:<keysym name>` for the key's keysym. Each check starts
   from a fresh state. Prints XKB_KEYMAP_CHECK_OK <checks> <leds> <keys>, or a
   XKB_KEYMAP_CHECK_FAIL line per failure and exits 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long size;
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || (text = malloc((size_t)size + 1)) == NULL) {
        if (f) fclose(f);
        return NULL;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(text);
        return NULL;
    }
    text[size] = '\0';
    fclose(f);
    return text;
}

int main(int argc, char **argv)
{
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    char *text;
    int i, failures = 0, checks = 0;
    xkb_keycode_t key, keys = 0;

    if (argc < 2 || (argc - 2) % 3 != 0) {
        fprintf(stderr, "usage: xkb_keymap_check <keymap file> [<code> <held> <expect>]...\n");
        return 2;
    }
    text = slurp(argv[1]);
    if (text == NULL) {
        fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL cannot read %s\n", argv[1]);
        return 1;
    }
    context = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
    keymap = context ? xkb_keymap_new_from_string(context, text, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS)
                     : NULL;
    if (keymap == NULL) {
        fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL libxkbcommon did not compile the keymap\n");
        return 1;
    }
    for (key = xkb_keymap_min_keycode(keymap); key <= xkb_keymap_max_keycode(keymap); ++key)
        if (xkb_keymap_num_layouts_for_key(keymap, key) > 0) ++keys;
    /* The two locks' indicators: named, off at first, and lit by a press
       and release of the key that carries the lock's keysym, when a key
       does (a remap may take it away). A keymap without indicators crashes
       Xwayland 24.1 (ADR 0228; xkeyboard-config's
       keymaps carry them, ADR 0231). */
    {
        static const struct { const char *name; xkb_keysym_t sym; } leds[] = {
            { "Caps Lock", XKB_KEY_Caps_Lock }, { "Num Lock", XKB_KEY_Num_Lock }
        };
        unsigned k;
        for (k = 0; k < sizeof(leds) / sizeof(leds[0]); ++k) {
            struct xkb_state *state = xkb_state_new(keymap);
            xkb_keycode_t lock = XKB_KEYCODE_INVALID, c;
            ++checks;
            if (xkb_keymap_led_get_index(keymap, leds[k].name) == XKB_LED_INVALID) {
                fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL no indicator \"%s\"\n", leds[k].name);
                ++failures;
                xkb_state_unref(state);
                continue;
            }
            if (xkb_state_led_name_is_active(state, leds[k].name) != 0) {
                fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL indicator \"%s\" is lit with nothing locked\n",
                        leds[k].name);
                ++failures;
            }
            for (c = xkb_keymap_min_keycode(keymap); c <= xkb_keymap_max_keycode(keymap) &&
                 lock == XKB_KEYCODE_INVALID; ++c) {
                const xkb_keysym_t *syms;
                if (xkb_keymap_key_get_syms_by_level(keymap, c, 0, 0, &syms) == 1 && syms[0] == leds[k].sym)
                    lock = c;
            }
            if (lock != XKB_KEYCODE_INVALID) {
                xkb_state_update_key(state, lock, XKB_KEY_DOWN);
                xkb_state_update_key(state, lock, XKB_KEY_UP);
                if (xkb_state_led_name_is_active(state, leds[k].name) != 1) {
                    fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL indicator \"%s\" is not lit after key %u\n",
                            leds[k].name, (unsigned)lock - 8);
                    ++failures;
                }
            }
            xkb_state_unref(state);
        }
    }
    for (i = 2; i + 2 < argc; i += 3) {
        struct xkb_state *state = xkb_state_new(keymap);
        xkb_keycode_t code = (xkb_keycode_t)atoi(argv[i]) + 8;
        const char *held = argv[i + 1], *expect = argv[i + 2];
        char got[64];
        ++checks;
        if (strcmp(held, "-") != 0) {
            const char *p = held;
            while (*p) {
                xkb_state_update_key(state, (xkb_keycode_t)atoi(p) + 8, XKB_KEY_DOWN);
                p += strcspn(p, ",");
                if (*p == ',') ++p;
            }
        }
        if (strncmp(expect, "mod:", 4) == 0) {
            xkb_state_update_key(state, code, XKB_KEY_DOWN);
            if (xkb_state_mod_name_is_active(state, expect + 4, XKB_STATE_MODS_EFFECTIVE) != 1) {
                fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL key %s does not put %s in effect\n", argv[i],
                        expect + 4);
                ++failures;
            }
        } else if (strncmp(expect, "sym:", 4) == 0) {
            xkb_keysym_t sym = xkb_state_key_get_one_sym(state, code);
            xkb_keysym_get_name(sym, got, sizeof(got));
            if (strcmp(got, expect + 4) != 0) {
                fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL key %s held %s is %s, not %s\n", argv[i],
                        held, got, expect + 4);
                ++failures;
            }
        } else {
            xkb_state_key_get_utf8(state, code, got, sizeof(got));
            if (strcmp(got, expect) != 0) {
                fprintf(stderr, "XKB_KEYMAP_CHECK_FAIL key %s held %s types \"%s\", not \"%s\"\n",
                        argv[i], held, got, expect);
                ++failures;
            }
        }
        xkb_state_unref(state);
    }
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    free(text);
    if (failures) return 1;
    printf("XKB_KEYMAP_CHECK_OK checks=%d leds=caps,num keys=%u\n", checks, (unsigned)keys);
    return 0;
}
