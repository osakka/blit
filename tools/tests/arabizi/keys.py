#!/usr/bin/env python3
"""ADR 0233 design prototype, key by key: feed(key) -> commits, preedit.

    keys.py score <set.tsv> [-v] [numbers]   word and line accuracy, fed key by key
    keys.py type <text>                      show each key's preedit and commits

The engine sees one key at a time, as typing gives them. An article, a
conjunction, or a word that can begin a phrase is held in the preedit across
a space; nothing committed is ever taken back. Throwaway: it settles the
behaviour the C engine implements.
"""
import os, re, sys
sys.argv, _args = sys.argv[:1], sys.argv[1:]
import prototype as P  # noqa: E402  (rules, readings, English test)
sys.argv += _args

LETTER = re.compile(r"[A-Za-z0-9'`\-]")
PHRASE_STARTS = {k[:n] for k in P.R.phrases for n in range(1, len(k))}


def word_reading(latin):
    if re.fullmatch(r"[0-9]+", latin):
        return latin
    if (latin.lower(),) in P.R.words:
        return P.R.words[(latin.lower(),)]
    parts = latin.split('-')
    out = ''
    if len(parts) > 1 and (parts[0].lower() in P.R.article or
                           re.fullmatch(r"[aei](sh|th|dh|[tdrzsln])", parts[0].lower())):
        out, parts = 'ال', parts[1:]
    for p in parts:
        if not out and P.is_english(p):
            out += p
        else:
            plain, marked, _ = P.arabic(p)
            out += marked if P.TASHKEEL else plain
    return out


def is_article(word, nxt=None):
    w = word.lower()
    if w in P.R.article:
        return True
    sun = re.fullmatch(r"[aei](sh|th|dh|9'|6'|[tdrzsln96])", w)
    return bool(sun and (nxt is None or nxt.lower().startswith(sun.group(1))))


def reading(held):
    """The held words (and the current one, last) as one commit."""
    out, i, words = [], 0, held
    while i < len(words):
        for n in range(len(words) - i, 0, -1):
            key = tuple(w.lower() for w in words[i:i + n])
            if n > 1 and key in P.R.phrases or (n == 1 and key in P.R.phrases):
                out.append(P.R.phrases[key]); i += n; break
        else:
            w = words[i]
            nxt = words[i + 1] if i + 1 < len(words) else None
            if nxt is not None and is_article(w, nxt):
                out.append('ال' + word_reading(nxt).removeprefix('ال')); i += 2; continue
            if nxt is not None and w.lower() in P.R.conj:
                rest = reading([nxt])
                out.append('و' + rest); i += 2; continue
            out.append(word_reading(w)); i += 1
            continue
    return ' '.join(out)


class Engine:
    def __init__(self):
        self.held, self.word = [], ''

    def holds(self, word):
        """Whether a word just ended by a space stays in the preedit."""
        w = word.lower()
        if self.held and self.held[-1].lower() in P.R.conj | P.R.article:
            pass                                   # its joining word ends the hold
        elif w in P.R.article or w in P.R.conj or re.fullmatch(r"[aei](sh|th|dh|[tdrzsln])", w):
            return True
        key = tuple(x.lower() for x in self.held) + (w,)
        return key in PHRASE_STARTS

    def preedit(self):
        return (' '.join(self.held + [self.word])).strip()

    def flush(self, tail=''):
        words = self.held + ([self.word] if self.word else [])
        self.held, self.word = [], ''
        return [reading(words) + tail] if words else ([tail] if tail else [])

    def feed(self, key):
        """One key: returns the commits (text) and whether the key passes."""
        if LETTER.match(key):
            self.word += key
            return [], False
        if key == ' ':
            if self.word and self.holds(self.word):
                self.held.append(self.word); self.word = ''
                return [], False
            return self.flush(' '), False
        if key == '\b':
            if self.word:
                self.word = self.word[:-1]; return [], False
            if self.held:
                self.word = self.held.pop(); return [], False
            return [], True
        if key == '\n':
            return self.flush(), True
        # Punctuation: the end of the commit, its key not passed.
        return self.flush(P.R.punct.get(key, key)), False


def typed(text):
    e, out = Engine(), []
    for k in text:
        commits, passed = e.feed(k)
        out += commits
        if passed and k == '\n':
            out.append('\n')
    out += e.flush()
    return ''.join(out)


def words_of(s):
    return re.findall(r"\S+", s)


def score(path, verbose, numbers_only):
    rows = [l.rstrip('\n').split('\t') for l in open(path, encoding='utf-8') if l.strip() and not l.startswith('#')]
    lines = lok = wok = wn = 0
    for latin, want, cat in rows:
        got = typed(latin)
        lines += got == want
        lok += P.loose(got) == P.loose(want)
        gw, ww = words_of(got), words_of(want)
        wn += len(ww)
        wok += sum(1 for a, b in zip(gw, ww) if a == b) if len(gw) == len(ww) else \
            sum(1 for b in ww if b in gw)
        if verbose and not numbers_only and got != want:
            print('   %-8s %-26s want %-22s got %s' % (cat, latin, want, got))
    print('%-16s words %4d/%d (%.0f%%)  lines %3d/%d (%.0f%%), loose %.0f%%' % (
        os.path.basename(path), wok, wn, 100 * wok / wn, lines, len(rows), 100 * lines / len(rows),
        100 * lok / len(rows)))


if __name__ == '__main__':
    if sys.argv[1] == 'score':
        score(sys.argv[2], '-v' in sys.argv, 'numbers' in sys.argv)
    else:
        e = Engine()
        for k in ' '.join(sys.argv[2:]):
            c, p = e.feed(k)
            print(repr(k), 'preedit=%r' % e.preedit(), 'commit=%r' % c if c else '', 'pass' if p else '')
        print('flush', e.flush())
