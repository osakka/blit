#!/bin/sh
# The pinned fontTools the font builders use (tools/fonts/build-*.py), made
# reproducible (Gitea #118): fontTools 4.60.1's pure-Python wheel, fetched
# once from PyPI into build/deps and checked against the SHA-256 PyPI
# publishes for it. Prints the directory to put on PYTHONPATH.
#
#   PYTHONPATH=$(tools/fonts/fonttools.sh) python3 tools/fonts/build-blit-icons.py out.ttf
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
VERSION=4.60.1
WHEEL=fonttools-$VERSION-py3-none-any.whl
SHA256=906306ac7afe2156fcf0042173d6ebbb05416af70f6b370967b47f8f00103bbb
URL=https://files.pythonhosted.org/packages/c7/93/0dd45cd283c32dea1545151d8c3637b4b8c53cdb3a625aeb2885b184d74d/$WHEEL
DIR=$ROOT/build/deps/fonttools-$VERSION
if [ ! -f "$DIR/fontTools/__init__.py" ]; then
    mkdir -p "$DIR"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    curl -fsSL -o "$tmp/$WHEEL" "$URL" || { echo "fonttools.sh: cannot fetch $URL" >&2; exit 1; }
    echo "$SHA256  $tmp/$WHEEL" | sha256sum -c --quiet - || { echo "fonttools.sh: $WHEEL is not the pinned one" >&2; exit 1; }
    python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$tmp/$WHEEL" "$DIR"
fi
echo "$DIR"
