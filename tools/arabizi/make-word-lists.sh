#!/bin/sh
# tools/arabizi/make-word-lists.sh
#
# ADR 0233: the Arabizi engine's word lists, made from Hermit Dave's
# FrequencyWords (OpenSubtitles 2018 counts, content CC-BY-SA-4.0) at a
# pinned commit, so the same files come out every time. Writes
# assets/arabizi/ar-words.txt (the 100,000 most frequent Arabic words,
# Arabic letters only, seen at least 3 times) and en-words.txt (the 50,000
# most frequent English words, used only to leave English typed as English),
# and checks them against the sums recorded below.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
COMMIT=525f9b560de45753a5ea01069454e72e9aa541c6
BASE=https://raw.githubusercontent.com/hermitdave/FrequencyWords/$COMMIT/content/2018
OUT=$ROOT/assets/arabizi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl --fail --silent --show-error -o "$work/ar_full.txt" "$BASE/ar/ar_full.txt"
curl --fail --silent --show-error -o "$work/en_50k.txt" "$BASE/en/en_50k.txt"
python3 - "$work/ar_full.txt" "$OUT/ar-words.txt" <<'PY'
import re, sys
kept = 0
with open(sys.argv[2], 'w', encoding='utf-8') as out:
    for line in open(sys.argv[1], encoding='utf-8'):
        p = line.split()
        if len(p) == 2 and int(p[1]) >= 3 and re.fullmatch('[ء-ي]+', p[0]):
            out.write('%s %s\n' % (p[0], p[1]))
            kept += 1
            if kept == 100000:
                break
PY
python3 - "$work/en_50k.txt" "$OUT/en-words.txt" <<'PY'
import sys
with open(sys.argv[2], 'w', encoding='utf-8') as out:
    for line in open(sys.argv[1], encoding='utf-8'):
        p = line.split()
        if len(p) == 2 and p[0].isalpha():
            out.write('%s %s\n' % (p[0].lower(), p[1]))
PY
cd "$OUT"
sha256sum ar-words.txt en-words.txt
