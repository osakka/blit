#!/usr/bin/env python3
"""ADR 0233: the Arabizi engine's accuracy, held so it only rises.

    arabizi_gate.py [blit-server]

Runs a blit-server of its own with BLIT_ARABIZI_ROOT=assets/arabizi and
sends each line of tools/tests/arabizi/maintainer.tsv through
POST /v1/input/arabizi/convert, which feeds it key by key as typing does.
Word accuracy is primary, line accuracy beside it, and both must reach the
floors below. Raise a floor when the engine improves; never lower one.

The owner's own lines are personal and never in this repository. When
build/private/arabizi/owner.tsv exists, they are scored too, as numbers
only (no line is printed), against their own floor.

Also checked:
- the worst case for one word's search is bounded;
- tashkeel writes marks;
- a missing text is 400;
- missing rules are 503, not a crash.
"""
import json, os, re, subprocess, sys, tempfile, time, urllib.error, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
import gate_env  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server'))
PORT = 19735 + int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0'))
FLOORS = {  # (word %, line %): measured 2026-11-13 at the engine's first release
    'maintainer.tsv': (80.0, 77.9),   # 193/241 words, 170/218 lines
    'owner.tsv': (67.5, 38.4),        # 54/80 words, 10/26 lines
}
WORST_MS = 50   # one 48-letter word of the most ambiguous units


def fail(why):
    print('ARABIZI_GATE_FAIL %s' % why)
    sys.exit(1)


class Server:
    def __init__(self, root):
        work = tempfile.mkdtemp(prefix='blit-arabizi-gate-')
        env = gate_env.server_env(BLIT_ARABIZI_ROOT=root, BLIT_TEST_MODE='1', BLIT_LOGIN_AUTOSTART='0',
                                  BLIT_EXPECT_DESKTOP='0', BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'),
                                  BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
        self.process = subprocess.Popen([BINARY, str(PORT)], env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL)
        for _ in range(150):
            try:
                urllib.request.urlopen('http://127.0.0.1:%d/v1/version' % PORT, timeout=1)
                return
            except Exception:
                if self.process.poll() is not None:
                    fail('the server exited at start')
                time.sleep(0.1)
        fail('the server did not start')

    def convert(self, text, tashkeel=False):
        req = urllib.request.Request('http://127.0.0.1:%d/v1/input/arabizi/convert' % PORT, method='POST',
                                     data=json.dumps({'text': text, 'tashkeel': tashkeel}).encode(),
                                     headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status, json.load(r)
        except urllib.error.HTTPError as e:
            return e.code, json.load(e)

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(5)
        except subprocess.TimeoutExpired:
            self.process.kill()


def score(server, path):
    rows = [l.rstrip('\n').split('\t') for l in open(path, encoding='utf-8') if l.strip() and not l.startswith('#')]
    words = good_words = lines = 0
    for latin, want, _ in rows:
        status, got = server.convert(latin)
        if status != 200:
            fail('%s: HTTP %d' % (os.path.basename(path), status))
        got = got['text']
        lines += got == want
        gw, ww = got.split(), want.split()
        words += len(ww)
        good_words += sum(1 for a, b in zip(gw, ww) if a == b) if len(gw) == len(ww) else sum(1 for b in ww if b in gw)
    return 100 * good_words / words, 100 * lines / len(rows), len(rows)


def main():
    results = []
    server = Server(os.path.join(ROOT, 'assets/arabizi'))
    try:
        sets = [os.path.join(ROOT, 'tools/tests/arabizi/maintainer.tsv')]
        private = os.path.join(ROOT, 'build/private/arabizi/owner.tsv')
        if os.path.exists(private):
            sets.append(private)
        for path in sets:
            name = os.path.basename(path)
            w, l, n = score(server, path)
            fw, fl = FLOORS[name]
            if w < fw or l < fl:
                fail('%s: words %.1f%% (floor %.1f), lines %.1f%% (floor %.1f)' % (name, w, fw, l, fl))
            results.append('%s=words%.0f,lines%.0f(n%d)' % (name.split('.')[0], w, l, n))
        # The worst case: one long word of the units with the most options.
        start = time.time()
        status, _ = server.convert("th2dh2th2dh2th2dh2th2dh2th2dh2th2dh2" + "a" * 12)
        worst = (time.time() - start) * 1000
        if status != 200 or worst > WORST_MS:
            fail('the worst word took %.0f ms (bound %d), HTTP %d' % (worst, WORST_MS, status))
        status, got = server.convert('kitab', True)
        if status != 200 or not re.search('[ً-ْ]', got['text']):
            fail('tashkeel wrote no marks: %r' % (got,))
        status, _ = server.convert('')
        if status != 400:
            fail('an empty text was HTTP %d, not 400' % status)
        results.append('worst=%.0fms tashkeel=marks empty=400' % worst)
    finally:
        server.stop()
    missing = Server(tempfile.mkdtemp(prefix='blit-arabizi-none-'))
    try:
        status, got = missing.convert('salam')
        if status != 503 or got.get('code') != 'arabizi_unavailable':
            fail('missing rules gave HTTP %d %r, not 503' % (status, got))
        results.append('missing=503')
    finally:
        missing.stop()
    print('ARABIZI_GATE_PASS ' + ' '.join(results))


main()
