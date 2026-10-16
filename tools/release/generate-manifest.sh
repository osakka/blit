#!/bin/sh
set -eu

# Generate or verify the machine-owned identity section of a release
# manifest. Human acceptance claims and notes do not belong in this tool:
# they are evidence recorded by the release operator, not facts derivable
# from a Debian archive.

usage() {
    cat >&2 <<'EOF'
usage:
  generate-manifest.sh PACKAGE [OUTPUT]
  generate-manifest.sh --check PACKAGE MANIFEST

The generated output is the canonical machine-owned manifest section. Set
BLIT_SOURCE_COMMIT and BLIT_DOCUMENTATION_COMMIT when the package was built
from a commit that is not the current checkout (for example, when recording
the manifest in the following commit).
EOF
    exit 2
}

fail() {
    echo "release manifest: $*" >&2
    exit 1
}

command -v dpkg-deb >/dev/null 2>&1 || fail "dpkg-deb is required"
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is required"

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)

manifest_value() {
    sed -n "s/^$1: //p" "$2" | sed -n '1p'
}

render_manifest() {
    package=$1
    package_version=$2
    release=$3
    package_sha256=$4
    source_commit=$5
    documentation_commit=$6
    server_sha256=$7
    helper_sha256=$8
    adapter_sha256=$9
    hibr_version=${10}

    cat <<EOF
release: $release
package: $(basename "$package")
package_sha256: $package_sha256
source_commit: $source_commit
package_documentation_commit: $documentation_commit
server_binary_sha256: $server_sha256
update_helper_sha256: $helper_sha256
hibr_adapter_sha256: $adapter_sha256
hibr_version: $hibr_version
EOF
}

generate() {
    package=$1
    output=${2:-}
    test -f "$package" || fail "package not found: $package"

    package_version=$(dpkg-deb -f "$package" Version)
    case "$package_version" in
        *-1) release=${package_version%-1} ;;
        *) fail "unsupported package version '$package_version' (expected -1)" ;;
    esac
    package_name=$(basename "$package")
    expected_name="blit_${package_version}_amd64.deb"
    [ "$package_name" = "$expected_name" ] ||
        fail "package name '$package_name' does not match '$expected_name'"

    source_commit=${BLIT_SOURCE_COMMIT:-$(git -C "$root" rev-parse HEAD)}
    documentation_commit=${BLIT_DOCUMENTATION_COMMIT:-$source_commit}
    [ "$source_commit" != "" ] || fail "source commit is empty"
    [ "$documentation_commit" != "" ] || fail "documentation commit is empty"

    package_sha256=$(sha256sum "$package" | awk '{print $1}')
    stage=$(mktemp -d "${TMPDIR:-/tmp}/blit-manifest.XXXXXX")
    trap 'rm -rf "$stage"' EXIT HUP INT TERM
    dpkg-deb -x "$package" "$stage"

    server="$stage/usr/lib/blit/bin/blit-server"
    helper="$stage/usr/lib/blit/bin/blit-update-helper"
    adapter="$stage/usr/lib/blit/hibr/console-blit.so"
    hibr_file="$stage/usr/share/doc/blit/HIBR_VERSION"
    for required in "$server" "$helper" "$adapter" "$hibr_file"; do
        test -f "$required" || fail "package is missing $required"
    done
    server_sha256=$(sha256sum "$server" | awk '{print $1}')
    helper_sha256=$(sha256sum "$helper" | awk '{print $1}')
    adapter_sha256=$(sha256sum "$adapter" | awk '{print $1}')
    hibr_version=$(sed -n '1p' "$hibr_file")
    [ "$hibr_version" != "" ] || fail "packaged HIBR_VERSION is empty"

    if [ -n "$output" ]; then
        output_dir=$(dirname "$output")
        test -d "$output_dir" || fail "output directory does not exist: $output_dir"
        temporary="$output.tmp.$$"
        render_manifest "$package" "$package_version" "$release" \
            "$package_sha256" "$source_commit" "$documentation_commit" \
            "$server_sha256" "$helper_sha256" "$adapter_sha256" \
            "$hibr_version" >"$temporary"
        mv -f "$temporary" "$output"
    else
        render_manifest "$package" "$package_version" "$release" \
            "$package_sha256" "$source_commit" "$documentation_commit" \
            "$server_sha256" "$helper_sha256" "$adapter_sha256" \
            "$hibr_version"
    fi
}

check() {
    package=$1
    manifest=$2
    test -f "$manifest" || fail "manifest not found: $manifest"
    source_commit=$(manifest_value source_commit "$manifest")
    documentation_commit=$(manifest_value package_documentation_commit "$manifest")
    [ "$source_commit" != "" ] || fail "manifest has no source_commit"
    [ "$documentation_commit" != "" ] ||
        fail "manifest has no package_documentation_commit"

    expected=$(mktemp "${TMPDIR:-/tmp}/blit-manifest-expected.XXXXXX")
    trap 'rm -f "$expected"' EXIT HUP INT TERM
    BLIT_SOURCE_COMMIT=$source_commit \
        BLIT_DOCUMENTATION_COMMIT=$documentation_commit \
        generate "$package" >"$expected"

    # The manifest may contain human-owned acceptance fields after this
    # block. Every generated line must occur exactly as generated; omission,
    # drift, or a duplicate machine field is a release error.
    while IFS= read -r line; do
        count=$(grep -Fxc "$line" "$manifest" || true)
        [ "$count" -eq 1 ] || {
            echo "release manifest: generated field mismatch: $line" >&2
            exit 1
        }
    done <"$expected"

    package_release=$(dpkg-deb -f "$package" Version)
    manifest_release=$(manifest_value release "$manifest")
    [ "$manifest_release" = "${package_release%-1}" ] ||
        fail "manifest release does not match package"
    echo "RELEASE_MANIFEST_PASS package=$package_release manifest=$manifest"
}

case "${1:-}" in
    --check)
        [ "$#" -eq 3 ] || usage
        check "$2" "$3"
        ;;
    -h|--help) usage ;;
    "") usage ;;
    *)
        [ "$#" -le 2 ] || usage
        generate "$@"
        ;;
esac
