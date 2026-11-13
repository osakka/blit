# ADR 0233: Arabizi, a keyboard of our own

- Status: proposed (the owner's "go", 2026-11-13; this draft for the owner's
  and Hibr's review before any build)
- Date: 2026-11-13
- Scope: a new keyboard kind in Blit's keyboard list (ADR 0231): an input
  method that turns Latin Arabizi typing into Arabic, with or without
  tashkeel. Its engine, its rules and data, its delivery to terminals, to
  Hibr's desktop, to Wayland programs and to X programs, and its Control
  Panel tuning.

## Context

The owner asked for:
- a keyboard where they type English letters and it writes Arabic, "our
  own", possibly a standard;
- tunable in the Control Panel, never dictated (Hibr and HUI ethos);
- tashkeel;
- 95–100% accuracy on how they actually type.

Arabizi (the Arabic chat alphabet) has a shared core and no standard. From
Wikipedia's "Arabic chat alphabet" table:
- **Agreed:** 2 ء, 3 ع, 5 خ, 6 ط, 7 ح, 9 ص.
- **An apostrophe dots a letter:** 3' غ, 6' ظ, 9' ض, and for some writers
  7' خ.
- **Digraphs:** sh, kh, gh, th, dh.
- **Regional:**
  - 8 is غ in Lebanon and ق in Saudi Arabia.
  - 2 is ق in Egypt.
  - $ is ش in Jordan.
  - Capital D and T are emphatic in Lebanon.
- **Vowels:** no rule at all. A single "a" may be a long alef, a short
  fatha, or nothing.

So a keyboard layout can't do it. A layout maps one key to one letter;
Arabizi needs letter pairs, look-back and choice.

Blit's keyboards are libxkbcommon layouts (ADR 0231). The owner now has US
and Arabic (Buckwalter) on the mini PC, and Arabic (Macintosh, phonetic) is
offered. Both are one key, one letter.

### What was measured (a throwaway prototype, 2026-11-13)

**Test sets:**
- 218 lines of mixed-dialect Arabizi with their Arabic, written by Blit's
  maintainer.
- 26 lines of the owner's own Levantine typing, as they wrote them; the
  owner confirmed the Arabic for each. Kept privately, never in this
  repository: it is personal. It was held out for the first score, but
  rules were then added from its misses, so it no longer is. A fresh set
  from the owner is the held-out one, scored as a number only.

**Scores** (whole-line exact match; "loose" counts hamza seats and ة/ه as
equal):

| Approach | Maintainer's set | Owner's set (held out) |
|---|---|---|
| Rules only | 39% | — |
| Rules + a word-frequency list (OpenSubtitles, CC-BY-SA-4.0) | 78% (83% loose) | 38% |

The gap on the owner's lines is the finding:
- **Dialect words:** Levantine words (منيحة, مناح, رايحين, اشتقتلكم) are
  rare in a subtitle-based list.
- **Names:** the people the owner writes to.
- **English mixed in:** "all is well my friends", "Chicken Nuggets".
- **Context:** "3am" is عم before a verb and عام before a noun.
- **Classical endings:** in salamu, ra7matu, allahu the final vowel is a
  case ending.

A bigger list didn't help: 100,000 words scored 82% and 610,000 scored
83% on the maintainer's set.

These scores are provisional on three counts. They have to be measured
again before any gate threshold is set:
- **Look-ahead:** the prototype converted whole lines, joining across
  spaces (el bait → البيت, wa barakato → وبركاته), which typing key by key
  can only do by holding the preedit (below).
- **Metric:** whole-line exact match is too coarse; word accuracy is the
  primary measure.
- **Word list:** they used the 610,000-word list, not the 100,000-word file
  that ships.

### Tashkeel today

- **Blit renders tashkeel correctly.** Checked with the mini PC's setup
  (Fira Code 24 px plus Blit Arabic Modern, `visual-cell-marks`, text in
  visual order as the desktop sends it), every mark draws on its base cell:
  - fatha, kasra, damma, sukun;
  - shadda with a vowel;
  - the dagger alef;
  - the three tanween.
- **What the owner sees is Hibr #542.**
  - Fields count code points, so the caret can split a letter from its
    marks.
  - A caret between a letter and its fatha makes the fatha vanish, because
    the console attaches a mark only within one `console put`.
  - A key whose text is longer than one character is dropped by fields.
  - #542 fixes all three. It adds a `text <string>` console event that this
    ADR's commits use.
- **No way to type tashkeel on US.** Buckwalter can (a, i, u, o, ~, F N K).

## Options considered

1. **Replace typed letters with Backspace** (each key types a letter, and
   the next key rewrites it). Rejected: outside a text field Backspace is a
   command (a file manager goes up a folder). An input method must never
   delete what it typed.
2. **Rules and a frequency list only.** Measured above. It tops out well
   short of the owner's bar on their own typing.
3. **A neural network alone.** It learns vowels and context, but it's
   opaque: no setting can change what it does, which breaks the
   Control-Panel ethos. And it needs a large parallel corpus Blit doesn't
   have.
4. **A hybrid:** rules propose, a small model chooses, and the user's own
   layer overrides and teaches. Chosen.

## Decision

### The keyboard

**Arabic (Arabizi)** is a keyboard in Blit's one list (ADR 0231),
`{"layout":"ara","variant":"arabizi"}`, offered by
`/v1/input/keyboards/available` beside the xkb variants. It's built in, so
nothing is installed. It only takes effect once added, and the switcher
(menu bar, Next Keyboard) treats it like any other keyboard.

### The engine: an input method in Blit

The engine sits in Blit's key path after xkb and compose
(`dispatch_evdev_key_event`), for the device's keys while Arabizi is the
current keyboard and a text field has focus (below).

- **Keys:** Latin letters, digits, apostrophe and hyphen build a *preedit*:
  the Latin typed so far in this word and its best Arabic reading.
- **Commit:** a space or Enter commits the reading, then passes as its
  key. Punctuation commits too, but differently: the converted mark (؟ ، ؛,
  or the punctuation as typed) is the last character of the same commit,
  and the original key is not passed.
- **Tab:** cycles the candidates while a preedit exists, and passes as Tab
  when none does, since dialogs move the focus with it.
- **Held across a space:** an article (el, al, il, and the sun-letter forms
  esh, en, ...), a conjunction (wa, w, ou, o), or a word that can begin a
  fixed phrase (in, ma, ...) stays in the preedit when a space follows.
  The next word then joins it (البيت, وبركاته, إن شاء الله). A second
  space, punctuation, Enter, or a next word that does not continue the
  phrase commits what is held, as its own reading, and typing goes on.
  The preedit may span words, but the engine never deletes.
- **The dry run is the same code:** the endpoint and the corpus gate feed
  the text key by key through the engine. They have no second path that
  sees the whole line.
- **Backspace with a preedit:** takes one Latin letter back, and the
  reading is recomputed.
- **Escape:** cancels the preedit.
- **Chords:** a ctrl, alt or super chord commits first, then passes.
  Shortcuts still come from the US base key (ADR 0231).
- **Never deletes:** the engine never deletes anything it has committed.
- **Focus lost mid-word:** commits by default. The setting **On Focus
  Loss** offers commit or drop.
- **No text field focused:** the Arabizi keyboard types plain US keys, so
  menus, lists and shortcut capture behave as they always do. While a
  menu, a dropdown or a confirm box is open over a focused field (they take
  the keys), the desktop sends `TEXT field off`, then `TEXT field on` again
  when it closes. Type-to-search is the one menu that is a field.
- **Real digits:** a word of digits only stays digits (2024, 24/7). A digit
  next to a letter is a letter. To type a digit inside a word, use the
  **Literal key** setting (default: a backquote before the digit, `` `3 ``).

### Rules propose

One rules file, `arabizi.rules`, read by the engine and shown and edited in
the Control Panel, is the convention. Its parts:
- the digit and apostrophe table;
- digraphs;
- each Latin letter's Arabic options by position (start, middle, end of a
  word), the first being the default;
- long vowels (aa, ee, ii, oo, uu, ou) and diphthongs;
- endings (-an tanween, -eh/-ah ة);
- the article (el, al, il, and the sun-letter forms esh, en, ...), joined
  to its word;
- the conjunction (wa, w, ou, o), joined;
- fixed phrases (inshallah → إن شاء الله, ...);
- punctuation.

Every option may carry the haraka it stands for when silent (a silent
medial "a" is a fatha). That makes tashkeel one table with a switch, not a
second table.

The engine expands a word into its candidate readings. A bound on
candidates and time per keystroke is measured on the gateway loop.

### A scorer chooses

Phase 1 ranks candidates by a word-frequency list. Phase 2 replaces it with
a small character-level model:
- **Inputs:** the Latin word, its neighbours, and each candidate.
- **Size:** a few MB of int8 weights.
- **Inference:** in plain C in Blit, with no runtime dependency.

The model only orders the candidates the rules produced. So no output can
break the user's rules or tuning, and its choice is always one the Control
Panel could have named.

### The user's layer overrides and teaches

1. **My Words:** the user's own Arabizi → Arabic entries (names, family
   words, spellings). They win over every rule and score, applied at once.
2. **Choosing:** Tab (the Candidates key) cycles the preedit through its
   next candidates while a preedit exists. The pick is remembered as a My Words entry
   when **Learn From My Choices** is on.
3. **Retraining:** opt-in and local. Committed words and the user's
   choices are kept in Blit's state directory (never sent anywhere), and the
   scorer is retrained on the machine. Retrain on a schedule or on demand
   from the pane, with the corrections file viewable and erasable there.

### Training data

**The rules run backwards.** Arabic text, including dialect text, is
turned into many plausible Arabizi spellings: the digit forms, every vowel
convention, apostrophe and digraph variants. That gives a large synthetic
parallel corpus, and the owner's real typing fine-tunes it. Public sources
are listed with their licences before use:
- the word-frequency list is CC-BY-SA-4.0, compatible one-way with Blit's
  GPL-3.0-or-later but requiring attribution;
- dialect corpora are each checked.

Shipping any data file is the owner's call, recorded in the release.

### Delivery: preedit and commit, per target

1. **Hibr's desktop** (where the owner types), agreed with Hibr on
   2026-11-13. A contract on the frame channel:
   - desktop → Blit: `TEXT field on <row> <col>` (the caret's cell in the
     session) and `TEXT field off`, sent only on change. Under hold, hold
     rewrites the coordinates per display. A focused terminal window is a
     field at its program's cursor. The hibr menu's type-to-search is a
     field. Menus without search, lists and shortcut capture are not.
   - Blit → desktop: `TEXT preedit <text>` (empty clears it). The desktop
     draws it at the caret, underlined and not yet part of the value, with
     its own bidi and widths.
   - The commit is kitty associated text. Hibr #542 makes the console report
     multi-code-point text as one `text <string>` event: fields insert it,
     and terminal windows pass it to their program. One path, no second
     insert route.
2. **A plain terminal session** (no desktop): Blit draws the preedit
   itself at the session's cursor and commits as typed text.
3. **Wayland programs** (Firefox, LibreOffice, Zoom on the mini PC when
   native): `zwp_text_input_v3` in blit-compositor. Its enable and disable
   is the field signal, `set_cursor_rectangle` is the caret,
   `preedit_string` the preedit, and `commit_string` the commit. While one
   has the focus the desktop sends `TEXT field off`.
4. **X programs** (XTerm, Steam): no field signal, no caret. This is the
   degraded path, recorded as such:
   - whole words are committed through the keymap, as a burst of
     keycode-and-level events for letters placed on spare levels (counted
     against X's 255 keycodes and proved on `xwayland_gate`'s `key-sym`
     line);
   - the preedit is drawn by Blit at a fixed place near the window's
     bottom edge.

### Control Panel (Blit Keyboard, shown when Arabizi is in the list)

- **Try It:** a field; type Arabizi and see the Arabic and the candidates.
  The engine's own dry run (`POST /v1/input/arabizi/convert`) gives the
  pane, the gates and the user the same code.
- **Tashkeel:** off, or on (written marks).
- **Digits:**
  - each digit's letter, with the regional choices offered (8: ق or غ;
    2: ء or ق);
  - the apostrophe forms;
  - the Literal key.
- **Letters:** each Latin letter's options and their order, from the rules
  file, editable, with Restore Defaults.
- **Article and conjunction:** joined (default) or kept apart.
- **English words:** left as typed (default), with a sensitivity, or never.
- **My Words:** list, add, edit, remove, import, export.
- **Learning:**
  - Learn From My Choices;
  - Keep My Typing For Retraining (off by default; local; erasable);
  - Retrain Now and a schedule.
- **Candidates key** (Tab) and **On Focus Loss** (commit or drop).

The pane ships in hibr-apps with the owner's OK, as every pane does.

### The standard

The default rules file, published with its rationale and test sets, is
"Blit Arabizi". Anyone can type it predictably:
- long vowels doubled when it matters (kitaab);
- a backquote for a literal digit;
- 3' 6' 9' 7' for the dotted letters.

The scorer exists so that people needn't type it strictly.

## Consequences

- One engine and one rules file serve terminals, Hibr's desktop and
  Wayland programs. X programs get whole-word commits.
- Hibr builds its half: the TEXT lines, drawing the preedit, and #542's
  `text` event and cluster-safe fields. Nothing in Hibr has to know
  Arabizi.
- The engine runs on Blit's gateway loop. Its cost per key is measured and
  bounded in a gate.
- Typed text kept for retraining is personal data. It is off by default,
  local, visible and erasable in the pane, and never in diagnostics or
  logs.

## Phases

1. **Phase 1 (Blit):**
   - the rules file;
   - the engine with the frequency scorer and My Words;
   - the dry-run endpoint;
   - the corpus gate (both sets, with thresholds);
   - plain-terminal delivery;
   - Arabizi in the keyboard list.
2. **Phase 2 (Blit and Hibr):** the TEXT contract and desktop delivery,
   once #542's `text` event is in; the pane (Try It, Digits, My Words,
   Tashkeel).
3. **Phase 3:** text-input-v3 in blit-compositor, and X word commits.
4. **Phase 4:**
   - the synthetic corpus and the small model scorer;
   - Learn From My Choices;
   - opt-in retraining.

## Evidence (to be recorded per phase)

- **The corpus gate:**
  - the maintainer's set as `tools/tests/arabizi/*.tsv`;
  - the owner's sets read from a private path outside the repository when
    present, and reported as numbers only;
  - word accuracy primary, line accuracy beside it, exact and loose;
  - a threshold that only rises.
  - Target on the owner's typing: 95% exact once My Words and the model are
    in. 100% for every word the owner has corrected once.
- **The dry run** equals delivery: the same text through the endpoint and
  through keys gives the same commits.
- **Delivery:**
  - `kitty_keyboard_gate` (commits as associated text);
  - `frame_input_gate` (the TEXT lines);
  - `compositor_gate` (text-input-v3);
  - `xwayland_gate` (word commits).
- **The cost per key** on the gateway loop, measured and bounded.

## Open questions for the owner

- 8 by default: ق (Gulf and Saudi) or غ (Lebanese)?
- Article and conjunction joined by default? (The owner's answer, 2026-11-13: joined.)
- Tashkeel on by default when Arabizi is added, or off?

Related: ADR 0231 (keyboards from xkb), Hibr #538 (menus that scroll; the
candidate list), Hibr #542 (tashkeel in fields; the `text` event).
