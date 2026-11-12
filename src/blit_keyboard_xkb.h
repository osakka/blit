/* Blit's keyboards through libxkbcommon (ADR 0231).
 *
 * What a key types, and what the modifiers and locks are, come from one
 * compiled xkb keymap -- the system's xkeyboard-config data, compiled by
 * name (rules evdev, model pc105) -- and, per keyboard, one xkb_state on it.
 * The same keymap's text is what GET /v1/input/keymap serves and what
 * blit-compositor hands to Wayland and X programs, and the same per-keyboard
 * state's modifiers go to it as `INPUT modifiers`, so a terminal, a Wayland
 * program and an X program agree on every key.
 *
 * A device's modifier roles (issue #66: Caps Lock as Control, a key that is
 * nothing) are applied before the state sees a key: the physical code is
 * translated to the role's own code (Control is Left Control, AltGr is the
 * keymap's <LVL3>, `none` is no key at all). The held set stays keyed by
 * the physical code, and a logical key goes down with the first physical
 * key holding it and up with the last, so a real Control and a remapped
 * Caps Lock held together release Control only when both are up.
 *
 * Modifiers are read by their real names, Shift, Control, Mod1 (Alt), Mod4
 * (Super), Mod5 (AltGr, xkeyboard-config's LevelThree), Lock and Mod2 (Num
 * Lock): libxkbcommon before 1.8 does not report a virtual modifier such as
 * "Alt" as active by name, and xkeyboard-config maps them this way in every
 * layout.
 *
 * Included once by blit_server.c, after the held set and the role lookup. */
#ifndef BLIT_KEYBOARD_XKB_H
#define BLIT_KEYBOARD_XKB_H

#include <xkbcommon/xkbcommon.h>

/* The keymap's <LVL3>, ISO_Level3_Shift in every pc-based layout: what the
   `altgr` role becomes, since Right Alt is Alt in a layout without a third
   level. */
#define BLIT_KEY_LEVEL3 84

enum {
    BLIT_XKB_SHIFT, BLIT_XKB_CONTROL, BLIT_XKB_ALT, BLIT_XKB_LEVEL3,
    BLIT_XKB_SUPER, BLIT_XKB_LOCK, BLIT_XKB_NUM, BLIT_XKB_MODS
};

static struct xkb_context *blit_xkb_context;
static struct xkb_keymap *blit_xkb_keymap;
static char *blit_xkb_text;
static size_t blit_xkb_text_length;
static xkb_mod_index_t blit_xkb_mod[BLIT_XKB_MODS];

/* The keyboards (ADR 0231 step 2): an ordered list, each compiled once, and
   the current one, whose keymap every keyboard's state is on. Switching
   swaps the keymap; each keyboard's state is made again on it the next
   time it is used, its locks kept. */
#define BLIT_KEYBOARDS_MAX 16
#define BLIT_KEYBOARD_NAME 48

typedef struct {
    char layout[BLIT_KEYBOARD_NAME];
    char variant[BLIT_KEYBOARD_NAME];
    struct xkb_keymap *keymap;
    char *text;
    size_t length;
} BlitKeyboard;

static BlitKeyboard blit_keyboards[BLIT_KEYBOARDS_MAX];
static int blit_keyboard_count;
static int blit_keyboard_current;
/* Set by a list other than the built-in `us` alone: what the settings
   document then keeps. */
static int blit_keyboards_configured;

/* A layout or variant name as xkb spells them: lower-case letters, digits,
   '_' and '-', at most BLIT_KEYBOARD_NAME - 1 ("" for no variant). */
static int blit_keyboard_name_ok(const char *name, int empty_ok)
{
    size_t i, n = strlen(name);
    if (n == 0) return empty_ok;
    if (n >= BLIT_KEYBOARD_NAME) return 0;
    for (i = 0; i < n; ++i)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-'))
            return 0;
    return 1;
}

/* One keyboard compiled from the system's data: its keymap and its text. */
static int blit_xkb_compile(const char *layout, const char *variant, struct xkb_keymap **keymap,
                            char **text, char *problem, size_t capacity)
{
    struct xkb_rule_names rules;
    if (blit_xkb_context == NULL)
        blit_xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (blit_xkb_context == NULL) {
        snprintf(problem, capacity, "libxkbcommon could not make a context");
        return 0;
    }
    memset(&rules, 0, sizeof(rules));
    rules.rules = "evdev";
    rules.model = "pc105";
    rules.layout = layout;
    rules.variant = variant;
    rules.options = "";
    *keymap = xkb_keymap_new_from_names(blit_xkb_context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (*keymap == NULL) {
        snprintf(problem, capacity,
                 "no xkb keymap for layout \"%s\" variant \"%s\" (is xkb-data installed?)",
                 layout, variant);
        return 0;
    }
    *text = xkb_keymap_get_as_string(*keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (*text == NULL) {
        xkb_keymap_unref(*keymap);
        *keymap = NULL;
        snprintf(problem, capacity, "the xkb keymap could not be written as text");
        return 0;
    }
    return 1;
}

/* Keyboard `index` the one keys are read with: its keymap, its text, the
   modifier indices on it. */
static void blit_keyboards_use(int index)
{
    static const char *const names[BLIT_XKB_MODS] = {
        XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CTRL, XKB_MOD_NAME_ALT, "Mod5",
        XKB_MOD_NAME_LOGO, XKB_MOD_NAME_CAPS, XKB_MOD_NAME_NUM
    };
    int i;
    blit_keyboard_current = index;
    blit_xkb_keymap = blit_keyboards[index].keymap;
    blit_xkb_text = blit_keyboards[index].text;
    blit_xkb_text_length = blit_keyboards[index].length;
    for (i = 0; i < BLIT_XKB_MODS; ++i)
        blit_xkb_mod[i] = xkb_keymap_mod_get_index(blit_xkb_keymap, names[i]);
}

/* The list replaced: `n` keyboards by `layouts` and `variants`, `current`
   the one in use. Each is compiled first; any that does not compile (or a
   name xkb does not spell) leaves the list in use as it was and says which
   in `problem`. Returns 1 when replaced. */
static int blit_keyboards_set(char layouts[][BLIT_KEYBOARD_NAME], char variants[][BLIT_KEYBOARD_NAME],
                              int n, int current, char *problem, size_t capacity)
{
    static BlitKeyboard fresh[BLIT_KEYBOARDS_MAX];
    int i, k;
    if (n < 1 || n > BLIT_KEYBOARDS_MAX) {
        snprintf(problem, capacity, "keyboards: from 1 to %d, not %d", BLIT_KEYBOARDS_MAX, n);
        return 0;
    }
    if (current < 0 || current >= n) {
        snprintf(problem, capacity, "current: %d is not one of the %d keyboards", current, n);
        return 0;
    }
    memset(fresh, 0, sizeof(fresh));
    for (i = 0; i < n; ++i) {
        if (!blit_keyboard_name_ok(layouts[i], 0) || !blit_keyboard_name_ok(variants[i], 1)) {
            snprintf(problem, capacity, "keyboard %d: layout and variant are xkb names "
                     "(a-z, 0-9, _ and -)", i);
            for (k = 0; k < i; ++k) {
                xkb_keymap_unref(fresh[k].keymap);
                free(fresh[k].text);
            }
            return 0;
        }
        snprintf(fresh[i].layout, sizeof(fresh[i].layout), "%s", layouts[i]);
        snprintf(fresh[i].variant, sizeof(fresh[i].variant), "%s", variants[i]);
        if (!blit_xkb_compile(layouts[i], variants[i], &fresh[i].keymap, &fresh[i].text,
                              problem, capacity)) {
            for (k = 0; k < i; ++k) {
                xkb_keymap_unref(fresh[k].keymap);
                free(fresh[k].text);
            }
            return 0;
        }
        fresh[i].length = strlen(fresh[i].text);
    }
    for (i = 0; i < blit_keyboard_count; ++i) {
        xkb_keymap_unref(blit_keyboards[i].keymap);
        free(blit_keyboards[i].text);
    }
    memcpy(blit_keyboards, fresh, sizeof(fresh));
    blit_keyboard_count = n;
    blit_keyboards_configured = !(n == 1 && strcmp(layouts[0], "us") == 0 && variants[0][0] == '\0');
    blit_keyboards_use(current);
    return 1;
}

/* The built-in list, `us` alone: what Blit starts with. */
static int blit_keyboards_default(char *problem, size_t capacity)
{
    char layouts[1][BLIT_KEYBOARD_NAME] = { "us" }, variants[1][BLIT_KEYBOARD_NAME] = { "" };
    return blit_keyboards_set(layouts, variants, 1, 0, problem, capacity);
}

/* The system's list of keyboards, xkeyboard-config's rules/evdev.lst, in
   the first of libxkbcommon's include paths that has one; NULL without. */
static FILE *blit_xkb_rules_list(void)
{
    char path[1024];
    unsigned i, n;
    if (blit_xkb_context == NULL)
        blit_xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (blit_xkb_context == NULL)
        return NULL;
    n = xkb_context_num_include_paths(blit_xkb_context);
    for (i = 0; i < n; ++i) {
        FILE *file;
        snprintf(path, sizeof(path), "%s/rules/evdev.lst",
                 xkb_context_include_path_get(blit_xkb_context, i));
        if ((file = fopen(path, "r")) != NULL)
            return file;
    }
    return NULL;
}

/* One line of a rules list's section: its name and its description (a
   variant's after its "layout: "). Returns 0 for a blank or other line. */
static int blit_xkb_list_line(char *line, char **name, char **layout, char **text)
{
    char *p = line, *end;
    *layout = NULL;
    if (line[0] != ' ') return 0;
    while (*p == ' ') ++p;
    *name = p;
    while (*p && *p != ' ' && *p != '\n') ++p;
    if (*p == '\0' || *p == '\n') return 0;
    *p++ = '\0';
    while (*p == ' ') ++p;
    end = p + strlen(p);
    while (end > p && (end[-1] == '\n' || end[-1] == '\r')) *--end = '\0';
    *text = p;
    return **name != '\0';
}

/* The system's description of a keyboard ("Arabic (Macintosh,
   phonetic)"), or its own names when the list has none. */
static void blit_keyboard_description(const char *layout, const char *variant, char *out,
                                      size_t capacity)
{
    char line[512], *name, *text, *ignored;
    int section = 0;
    FILE *file = blit_xkb_rules_list();
    snprintf(out, capacity, "%s%s%s", layout, variant[0] ? " " : "", variant);
    if (file == NULL)
        return;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '!') {
            section = strncmp(line, "! layout", 8) == 0 ? 1 : strncmp(line, "! variant", 9) == 0 ? 2 : 0;
            continue;
        }
        if (!blit_xkb_list_line(line, &name, &ignored, &text))
            continue;
        if (section == 1 && variant[0] == '\0' && strcmp(name, layout) == 0) {
            snprintf(out, capacity, "%s", text);
            break;
        }
        if (section == 2 && variant[0] != '\0' && strcmp(name, variant) == 0) {
            size_t n = strlen(layout);
            if (strncmp(text, layout, n) == 0 && text[n] == ':' && text[n + 1] == ' ') {
                snprintf(out, capacity, "%s", text + n + 2);
                break;
            }
        }
    }
    fclose(file);
}

/* The code a role makes a key: the key the state is told about. -1 for
   `none`, which is no key. */
static int input_role_code(int role)
{
    switch (role) {
    case BLIT_MODIFIER_SHIFT: return KEY_LEFTSHIFT;
    case BLIT_MODIFIER_CTRL: return KEY_LEFTCTRL;
    case BLIT_MODIFIER_ALT: return KEY_LEFTALT;
    case BLIT_MODIFIER_ALTGR: return BLIT_KEY_LEVEL3;
    case BLIT_MODIFIER_META: return KEY_LEFTMETA;
    case BLIT_MODIFIER_CAPS_LOCK: return KEY_CAPSLOCK;
    case BLIT_MODIFIER_NUM_LOCK: return KEY_NUMLOCK;
    default: return -1;
    }
}

/* The logical code of a physical one on this device: itself, or its role's;
   -1 for a key that is nothing. */
static int input_logical_code(const BlitInputDevice *device, unsigned short code)
{
    int role = input_device_modifier_role(device, code);
    return role == BLIT_MODIFIER_UNMAPPED ? (int)code : input_role_code(role);
}

/* How many held physical keys other than `except` hold `logical`. */
static int input_logical_holders(const BlitInputDevice *device, int logical,
                                 unsigned short except)
{
    int count = 0, i;
    if (logical >= 0 && logical <= KEY_MAX && (unsigned short)logical != except &&
        input_device_modifier_role(device, (unsigned short)logical) == BLIT_MODIFIER_UNMAPPED &&
        evdev_key_is_held(device, (unsigned short)logical))
        ++count;
    for (i = 0; i < device->modifier_count; ++i)
        if (device->modifier_codes[i] != except &&
            input_role_code(device->modifier_roles[i]) == logical &&
            evdev_key_is_held(device, device->modifier_codes[i]))
            ++count;
    return count;
}

static int blit_xkb_active(struct xkb_state *state, int which, enum xkb_state_component type)
{
    xkb_mod_index_t index = blit_xkb_mod[which];
    return state != NULL && index != XKB_MOD_INVALID &&
        xkb_state_mod_index_is_active(state, index, type) > 0;
}

/* This keyboard's flags, read from its state: the modifiers held (or
   latched) and the locks on. The state is the one source; the flags are
   what the encoders read. */
static void input_device_xkb_flags(BlitInputDevice *device)
{
    struct xkb_state *state = device->xkb_state;
    enum xkb_state_component held = XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED;
    device->shift = blit_xkb_active(state, BLIT_XKB_SHIFT, held);
    device->ctrl = blit_xkb_active(state, BLIT_XKB_CONTROL, held);
    device->alt = blit_xkb_active(state, BLIT_XKB_ALT, held);
    device->altgr = blit_xkb_active(state, BLIT_XKB_LEVEL3, held);
    if (state != NULL) {
        device->caps_lock = blit_xkb_active(state, BLIT_XKB_LOCK, XKB_STATE_MODS_LOCKED);
        device->num_lock = blit_xkb_active(state, BLIT_XKB_NUM, XKB_STATE_MODS_LOCKED);
    }
}

/* Set this keyboard's Caps Lock and Num Lock in its state (a boot setting,
   or the kernel's LEDs adopted), the held modifiers kept. */
static void input_device_xkb_locks(BlitInputDevice *device, int caps, int num)
{
    struct xkb_state *state = device->xkb_state;
    xkb_mod_mask_t locked, caps_bit = 0, num_bit = 0;
    if (state == NULL) {
        device->caps_lock = caps;
        device->num_lock = num;
        return;
    }
    if (blit_xkb_mod[BLIT_XKB_LOCK] != XKB_MOD_INVALID) caps_bit = 1u << blit_xkb_mod[BLIT_XKB_LOCK];
    if (blit_xkb_mod[BLIT_XKB_NUM] != XKB_MOD_INVALID) num_bit = 1u << blit_xkb_mod[BLIT_XKB_NUM];
    locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED) & ~(caps_bit | num_bit);
    if (caps) locked |= caps_bit;
    if (num) locked |= num_bit;
    xkb_state_update_mask(state,
                          xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
                          xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
                          locked,
                          xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_DEPRESSED),
                          xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LATCHED),
                          xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_LOCKED));
    input_device_xkb_flags(device);
}

/* This keyboard's state made again on the keymap in use: its held keys
   (each logical key once) and its locks as its flags have them. */
static void input_device_xkb_rebuild(BlitInputDevice *device)
{
    static unsigned char seen[(KEY_MAX + 8) / 8];
    int caps = device->caps_lock, num = device->num_lock;
    unsigned int code;
    if (device->xkb_state != NULL)
        xkb_state_unref(device->xkb_state);
    device->xkb_state = blit_xkb_keymap != NULL ? xkb_state_new(blit_xkb_keymap) : NULL;
    device->xkb_keymap = blit_xkb_keymap;
    if (device->xkb_state == NULL) {
        input_device_xkb_flags(device);
        return;
    }
    memset(seen, 0, sizeof(seen));
    for (code = 0; code <= KEY_MAX; ++code) {
        int logical;
        if (!evdev_key_is_held(device, (unsigned short)code))
            continue;
        logical = input_logical_code(device, (unsigned short)code);
        if (logical < 0 || (seen[logical / 8] & (1u << (logical % 8))))
            continue;
        seen[logical / 8] |= (unsigned char)(1u << (logical % 8));
        xkb_state_update_key(device->xkb_state, (xkb_keycode_t)logical + 8, XKB_KEY_DOWN);
    }
    input_device_xkb_locks(device, caps, num);
}

/* This keyboard's state, made (or made again, after the keymap changed)
   when it is first needed. NULL only without a keymap. */
static struct xkb_state *input_device_xkb(BlitInputDevice *device)
{
    if (device->xkb_state == NULL || device->xkb_keymap != blit_xkb_keymap)
        input_device_xkb_rebuild(device);
    return device->xkb_state;
}

/* Kept as the name every reader uses: the flags from the state. */
static void evdev_refresh_modifiers(BlitInputDevice *device)
{
    input_device_xkb(device);
    input_device_xkb_flags(device);
}

/* A physical press or release told to this keyboard's state, before the
   held set records it. Returns the logical code that went down or up --
   what a compositor is told -- or -1 when nothing did: a key that is
   nothing, or a logical key another physical key still holds. */
static int input_device_xkb_key(BlitInputDevice *device, unsigned short code, int pressed)
{
    int logical = input_logical_code(device, code);
    struct xkb_state *state;
    if (logical < 0 || input_logical_holders(device, logical, code) > 0)
        return -1;
    state = input_device_xkb(device);
    if (state != NULL)
        xkb_state_update_key(state, (xkb_keycode_t)logical + 8,
                             pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    input_device_xkb_flags(device);
    return logical;
}

/* This keyboard's modifiers as a compositor states them: depressed,
   latched, locked, and the layout group. */
static void input_device_xkb_serialize(BlitInputDevice *device, unsigned int out[4])
{
    struct xkb_state *state = input_device_xkb(device);
    out[0] = out[1] = out[2] = out[3] = 0;
    if (state == NULL)
        return;
    out[0] = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    out[1] = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    out[2] = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    out[3] = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
}

static int evdev_super_held(const BlitInputDevice *keyboard)
{
    return blit_xkb_active(keyboard->xkb_state, BLIT_XKB_SUPER,
                           XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}

static int blit_key_is_keypad(unsigned short code)
{
    switch (code) {
    case KEY_KP0: case KEY_KP1: case KEY_KP2: case KEY_KP3: case KEY_KP4:
    case KEY_KP5: case KEY_KP6: case KEY_KP7: case KEY_KP8: case KEY_KP9:
    case KEY_KPDOT: case KEY_KPSLASH: case KEY_KPASTERISK: case KEY_KPMINUS:
    case KEY_KPPLUS: case KEY_KPENTER: case KEY_KPEQUAL: case KEY_KPCOMMA:
    case KEY_KPPLUSMINUS: case KEY_KPJPCOMMA: case KEY_KPLEFTPAREN:
    case KEY_KPRIGHTPAREN:
        return 1;
    default:
        return 0;
    }
}

/* What this key types on this keyboard now, as UTF-8 into `out` with its
   length, or 0 when it types nothing: from the keysym the state gives it
   (Shift, AltGr, Caps Lock as the layout has them), not
   xkb_state_key_get_utf8, which applies the Control transformation. A
   control character is not text, and the keypad's text is the keypad
   encoders' own (its digits depend on Num Lock and the application keypad
   mode). */
static int evdev_key_character(const BlitInputDevice *device,
                               unsigned short code, char *out, size_t capacity)
{
    int logical, length;
    xkb_keysym_t sym;
    uint32_t scalar;
    if (capacity == 0)
        return 0;
    out[0] = '\0';
    if (device->xkb_state == NULL || blit_key_is_keypad(code))
        return 0;
    logical = input_logical_code(device, code);
    if (logical < 0)
        return 0;
    sym = xkb_state_key_get_one_sym(device->xkb_state, (xkb_keycode_t)logical + 8);
    scalar = xkb_keysym_to_utf32(sym);
    if (scalar < 32 || scalar == 127 || (scalar >= 128 && scalar < 160))
        return 0;
    length = xkb_keysym_to_utf8(sym, out, capacity);
    if (length <= 0) {
        out[0] = '\0';
        return 0;
    }
    return (int)strlen(out);
}

/* The character on a level of this key in the keymap (0 plain, 1 Shift),
   in the layout the keyboard is in, or 0. Kitty's key code and its shifted
   key. */
static uint32_t input_xkb_level(const BlitInputDevice *device, unsigned short code, int level)
{
    const xkb_keysym_t *syms;
    xkb_layout_index_t layout = 0;
    int logical = input_logical_code(device, code);
    if (blit_xkb_keymap == NULL || logical < 0)
        return 0;
    if (device->xkb_state != NULL)
        layout = xkb_state_key_get_layout(device->xkb_state, (xkb_keycode_t)logical + 8);
    if (layout == XKB_LAYOUT_INVALID)
        layout = 0;
    if (xkb_keymap_key_get_syms_by_level(blit_xkb_keymap, (xkb_keycode_t)logical + 8,
                                         layout, (xkb_level_index_t)level, &syms) != 1)
        return 0;
    return xkb_keysym_to_utf32(syms[0]);
}

/* Kitty's code for a modifier or lock key, by the keysym the keymap gives
   the key (so Right Alt is Right Alt in `us` and ISO_Level3_Shift where the
   layout makes it AltGr); 0 for a key that is neither. */
static unsigned int input_xkb_modifier_key(const BlitInputDevice *device, unsigned short code)
{
    const xkb_keysym_t *syms;
    int logical = input_logical_code(device, code);
    if (blit_xkb_keymap == NULL || logical < 0 ||
        xkb_keymap_key_get_syms_by_level(blit_xkb_keymap, (xkb_keycode_t)logical + 8,
                                         0, 0, &syms) != 1)
        return 0;
    switch (syms[0]) {
    case XKB_KEY_Shift_L: return 57441;
    case XKB_KEY_Control_L: return 57442;
    case XKB_KEY_Alt_L: return 57443;
    case XKB_KEY_Super_L: return 57444;
    case XKB_KEY_Hyper_L: return 57445;
    case XKB_KEY_Meta_L: return 57446;
    case XKB_KEY_Shift_R: return 57447;
    case XKB_KEY_Control_R: return 57448;
    case XKB_KEY_Alt_R: return 57449;
    case XKB_KEY_Super_R: return 57450;
    case XKB_KEY_Hyper_R: return 57451;
    case XKB_KEY_Meta_R: return 57452;
    case XKB_KEY_ISO_Level3_Shift: return 57453;
    case XKB_KEY_ISO_Level5_Shift: return 57454;
    case XKB_KEY_Caps_Lock: return 57358;
    case XKB_KEY_Num_Lock: return 57360;
    default: return 0;
    }
}

/* The keymap as text (and its length), for GET /v1/input/keymap and for
   blit-compositor's `INPUT keymap`; NULL without one. */
static const char *blit_xkb_keymap_text(size_t *length)
{
    *length = blit_xkb_text_length;
    return blit_xkb_text;
}

#endif
