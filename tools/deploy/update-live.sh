#!/bin/sh
set -eu

# Operator-only tooling. This is not installed in the Debian package and
# never guesses, prints, or stores an update credential.

usage() {
    echo "usage: $0 HOST PACKAGE" >&2
    echo "set BLIT_UPDATE_TOKEN or BLIT_UPDATE_TOKEN_FILE; optionally set BLIT_AUTH_TOKEN or BLIT_AUTH_TOKEN_FILE" >&2
    exit 2
}

[ "$#" -eq 2 ] || usage
host=$1
package=$2
test -f "$package" || { echo "update refused: package not found: $package" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "update refused: curl is required" >&2; exit 2; }
command -v dpkg-deb >/dev/null 2>&1 || { echo "update refused: dpkg-deb is required" >&2; exit 2; }

if [ "${BLIT_UPDATE_TOKEN:-}" != "" ]; then
    update_token=$BLIT_UPDATE_TOKEN
elif [ "${BLIT_UPDATE_TOKEN_FILE:-}" != "" ]; then
    test -r "$BLIT_UPDATE_TOKEN_FILE" || { echo "update refused: token file is not readable" >&2; exit 2; }
    update_token=$(sed -n '1p' "$BLIT_UPDATE_TOKEN_FILE" | sed 's/[[:space:]]*$//')
else
    echo "update refused: provide the live server's BLIT_UPDATE_TOKEN via environment or token file" >&2
    exit 2
fi
[ "$update_token" != "" ] || { echo "update refused: update token is empty" >&2; exit 2; }

package_version=$(dpkg-deb -f "$package" Version)
version=${package_version%-*}
sha256=$(sha256sum "$package" | awk '{print $1}')
repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
manifest="$repo_root/releases/$version.yaml"
test -f "$manifest" || {
    echo "update refused: release manifest not found: $manifest" >&2
    exit 2
}
manifest_package=$(sed -n 's/^package: //p' "$manifest")
manifest_sha256=$(sed -n 's/^package_sha256: //p' "$manifest")
package_name=$(basename "$package")
[ "$manifest_package" = "$package_name" ] || {
    echo "update refused: package name does not match release manifest" >&2
    exit 2
}
[ "$manifest_sha256" = "$sha256" ] || {
    echo "update refused: package checksum does not match release manifest" >&2
    exit 2
}
base_url=${BLIT_BASE_URL:-http://$host:8080}
api_token=${BLIT_AUTH_TOKEN:-}
if [ "$api_token" = "" ] && [ "${BLIT_AUTH_TOKEN_FILE:-}" != "" ]; then
    api_token=$(sed -n '1p' "$BLIT_AUTH_TOKEN_FILE" | sed 's/[[:space:]]*$//')
fi
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/blit-update.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

curl_auth() {
    if [ "$api_token" != "" ]; then
        curl --silent --show-error --connect-timeout 3 --max-time 120 "$@" -H "Authorization: Bearer $api_token"
    else
        curl --silent --show-error --connect-timeout 3 --max-time 120 "$@"
    fi
}

status=$(curl_auth --max-time 8 --fail "$base_url/v1/control/update/status") || {
    echo "update refused: could not read update status from $base_url" >&2
    exit 1
}
echo "$status" | grep -q '"state":"ready"' || {
    echo "update refused: server is not ready: $status" >&2
    exit 1
}
echo "$status" | grep -q '"active":false' || {
    echo "update refused: another upload is active; no retry was attempted" >&2
    exit 1
}

http_code=$(curl_auth --request PUT \
    -H "X-Blit-Update-Token: $update_token" \
    -H "X-Blit-SHA256: $sha256" \
    -H 'Content-Type: application/vnd.debian.binary-package' \
    --data-binary "@$package" \
    -o "$tmpdir/response" -w '%{http_code}' \
    "$base_url/v1/control/update") || {
    # The package postinst may deliberately restart the active unit after
    # dpkg installs a changed service boundary. In that case the upload was
    # accepted but the HTTP connection can close before a response is read;
    # let the bounded verification poll decide whether installation succeeded.
    echo "update connection closed during supervised restart; polling status" >&2
    http_code=000
}

case "$http_code" in
    200|202) ;;
    000) ;;
    401|403)
        echo "update refused: update credential rejected (HTTP $http_code); API auth and update auth are separate" >&2
        exit 1
        ;;
    *)
        echo "update failed: HTTP $http_code; no retry was attempted" >&2
        sed -n '1,3p' "$tmpdir/response" >&2
        exit 1
        ;;
esac

echo "accepted package $package_version (server $version) sha256=$sha256"
echo "polling for supervised installation"
i=0
while [ "$i" -lt 60 ]; do
    sleep 1
    if ! status=$(curl_auth --max-time 8 --fail "$base_url/v1/control/update/status"); then
        # The supervised service is expected to close the listening socket
        # briefly while dpkg and systemd restart it.  Keep polling instead of
        # reporting a failed deployment during that bounded transition.
        i=$((i + 1))
        continue
    fi
    if echo "$status" | grep -q '"state":"ready"' &&
       echo "$status" | grep -q '"active":false'; then
        if live_version=$(curl_auth --max-time 8 --fail "$base_url/v1/version"); then
            echo "$live_version" | grep -q "\"server_version\":\"$version\"" && {
                echo "update verified: Blit $version"
                exit 0
            }
        fi
    fi
    i=$((i + 1))
done
echo "update failed: timed out waiting for Blit $version; inspect update status and diagnostics" >&2
exit 1
