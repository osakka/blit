#!/usr/bin/env bash
set -euo pipefail

package=${1:?usage: package_artifact_gate.sh PATH_TO_DEB}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
command -v dpkg-deb >/dev/null 2>&1
test -f "$package"

version=$(dpkg-deb -f "$package" Version)
release=${version%-1}
expected=$(sed -n '1p' "$root/packaging/VERSION")
test "$release" = "$expected"

manifest="$root/releases/$release.yaml"
test -f "$manifest"
actual_hash=$(sha256sum "$package" | awk '{print $1}')
manifest_hash=$(sed -n 's/^package_sha256: //p' "$manifest")
test "$actual_hash" = "$manifest_hash"

# The machine-owned release identity must be reproducible from the package;
# human acceptance fields may remain in the same YAML record, but hashes and
# package provenance must not be hand-copied.
"$root/tools/release/generate-manifest.sh" --check "$package" "$manifest"

stage=$(mktemp -d /tmp/blit-package-gate.XXXXXX)
cleanup() { rm -rf "$stage"; }
trap cleanup EXIT HUP INT TERM
dpkg-deb -x "$package" "$stage"
ruby "$root/tools/tests/service_boundary_gate.rb" --static "$stage/lib/systemd/system/blit.service"

# Every hash the manifest records must be the hash of what is inside this
# package.  0.1.432's manifest named a server binary that was never released:
# the value had been read from build/blit-desktop-current, a staging copy that
# the builder checks and never refreshes (issue #59).  package_sha256 was
# right, so the deployment itself was verified -- but a manifest that records
# a different artifact than it shipped cannot be used to answer what ran.
manifest_hash_matches() {
    name=$1
    file=$2
    recorded=$(sed -n "s/^$name: //p" "$manifest")
    if [ -z "$recorded" ]; then
        echo "PACKAGE_ARTIFACT_FAIL manifest has no $name" >&2
        exit 1
    fi
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [ "$recorded" != "$actual" ]; then
        echo "PACKAGE_ARTIFACT_FAIL $name records $recorded but the package" \
             "carries $actual" >&2
        exit 1
    fi
}
manifest_hash_matches server_binary_sha256 \
    "$stage/usr/lib/blit/bin/blit-server"
manifest_hash_matches update_helper_sha256 \
    "$stage/usr/lib/blit/bin/blit-update-helper"
manifest_hash_matches hibr_adapter_sha256 \
    "$stage/usr/lib/blit/hibr/console-blit.so"

test -x "$stage/usr/lib/blit/bin/blit-server"
test -f "$stage/usr/lib/blit/hibr/console-blit.so"
test -f "$stage/usr/lib/blit/fonts/Blit-Hibr-Spleen16.psf"
test -f "$stage/usr/lib/blit/fonts/BlitRetroNerd-Regular.ttf"
test -f "$stage/usr/lib/blit/fonts/nerd-glyphnames-v3.4.0.json"
test -f "$stage/usr/lib/blit/fonts/licenses/3270-LICENSE.txt"
BLIT_GATE_FONT_ROOT="$stage/usr/lib/blit/fonts" \
    ruby "$root/tools/tests/modern_font_assets_gate.rb"
test -f "$stage/usr/lib/blit/fonts/licenses/Spleen-LICENSE.txt"
test -f "$stage/usr/lib/blit/fonts/licenses/Terminus-OFL.txt"
test -f "$stage/usr/lib/blit/fonts/licenses/DejaVu-notice.txt"
test -f "$stage/usr/lib/blit/fonts/licenses/Noto-notice.txt"
test -f "$stage/usr/lib/blit/fonts/licenses/Droid-notice.txt"
test -f "$stage/usr/lib/blit/fonts/licenses/Apache-2.0.txt"
test -x "$stage/usr/share/blit/hibr/blit-session.hibr"
test -x "$stage/usr/share/blit/hibr/blit-login.hibr"
test -x "$stage/usr/share/blit/hibr/blit-login-session.hibr"
test -x "$stage/usr/share/blit/hibr/blit-desktop-observe.hibr"
test -x "$stage/usr/share/blit/hibr/blit-desktop-place.hibr"
source_commit=$(sed -n 's/^source_commit: //p' "$manifest")
for font_asset in Blit-Hibr-Spleen16.psf Blit-Hibr-Spleen16.source.json licenses/Spleen-LICENSE.txt Hack-Regular.ttf BlitModernNerd-Regular.ttf BlitRetroNerd-Regular.ttf NotoColorEmoji.ttf NotoEmojiText.ttf NotoEmojiText-LICENSE.txt Hack-LICENSE.txt licenses/HackNerd-LICENSE.txt licenses/NerdFonts-LICENSE.txt licenses/FontAwesome-LICENSE.txt NotoColorEmoji-LICENSE.txt modern-fonts.source.json icons/catalogue.json icons/LICENSE.txt icons/blit.svg icons/hibr-pen.svg icons/wifi-bars.svg icons/bluetooth.svg icons/berkanan.svg icons/wired.svg; do
    cmp -s <(git -C "$root" show "$source_commit:assets/fonts/$font_asset") \
        "$stage/usr/lib/blit/fonts/$font_asset" || {
        echo "PACKAGE_ARTIFACT_FAIL font differs from recorded source: $font_asset" >&2
        exit 1
    }
done
for font_asset in BlitArabicRetro.ttf BlitArabicModern.ttf arabic-fonts.source.json arabic-specimens.json KawkabMono-LICENSE.txt FreeMono-LICENSE.txt FreeMono-README.txt FreeMono-AUTHORS.txt sources/freefont-src-20120503.tar.gz; do
    cmp -s <(git -C "$root" show "$source_commit:assets/fonts/$font_asset") \
        "$stage/usr/lib/blit/fonts/$font_asset" || {
        echo "PACKAGE_ARTIFACT_FAIL Arabic asset differs from recorded source: $font_asset" >&2
        exit 1
    }
done
for entrypoint in blit-session.hibr blit-login-session.hibr blit-desktop-observe.hibr blit-desktop-place.hibr; do
    cmp -s <(git -C "$root" show "$source_commit:hibr/$entrypoint") \
        "$stage/usr/share/blit/hibr/$entrypoint" || {
        echo "PACKAGE_ARTIFACT_FAIL Hibr entrypoint differs from recorded source: $entrypoint" >&2
        exit 1
    }
done
test -f "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
grep -q 'mod load sys' "$stage/usr/share/blit/hibr/blit-login.hibr"
grep -q 'source "\$login"' "$stage/usr/share/blit/hibr/blit-login.hibr"
grep -q 'BLIT_HIBR_ADAPTER' "$stage/usr/share/blit/hibr/blit-runtime-console.hibr"
test ! -e "$stage/usr/lib/hibr/console-blit.so"
if command -v hibr >/dev/null 2>&1; then
    BLIT_GATE_RUNTIME_RC="$stage/usr/share/blit/hibr/blit-runtime-console.hibr" \
    BLIT_GATE_ADAPTER="$stage/usr/lib/blit/hibr/console-blit.so" \
        ruby "$root/tools/tests/native_child_provider_gate.rb"
fi
grep -q '^BLIT_DISPLAY_BACKEND=kms$' "$root/packaging/build-deb.sh"
grep -q 'console open' "$stage/usr/share/blit/hibr/blit-session.hibr"
grep -q 'export DT_BIDI=' "$stage/usr/share/blit/hibr/blit-session.hibr"
grep -q 'export HIBR_GFX=' "$stage/usr/share/blit/hibr/blit-session.hibr"
grep -q 'export COLORTERM=' "$stage/usr/share/blit/hibr/blit-session.hibr"
grep -q 'exec "\$HIBR" "\$desktop"' \
    "$stage/usr/share/blit/hibr/blit-session.hibr"
grep -q 'desktop=/usr/share/hibr/desktop/session.hibr' \
    "$stage/usr/share/blit/hibr/blit-session.hibr"
test ! -e "$stage/usr/share/blit/hibr/session.hibr"
test ! -e "$stage/usr/share/blit/hibr/system"
test -f "$stage/usr/lib/blit/openapi/blit-0.1.yaml"
strings "$stage/usr/lib/blit/bin/blit-server" | grep -Fx "$release" >/dev/null
cmp -s "$root/build/blit/blit-server" "$stage/usr/lib/blit/bin/blit-server"
cmp -s "$root/openapi/blit-0.1.yaml" "$stage/usr/lib/blit/openapi/blit-0.1.yaml"
documentation_commit=$(sed -n 's/^package_documentation_commit: //p' "$manifest")
git -C "$root" rev-parse --verify "$documentation_commit^{commit}" >/dev/null || {
    echo "PACKAGE_ARTIFACT_FAIL recorded documentation commit unavailable" >&2
    exit 1
}
cmp -s <(git -C "$root" show "$documentation_commit:README.md") \
    "$stage/usr/share/doc/blit/README.md" || {
    echo "PACKAGE_ARTIFACT_FAIL root README differs from recorded documentation" >&2
    exit 1
}
for document in README.md roadmap.md verification-status.md diagnostic-capture.md; do
    cmp -s <(git -C "$root" show "$documentation_commit:docs/$document") \
        "$stage/usr/share/doc/blit/docs/$document" || {
        echo "PACKAGE_ARTIFACT_FAIL documentation differs from recorded commit: $document" >&2
        exit 1
    }
    cmp -s "$stage/usr/share/doc/blit/docs/$document" \
        "$stage/usr/lib/blit/docs/$document" || {
        echo "PACKAGE_ARTIFACT_FAIL installed documentation copies disagree: $document" >&2
        exit 1
    }
done
if dpkg-deb --contents "$package" | grep -E -f "$root/tools/release/runtime-entrypoints.pattern" >/dev/null; then
    echo "PACKAGE_ARTIFACT_FAIL shell implementation or launcher present" >&2
    exit 1
fi

# A release manifest records package_sha256 as evidence, which is only
# evidence if the package can be rebuilt and checked.  It could not be until
# packaging/build-deb.sh pinned SOURCE_DATE_EPOCH: dpkg-deb stamped the ar
# container with the build time, so two builds of an identical tree differed.
# The rebuild is off by default because it costs a full package build; the
# release ritual runs it once.
reproducible=skipped
if [ "${BLIT_GATE_REBUILD:-0}" = 1 ]; then
    negative=$(mktemp /tmp/blit-package-negative.XXXXXX)
    if BLIT_HIBR_ROOT= "$root/packaging/build-deb.sh" \
           "$(dirname "$negative")" >"$negative" 2>&1; then
        echo "PACKAGE_ARTIFACT_FAIL package build accepted missing BLIT_HIBR_ROOT" >&2
        rm -f "$negative"
        exit 1
    fi
    grep -q 'BLIT_HIBR_ROOT is required for a package build' "$negative" || {
        echo "PACKAGE_ARTIFACT_FAIL missing-Hibr rejection was not explicit" >&2
        rm -f "$negative"
        exit 1
    }
    rm -f "$negative"
    rebuild=$(mktemp -d /tmp/blit-package-rebuild.XXXXXX)
    if BLIT_HIBR_ROOT=${BLIT_HIBR_ROOT:-} \
       BLIT_PACKAGE_DOCUMENTATION_COMMIT="$documentation_commit" \
       "$root/packaging/build-deb.sh" \
           "$rebuild" >/dev/null 2>&1 &&
       [ -f "$rebuild/$(basename "$package")" ]; then
        rebuilt_hash=$(sha256sum "$rebuild/$(basename "$package")" |
                       awk '{print $1}')
        if [ "$rebuilt_hash" != "$actual_hash" ]; then
            echo "PACKAGE_ARTIFACT_FAIL a rebuild of the same tree produced" \
                 "$rebuilt_hash, not $actual_hash" >&2
            rm -rf "$rebuild"
            exit 1
        fi
        reproducible=yes
    else
        echo "PACKAGE_ARTIFACT_FAIL the rebuild did not produce a package" >&2
        rm -rf "$rebuild"
        exit 1
    fi
    rm -rf "$rebuild"
fi

printf 'PACKAGE_ARTIFACT_PASS version=%s sha256=%s reproducible=%s\n' \
    "$version" "$actual_hash" "$reproducible"
