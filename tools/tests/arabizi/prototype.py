#!/usr/bin/env python3
"""Blit Arabizi design prototype: the engine reads arabizi.rules only.

    engine.py score <corpus.tsv> [-v] [key=value ...]
    engine.py say <text...> [tashkeel=1]

Throwaway: it settles the rules and the scoring the C engine will implement.
"""
import itertools, math, re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = dict(a.split('=', 1) for a in sys.argv if '=' in a and not a.startswith('-'))
PEN = float(ARGS.get('pen', '0.3'))         # a non-default option's cost
RATIO = float(ARGS.get('ratio', '3.0'))     # English kept when this much likelier
LIMIT = int(ARGS.get('limit', '20000'))     # candidates tried per word
TASHKEEL = ARGS.get('tashkeel', '0') == '1'


class Rules:
    def __init__(self, path):
        self.units = {}      # (latin, position) -> [(plain, marked)]
        self.phrases = {}    # tuple(words) -> arabic
        self.words = {}
        self.article, self.conj, self.punct = set(), set(), {}
        for line in open(path, encoding='utf-8'):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            kind, rest = line.split(None, 1)
            if kind in ('unit', 'long', 'ending'):
                latin, pos, opts = rest.split()
                self.units[(latin, pos)] = [self.option(o) for o in opts.split(',')]
                self.units.setdefault(('?kind', latin), kind)
            elif kind in ('phrase', 'word'):
                latin, arabic = (x.strip() for x in rest.split('=', 1))
                (self.phrases if kind == 'phrase' else self.words)[tuple(latin.lower().split())] = arabic
            elif kind == 'article':
                self.article |= set(rest.split())
            elif kind == 'conj':
                self.conj |= set(rest.split())
            elif kind == 'punct':
                a, b = rest.split()
                self.punct[a] = b
        self.latins = sorted({k[0] for k in self.units if k[0] != '?kind'}, key=len, reverse=True)

    @staticmethod
    def option(o):
        if o.startswith('-'):
            return ('', o[2:] if o.startswith('-:') else '')
        plain, _, mark = o.partition(':')
        return (plain, plain + mark)

    def kind(self, latin):
        return self.units.get(('?kind', latin))

    def opts(self, latin, pos):
        return self.units.get((latin, pos)) or self.units.get((latin, 'any'))


ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
DATA = os.environ.get('ARABIZI_DATA', os.path.join(ROOT, 'assets', 'arabizi'))
R = Rules(os.path.join(DATA, 'arabizi.rules'))


def load(path, alpha):
    out = {}
    for line in open(path, encoding='utf-8'):
        p = line.split()
        if len(p) == 2 and alpha(p[0]):
            out[p[0]] = int(p[1])
    return out


AR = load(os.path.join(DATA, 'ar-words.txt'), lambda w: True)
EN = load(os.path.join(DATA, 'en-words.txt'), str.isalpha)
AR_TOTAL, EN_TOTAL = sum(AR.values()), sum(EN.values())


def units(word):
    """Longest match against the rule table's Latin units; endings only last."""
    out, i, w = [], 0, word.lower()
    while i < len(w):
        for lat in R.latins:
            if w.startswith(lat, i):
                if R.kind(lat) == 'ending' and i + len(lat) != len(w):
                    continue
                out.append(lat)
                i += len(lat)
                break
        else:
            out.append(w[i])
            i += 1
    return out


def word_options(us):
    out = []
    for k, u in enumerate(us):
        pos = 'initial' if k == 0 else 'final' if k == len(us) - 1 else 'medial'
        if k > 0 and u == us[k - 1] and R.kind(u) == 'unit' and u not in 'aeiou':
            letter = (R.opts(u, 'medial') or [(u, u)])[0][0]
            out.append([('', 'ّ'), (letter, letter)])   # doubled: shadda once, or twice (الله)
            continue
        o = R.opts(u, pos) if R.kind(u) else None
        out.append(o or [(u, u)])
    return out


def candidates(word):
    us = units(word)
    opts = word_options(us)
    if math.prod(len(o) for o in opts) > LIMIT:
        opts = [o[:1] for o in opts]
    for idx in itertools.product(*[range(len(o)) for o in opts]):
        plain = ''.join(o[i][0] for o, i in zip(opts, idx))
        marked = ''.join(o[i][1] for o, i in zip(opts, idx))
        yield plain, marked, PEN * sum(idx)


def arabic(word):
    """(plain, marked, frequency) of the best reading; the rule default when
    no reading is a known word."""
    best, best_score = None, -1e18
    first = None
    for plain, marked, cost in candidates(word):
        if first is None:
            first = (plain, marked, 0)
        f = AR.get(plain, 0)
        if f and math.log(f) - cost > best_score:
            best, best_score = (plain, marked, f), math.log(f) - cost
    return best or first


def is_english(word):
    w = word.lower()
    if w not in EN or not w.isalpha():
        return False
    return EN[w] / EN_TOTAL > RATIO * (arabic(w)[2] / AR_TOTAL)


def convert(text):
    toks = re.findall(r"[A-Za-z0-9']+(?:-[A-Za-z0-9']+)*|\s+|.", text)
    out, i = [], 0
    while i < len(toks):
        # Phrases first, longest, over word tokens separated by single spaces.
        words = [(j, toks[j].lstrip('\0').lower()) for j in range(i, min(len(toks), i + 12)) if not toks[j].isspace()]
        hit = None
        for n in (range(min(5, len(words)), 0, -1) if not toks[i].isspace() else ()):
            key = tuple(w for _, w in words[:n])
            if key in R.phrases and all(toks[j].isspace() for j in range(i, words[n - 1][0]) if j not in [x for x, _ in words[:n]]):
                hit = (words[n - 1][0], R.phrases[key])
                break
        if hit:
            out.append(hit[1]); i = hit[0] + 1; continue
        t = toks[i]
        if t.isspace() or not re.match(r"\0?[A-Za-z0-9']", t):
            out.append(R.punct.get(t, t)); i += 1; continue
        if re.fullmatch(r"[0-9]+", t.lstrip('\0')):
            out.append(t); i += 1; continue
        low = t.lstrip('\0').lower()
        nxt = i + 2 if i + 2 < len(toks) and toks[i + 1] == ' ' else None
        # The article and the conjunction join the next word.
        sun = re.fullmatch(r"[aei](sh|th|dh|9'|6'|[tdrzsln96])", low)
        if nxt is not None and (low in R.article or (sun and toks[nxt].lower().startswith(sun.group(1)))):
            out.append('ال'); i = nxt; toks[i] = '\0' + toks[i]; continue
        if nxt is not None and low in R.conj:
            out.append('و'); i = nxt; continue
        joined = t.startswith('\0')
        t = t.lstrip('\0')
        parts = t.split('-')
        if len(parts) > 1 and (parts[0].lower() in R.article or re.fullmatch(r"[aei](sh|th|dh|[tdrzsln])", parts[0].lower())):
            out.append('ال'); parts = parts[1:]
        for p in parts:
            if tuple([p.lower()]) in R.words:
                out.append(R.words[(p.lower(),)]); continue
            if not joined and is_english(p):
                out.append(p); continue
            plain, marked, _ = arabic(p)
            out.append(marked if TASHKEEL else plain)
        i += 1
    return ''.join(out)


def loose(s):
    return s.translate(str.maketrans('أإآةى', 'اااهي'))


def score(path, verbose):
    rows = [l.rstrip('\n').split('\t') for l in open(path, encoding='utf-8') if l.strip() and not l.startswith('#')]
    ok = lok = 0
    for latin, want, cat in rows:
        got = convert(latin)
        ok += got == want
        lok += loose(got) == loose(want)
        if verbose and got != want:
            print('   %-8s %-24s want %-20s got %s' % (cat, latin, want, got))
    print('%-12s exact %3d/%d (%.0f%%)  loose %3d (%.0f%%)' % (os.path.basename(path), ok, len(rows), 100 * ok / len(rows), lok, 100 * lok / len(rows)))


if __name__ == '__main__':
    if sys.argv[1] == 'score':
        score(sys.argv[2], '-v' in sys.argv)
    else:
        print(convert(' '.join(a for a in sys.argv[2:] if '=' not in a)))
