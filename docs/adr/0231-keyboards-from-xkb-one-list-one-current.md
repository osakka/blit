# ADR 0231: Keyboards from xkb: one list, one current keyboard

- Status: accepted (the owner's OK, 2026-11-12); implementing in three
  releases (below)
- Date: 2026-11-12
- Scope: what a key types, everywhere Blit delivers it (terminals through
  the kitty and legacy encoders, Wayland programs through blit-compositor's
  `wl_keyboard`, X programs through Xwayland); the device `keymap`
  configuration; `/v1/input/keymap`; the switcher contract with the desktop

## Context

Blit types with one built-in US table (`evdev_us_unshifted`,
`evdev_us_shifted`) plus a per-device `keymap`: sparse overrides of four
levels per key (`base` can only be `us`). It has no named layouts, no
variants, no dead keys or compose, and no second layout to switch to. The
compositor's xkb keymap (`/v1/input/keymap`, ADR 0228 phase 1) is
generated from those tables, so terminals, Wayland programs and X programs
agree today, but only on what the tables can say.

The owner wants:

- any layout a system has, for any language;
- keyboards chosen on the fly, from the menu bar, the Control Strip, or a
  shortcut;
- "type in English, it writes Arabic": a phonetic layout, such as xkb's
  `ara(mac-phonetic)` (Q types ق, S types س, B types ب);
- a Keyboard pane that lists whatever there is, with no Blit release for a
  new language.

The owner types US on both machines; the first non-US case is phonetic
Arabic, letter by letter (`ara(mac-phonetic)`), which the owner chose.

## Options considered

1. Grow Blit's own tables: a table per layout in Blit's source. Every
   language needs a Blit release, and dead keys, compose and the xkb
   keymap for the compositor would all be Blit's to write and keep in step.
   That would be a second copy of xkeyboard-config.
2. xkb in the desktop: Hibr compiles keymaps and sends Blit tables. That
   adds a second owner of what a key types, and the terminal encoder,
   the compositor and the desktop would each read their own copy.
3. xkb in Blit: the server compiles every keyboard from the system's
   xkeyboard-config data with libxkbcommon, which blit-compositor already
   links. It keeps one compiled keymap per keyboard and has one current
   keyboard. Every path reads the text a key types from that keymap's
   state.

## Decision

Option 3.

- **The list.** The server setting `keyboards` is an ordered list of
  `{layout, variant, options}` names, as xkb spells them (`us`, `ara`
  `mac-phonetic`). The first entry is the default. Each entry compiles to
  its own `xkb_keymap`. Blit does not use xkb groups, because libxkbcommon
  1.7 caps a keymap at 4 of them; switching keyboards swaps whole keymaps
  instead, so the list has no length limit.
- **The current keyboard.** There is one current keyboard (one keymap) per
  seat, not per device and not per window. Modifier and lock state stays
  per device, as now (each keyboard has its own `xkb_state` on the shared
  keymap), so Shift on one keyboard does not shift another. Blit holds it; `keyboard <index>` on the input
  route, or the API, sets it. If someone wants each window to remember its
  own keyboard, the desktop sets the current keyboard when focus changes,
  and Blit still holds only one.
- **One source of what a key types.** The keysym the device's state gives
  a key, as UTF-32 (`xkb_keysym_to_utf32`, not `xkb_state_key_get_utf8`,
  which applies the Control transformation), replaces
  `evdev_key_character` for the legacy encoder, kitty flag 16's text and
  the input stream's text. A chord's bytes (Ctrl+C is 0x03) come from the
  base key, the US key at that position, so Ctrl+C is 0x03 under Arabic
  too. Keypad and functional keys keep Blit's own kitty codes; only text
  keys take their code from the keymap. The kitty key code is the
  keymap's level-1 symbol; flag 4's base key stays the US PC-101 key at
  the same position, as now, so shortcuts are the same in every layout. Dead keys and
  compose come from `xkb_compose` with the locale's table. The
  compositor's keymap is the current keyboard's own text
  (`xkb_keymap_get_as_string`), not one generated from Blit's tables.
  Switching keyboards sends Wayland programs a new keymap and moves
  Xwayland with them.
- **One source of modifier and lock state.** Blit's per-device
  `xkb_state` is it. The modifier roles are applied as a translation of
  the physical code to the role's own code (Caps Lock as Control becomes
  Left Control) before the state sees it, counted so two keys holding one
  modifier release it with the last. The compositor stops keeping its own
  state: Blit sends `INPUT modifiers <id> <depressed> <latched> <locked>
  <group>` on the route, so Num Lock at Start and a lock held across a
  keyboard switch reach Wayland and X programs (before this they did not:
  the compositor's state started with no locks).
- **The layout decides Right Alt.** Blit treated Right Alt as AltGr
  always; under xkb it is what the layout says (Alt in `us`, AltGr in
  layouts that have a third level).
- **xkb options are not offered yet.** An option can remap a modifier,
  which is a second way to say what a role says; that is decided before
  any option is offered.
- **The device `keymap` overrides go.** A per-key override is what an xkb
  option or a user's own symbols file says, so the field is removed rather
  than kept beside the list. The `modifiers` role map and repeat stay per
  device: they describe the hardware, not the language. A stored `keymap`
  is removed from the settings document when it is loaded (logged with
  the device), not kept as an unknown property: kept, every later save
  would carry it back and have the device's entry refused.
- **Modular by data.** The pane lists what the system's xkb rules say
  exists (`evdev.xml`). A user's own layouts in `~/.config/xkb` are found
  the same way, if the libxkbcommon in use reads that path; this is
  verified before it is promised.
- **Input methods (word-level transliteration, Chinese, Japanese, Korean)
  are a second kind of keyboard** in the same list and the same switcher.
  They need text-input and input-method support in the compositor and a
  preedit in terminals. That is a later ADR; this one leaves room for it.
- **Switchers.** The desktop's menu bar item, its Control Strip module and
  the Switch Layout action in its shortcut table (Hibr #518) all set Blit's
  one current keyboard, and all show it. The login and lock screens show it
  and can switch it, because passwords are typed there.

## Releases

1. xkb for `us`: text, chords, the kitty codes, role translation, the
   modifiers line, the served keymap from xkb, the `keymap` property
   removed. `us` was compared with Blit's table for every key and level:
   every printable key is the same; the key beside Left Shift on ISO
   keyboards (KEY_102ND) now types `<` and `>`; Right Alt is Alt.
2. The list, the current keyboard and switching, with the API, the Blit
   Keyboard pane's list and the desktop's switchers (Hibr).
3. Dead keys and compose, with the compose table's locale chosen
   explicitly (Blit runs with no LANG).

## Consequences

- `blit` depends on libxkbcommon (≥ 1.5.0, found through pkg-config as
  ADR 0230 says) and xkeyboard-config's data. Both are on every desktop
  distribution, and blit-compositor already needs the first.
- The device `keymap` field, its gate cases (`kitty_keyboard_gate`'s layout
  cases, the keymap gate) and `evdev_key_character` are replaced, not
  wrapped. A saved `keymap` override is dropped with a log line naming the
  device. There is no translation, because an override has no layout
  name.
- A key types the same in a terminal, a Wayland program and an X program,
  because all three read one compiled keymap.

## Evidence

- Step 1, 0.3.91: `kitty_keyboard_gate` (xkb `us`, its locks, the ISO
  key, Right Alt, the altgr role), `key_encoding_gate`, `keymap_gate`,
  `input_stream_text_gate`, `keyboard_settings_gate` (a stored keymap
  removed on load), `compositor_gate` and `frame_input_gate` (Blit's
  modifiers on the route, Num Lock reaching a client).
- Step 2, 0.3.93: `kitty_keyboard_gate` (`ara(mac-phonetic)`: S types
  س with the base key s, Ctrl+C 0x03 and its base key c, back to `us`),
  `keyboard_settings_gate` (the list and the current one after a
  restart, an unknown layout 422), `compositor_gate` (a switch to Arabic
  and back reaching a client as its keymap), `api_schema_gate`.

- Step 3, 0.3.94: `kitty_keyboard_gate` (`us(intl)`: dead acute then e
  types é, then space the apostrophe; with flag 16 key e, text é) and
  `frame_input_gate` (the keyboards on the frame channel, for a desktop
  the login manager started, which has no API token: the list, a change
  made through the API, a switch, EINVAL, opt-in). The compose data is
  libx11-data's, which the `blit` package now depends on.

- X programs, 2026-11-13: `xwayland_gate`. Blit's ara(mac-phonetic)
  keymap is sent as the next revision. S reaches the X stand-in as keysym
  arabic_seen (0x5d3, the س a terminal types; the X server's mapping read
  at the key), after `s` (0x73) under US, and `s` again after the US
  keymap returns. It runs in the build environment with Xwayland 24.1,
  under ASan and UBSan as well.

## Amended 2026-11-13 (0.3.107): mouse buttons as shortcuts (Hibr #540)

Agreed with Hibr. A mouse's further buttons were dropped by Blit before
0.3.107, so back and forward reached no program. They now travel as X
numbers them: 8 back (BTN_SIDE), 9 forward (BTN_EXTRA), 10 (BTN_FORWARD)
and 11 (BTN_BACK).

- **To programs:** `INPUT btn` by evdev code.
- **To the input stream:** `button` 8 to 11.
- **`INPUT keep` takes buttons** as `button<N>` (1 left, 2 middle, 3
  right, 8 to 11) with the keys' modifiers in the same order: `button8`,
  `super-button9`, `ctrl-button2`. Buttons have no names of their own,
  since `left` and `right` are the arrow keys, and the wheel's 4 to 7 are
  refused.
- **A kept button chord** (its modifiers held on any keyboard) goes to no
  route.
- **To a terminal:** a further button is reported as xterm's Cb 128 to
  131, but only when that session keeps the chord. A reader that knows
  nothing of them (Hibr before 0.99.522 took 128 for a left press) is
  never sent one, and nothing has to be installed in a particular order.

Evidence: `frame_input_gate` sock part.
