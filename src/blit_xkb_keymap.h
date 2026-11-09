/* GET /v1/input/keymap (ADR 0228 phase 1, milestone B): Blit's keyboard as
   a complete xkb keymap, text (`xkb_v1`), for blit-compositor to compile
   with libxkbcommon and hand to its clients.

   One source. Every character a key types at each level is asked of
   evdev_key_character -- the function Blit's own terminal input uses --
   with the device's Shift and AltGr set as that level needs, so the keymap
   is the device's sparse overrides on the built-in US table exactly as Blit
   reads them; and every key with a modifier role (the device's `modifiers`
   remap first, the role a code has by default otherwise) gets that role's
   keysym, so CapsLock remapped to Control is Control_L here as it is for
   Blit. Nothing here is a second table of characters.

   Shape:
   - xkb_keycodes: <Kn> = n + 8 for each evdev code n that does anything.
   - xkb_types: ONE_LEVEL; TWO_LEVEL (Shift); ALPHABETIC (Shift or Lock,
     not both: Blit's "CapsLock inverts Shift for a lower-case letter");
     FOUR_LEVEL and FOUR_LEVEL_ALPHABETIC (Mod5, AltGr, selects the third
     and with Shift the fourth level; Lock does not reach them, as in
     evdev_key_character).
   - xkb_compatibility: the modifier keysyms set Shift, Control, Mod1 (Alt),
     Mod4 (Super), Mod5 (LevelThree); Caps_Lock and Num_Lock lock Lock and
     Mod2.
   - xkb_symbols: each key's levels as keysyms -- ASCII by name, anything
     else as U<hex> -- and the modifier map by keysym.

   A level of more than one character (an override may hold up to four
   bytes) is its first character: an xkb level is one keysym. Keys Blit
   types nothing for and gives no role (the 102nd key without an override,
   multimedia keys) are left out; the keypad is its Num Lock-on keysyms
   (KP_0..KP_9, KP_Decimal) since Blit has no keypad navigation level.

   Included by blit_server.c after evdev_key_character and the modifier
   roles; header-only and static like Blit's other units. */
#ifndef BLIT_XKB_KEYMAP_H
#define BLIT_XKB_KEYMAP_H

/* The keysym name of a printable ASCII character. */
static const char *xkb_ascii_name(unsigned char c)
{
    static const char *const names[95] = {
        "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
        "apostrophe", "parenleft", "parenright", "asterisk", "plus", "comma", "minus",
        "period", "slash", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "colon",
        "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E",
        "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",
        "V", "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright",
        "asciicircum", "underscore", "grave", "a", "b", "c", "d", "e", "f", "g", "h", "i",
        "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y",
        "z", "braceleft", "bar", "braceright", "asciitilde"
    };
    return c >= 0x20 && c <= 0x7e ? names[c - 0x20] : NULL;
}

/* The keysym for the first character of `utf8` into `out`; "NoSymbol" for
   none or an invalid sequence. */
static void xkb_keysym_of(const char *utf8, char *out, size_t capacity)
{
    size_t consumed = 0;
    uint32_t scalar = 0;
    const char *name;
    if (utf8 == NULL || utf8[0] == '\0' ||
        !utf8_decode((const unsigned char *)utf8, strlen(utf8), &consumed, &scalar)) {
        snprintf(out, capacity, "NoSymbol");
        return;
    }
    if (scalar < 0x80 && (name = xkb_ascii_name((unsigned char)scalar)) != NULL)
        snprintf(out, capacity, "%s", name);
    else if (scalar >= 0xa0)
        snprintf(out, capacity, "U%04X", (unsigned)scalar);
    else
        snprintf(out, capacity, "NoSymbol");
}

/* A key that is not a character, by evdev code: its keysym, or NULL. The
   modifier keys are here by default; a role from the device's remap
   replaces them (xkb_role_keysym). */
static const char *xkb_special_keysym(unsigned short code)
{
    switch (code) {
    case KEY_ESC: return "Escape";
    case KEY_BACKSPACE: return "BackSpace";
    case KEY_TAB: return "Tab";
    case KEY_ENTER: return "Return";
    case KEY_LEFTCTRL: return "Control_L";
    case KEY_RIGHTCTRL: return "Control_R";
    case KEY_LEFTSHIFT: return "Shift_L";
    case KEY_RIGHTSHIFT: return "Shift_R";
    case KEY_LEFTALT: return "Alt_L";
    /* Blit reads the right Alt as AltGr (evdev_refresh_modifiers). */
    case KEY_RIGHTALT: return "ISO_Level3_Shift";
    case KEY_LEFTMETA: return "Super_L";
    case KEY_RIGHTMETA: return "Super_R";
    case KEY_CAPSLOCK: return "Caps_Lock";
    case KEY_NUMLOCK: return "Num_Lock";
    case KEY_SCROLLLOCK: return "Scroll_Lock";
    case KEY_SYSRQ: return "Print";
    case KEY_PAUSE: return "Pause";
    case KEY_COMPOSE: return "Menu";
    case KEY_MENU: return "Menu";
    case KEY_F1: return "F1";
    case KEY_F2: return "F2";
    case KEY_F3: return "F3";
    case KEY_F4: return "F4";
    case KEY_F5: return "F5";
    case KEY_F6: return "F6";
    case KEY_F7: return "F7";
    case KEY_F8: return "F8";
    case KEY_F9: return "F9";
    case KEY_F10: return "F10";
    case KEY_F11: return "F11";
    case KEY_F12: return "F12";
    case KEY_HOME: return "Home";
    case KEY_END: return "End";
    case KEY_PAGEUP: return "Prior";
    case KEY_PAGEDOWN: return "Next";
    case KEY_INSERT: return "Insert";
    case KEY_DELETE: return "Delete";
    case KEY_UP: return "Up";
    case KEY_DOWN: return "Down";
    case KEY_LEFT: return "Left";
    case KEY_RIGHT: return "Right";
    case KEY_KP0: return "KP_0";
    case KEY_KP1: return "KP_1";
    case KEY_KP2: return "KP_2";
    case KEY_KP3: return "KP_3";
    case KEY_KP4: return "KP_4";
    case KEY_KP5: return "KP_5";
    case KEY_KP6: return "KP_6";
    case KEY_KP7: return "KP_7";
    case KEY_KP8: return "KP_8";
    case KEY_KP9: return "KP_9";
    case KEY_KPDOT: return "KP_Decimal";
    case KEY_KPENTER: return "KP_Enter";
    case KEY_KPPLUS: return "KP_Add";
    case KEY_KPMINUS: return "KP_Subtract";
    case KEY_KPASTERISK: return "KP_Multiply";
    case KEY_KPSLASH: return "KP_Divide";
    default: return NULL;
    }
}

/* A remapped code's role as its keysym; "NoSymbol" for `none`. */
static const char *xkb_role_keysym(int role)
{
    switch (role) {
    case BLIT_MODIFIER_SHIFT: return "Shift_L";
    case BLIT_MODIFIER_CTRL: return "Control_L";
    case BLIT_MODIFIER_ALT: return "Alt_L";
    case BLIT_MODIFIER_ALTGR: return "ISO_Level3_Shift";
    case BLIT_MODIFIER_META: return "Super_L";
    case BLIT_MODIFIER_CAPS_LOCK: return "Caps_Lock";
    case BLIT_MODIFIER_NUM_LOCK: return "Num_Lock";
    default: return "NoSymbol";
    }
}

typedef struct {
    char *out;
    size_t capacity, length;
    int overflow;
} XkbText;

static void xkb_put(XkbText *t, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (t->overflow) return;
    va_start(ap, fmt);
    n = vsnprintf(t->out + t->length, t->capacity - t->length, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= t->capacity - t->length) {
        t->overflow = 1;
        return;
    }
    t->length += (size_t)n;
}

/* A keysym name, the longest here ISO_Level3_Shift. */
#define BLIT_XKB_KEYSYM_BYTES 32

/* The highest evdev code an xkb keycode (code + 8, at most 255) can hold. */
#define BLIT_XKB_LAST_CODE 247

/* What `code` is on `device`: its keysym at each of the four levels (plain,
   Shift, AltGr, Shift+AltGr), how many levels it has (0: nothing), and
   whether Lock inverts Shift for it. */
static int xkb_key_levels(const BlitInputDevice *device, unsigned short code,
                          char levels[4][BLIT_XKB_KEYSYM_BYTES], int *alphabetic)
{
    static BlitInputDevice scratch;
    char text[4][16];
    const char *special;
    int role = input_device_modifier_role(device, code), level;
    *alphabetic = 0;
    if (role != BLIT_MODIFIER_UNMAPPED) {
        if (role == BLIT_MODIFIER_NONE) return 0;
        snprintf(levels[0], BLIT_XKB_KEYSYM_BYTES, "%s", xkb_role_keysym(role));
        return 1;
    }
    /* The characters exactly as Blit types them, Lock off. */
    scratch = *device;
    scratch.caps_lock = 0;
    for (level = 0; level < 4; ++level) {
        scratch.shift = level & 1;
        scratch.altgr = level >> 1;
        evdev_key_character(&scratch, code, text[level], sizeof(text[level]));
    }
    if (text[0][0] == '\0' && text[1][0] == '\0' && text[2][0] == '\0' && text[3][0] == '\0') {
        special = xkb_special_keysym(code);
        if (special == NULL) return 0;
        snprintf(levels[0], BLIT_XKB_KEYSYM_BYTES, "%s", special);
        return 1;
    }
    for (level = 0; level < 4; ++level) xkb_keysym_of(text[level], levels[level], BLIT_XKB_KEYSYM_BYTES);
    *alphabetic = text[0][0] >= 'a' && text[0][0] <= 'z' && text[0][1] == '\0';
    if (strcmp(text[2], text[0]) == 0 && strcmp(text[3], text[1]) == 0)
        return strcmp(text[1], text[0]) == 0 ? 1 : 2;
    return 4;
}

/* The whole keymap for `device`. Returns its length, or 0 if it did not
   fit `capacity`. */
static size_t xkb_keymap_text(const BlitInputDevice *device, char *out, size_t capacity)
{
    XkbText t;
    char levels[4][BLIT_XKB_KEYSYM_BYTES];
    unsigned short code;
    int alphabetic, count;
    t.out = out;
    t.capacity = capacity;
    t.length = 0;
    t.overflow = 0;
    if (capacity == 0) return 0;
    out[0] = '\0';
    xkb_put(&t, "xkb_keymap {\n");
    xkb_put(&t, "xkb_keycodes \"blit\" {\n    minimum = 8;\n    maximum = 255;\n");
    for (code = 1; code <= BLIT_XKB_LAST_CODE; ++code)
        if (xkb_key_levels(device, code, levels, &alphabetic) > 0)
            xkb_put(&t, "    <K%u> = %u;\n", (unsigned)code, (unsigned)code + 8);
    xkb_put(&t, "};\n");
    xkb_put(&t,
            "xkb_types \"blit\" {\n"
            "    virtual_modifiers NumLock,Alt,Super,LevelThree;\n"
            "    type \"ONE_LEVEL\" { modifiers = none; map[none] = Level1; level_name[Level1] = \"Any\"; };\n"
            "    type \"TWO_LEVEL\" { modifiers = Shift; map[Shift] = Level2;\n"
            "        level_name[Level1] = \"Base\"; level_name[Level2] = \"Shift\"; };\n"
            "    type \"ALPHABETIC\" { modifiers = Shift+Lock; map[Shift] = Level2; map[Lock] = Level2;\n"
            "        level_name[Level1] = \"Base\"; level_name[Level2] = \"Caps\"; };\n"
            "    type \"FOUR_LEVEL\" { modifiers = Shift+Mod5; map[Shift] = Level2;\n"
            "        map[Mod5] = Level3; map[Shift+Mod5] = Level4;\n"
            "        level_name[Level1] = \"Base\"; level_name[Level2] = \"Shift\";\n"
            "        level_name[Level3] = \"AltGr\"; level_name[Level4] = \"Shift AltGr\"; };\n"
            "    type \"FOUR_LEVEL_ALPHABETIC\" { modifiers = Shift+Lock+Mod5; map[Shift] = Level2;\n"
            "        map[Lock] = Level2; map[Mod5] = Level3; map[Shift+Mod5] = Level4;\n"
            "        map[Lock+Mod5] = Level3; map[Shift+Lock+Mod5] = Level4;\n"
            "        level_name[Level1] = \"Base\"; level_name[Level2] = \"Shift\";\n"
            "        level_name[Level3] = \"AltGr\"; level_name[Level4] = \"Shift AltGr\"; };\n"
            "};\n");
    xkb_put(&t,
            "xkb_compatibility \"blit\" {\n"
            "    virtual_modifiers NumLock,Alt,Super,LevelThree;\n"
            "    interpret.useModMapMods = AnyLevel;\n"
            "    interpret.repeat = False;\n"
            "    interpret Shift_L { action = SetMods(modifiers = Shift); };\n"
            "    interpret Shift_R { action = SetMods(modifiers = Shift); };\n"
            "    interpret Control_L { action = SetMods(modifiers = Control); };\n"
            "    interpret Control_R { action = SetMods(modifiers = Control); };\n"
            "    interpret Alt_L { virtualModifier = Alt; action = SetMods(modifiers = Mod1); };\n"
            "    interpret Super_L { virtualModifier = Super; action = SetMods(modifiers = Mod4); };\n"
            "    interpret Super_R { virtualModifier = Super; action = SetMods(modifiers = Mod4); };\n"
            "    interpret ISO_Level3_Shift { virtualModifier = LevelThree; action = SetMods(modifiers = Mod5); };\n"
            "    interpret Caps_Lock { action = LockMods(modifiers = Lock); };\n"
            "    interpret Num_Lock { virtualModifier = NumLock; action = LockMods(modifiers = Mod2); };\n"
            "    interpret Any + AnyOf(all) { action = SetMods(modifiers = modMapMods, clearLocks); };\n"
            "    interpret Any { repeat = True; };\n"
            "};\n");
    xkb_put(&t, "xkb_symbols \"blit\" {\n");
    for (code = 1; code <= BLIT_XKB_LAST_CODE; ++code) {
        count = xkb_key_levels(device, code, levels, &alphabetic);
        if (count == 1)
            xkb_put(&t, "    key <K%u> { type = \"ONE_LEVEL\", [ %s ] };\n", (unsigned)code, levels[0]);
        else if (count == 2)
            xkb_put(&t, "    key <K%u> { type = \"%s\", [ %s, %s ] };\n", (unsigned)code,
                    alphabetic ? "ALPHABETIC" : "TWO_LEVEL", levels[0], levels[1]);
        else if (count == 4)
            xkb_put(&t, "    key <K%u> { type = \"%s\", [ %s, %s, %s, %s ] };\n", (unsigned)code,
                    alphabetic ? "FOUR_LEVEL_ALPHABETIC" : "FOUR_LEVEL", levels[0], levels[1],
                    levels[2], levels[3]);
    }
    xkb_put(&t,
            "    modifier_map Shift { Shift_L, Shift_R };\n"
            "    modifier_map Lock { Caps_Lock };\n"
            "    modifier_map Control { Control_L, Control_R };\n"
            "    modifier_map Mod1 { Alt_L };\n"
            "    modifier_map Mod2 { Num_Lock };\n"
            "    modifier_map Mod4 { Super_L, Super_R };\n"
            "    modifier_map Mod5 { ISO_Level3_Shift };\n"
            "};\n};\n");
    return t.overflow ? 0 : t.length;
}

#endif
