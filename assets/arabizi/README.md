# Arabizi data (ADR 0233)

The Arabizi keyboard's engine reads these, installed in
`/usr/share/blit/arabizi/`. Set `BLIT_ARABIZI_ROOT` to read them from
elsewhere.

- `arabizi.rules`: the rules ("Blit Arabizi"). Blit's own, GPL-3.0-or-later
  like the rest of Blit. Its format is described at its top.
- `ar-words.txt`: the 100,000 most frequent Arabic words (Arabic letters
  only, seen at least 3 times), one `<word> <count>` per line.
- `en-words.txt`: the 50,000 most frequent English words, used only to
  leave English typed as English.

## Credit and licence of the word lists

Both word lists are derived from **FrequencyWords** by Hermit Dave
(https://github.com/hermitdave/FrequencyWords, commit
525f9b560de45753a5ea01069454e72e9aa541c6), word counts from the
OpenSubtitles 2018 corpus (http://www.opensubtitles.org/,
http://opus.nlpl.eu/OpenSubtitles2018.php).

They are licensed under **Creative Commons Attribution-ShareAlike 4.0
International** (CC BY-SA 4.0, https://creativecommons.org/licenses/by-sa/4.0/).
The changes are those made by `tools/arabizi/make-word-lists.sh`: the
Arabic list is filtered to Arabic letters and at least 3 occurrences and cut
to 100,000 words; the English list is lower-cased and filtered to letters.
These derived lists are offered under the same licence.

Checksums (`tools/arabizi/make-word-lists.sh` prints them):

```
dd406b8c902c8dffc1ae27717a7f128a00a66f8a0beae5eee8d47cc35067e1d6  ar-words.txt
de97b4135c2702c97d7be3315f798d13adcaafc33615d4eec300a7933b4fe323  en-words.txt
```
