#!/bin/sh
set -eu

# Operator-only tooling. This is not installed in the Debian package and
# never guesses, prints, or stores an update credential.

# ADR 0232: PACKAGE is the release's blit .deb; blit-compositor of the same
# version must sit beside it. Both go in one upload, and the host's helper
# installs the compositor too when the host has it.
# Security review SR-2: so must the release's signature,
# blit-release_<version>_amd64.sig (tools/release/sign-release.sh). It goes
# as X-Blit-Signature; the host's helper refuses the update unless it
# verifies under one of the host's release keys.
#
# Gitea #206: --rollback-to PREVIOUS names the pair the box runs now (its
# blit .deb, with its compositor, its own .sig and releases/<version>.yaml,
# all checked before anything is sent). When the smoke check after the
# update fails, that pair is installed again the same way, signed by its own
# .sig, and the box is checked against the state it was in before the
# update. Gitea #207: the smoke check passes a box that is at least as it
# was (BLIT_SMOKE_BASELINE, from a failed run's report, names that state for
# a rollback by hand); a box that had no display passes with its displays
# up and frames advancing.
#
# Exit status: 0 installed and the smoke check passed; 2 refused before
# anything was sent; 1 the update failed (it may be installed: read the
# message); 3 the smoke check failed and the previous pair is back and
# passes it; 4 the smoke check failed and so did the rollback -- the box
# needs the runbook (docs/security-review-2026-11.md).

usage() {
    echo "usage: $0 [--rollback-to PREVIOUS_BLIT_PACKAGE] HOST BLIT_PACKAGE (blit-compositor of its version and the release's .sig beside each)" >&2
    echo "set BLIT_UPDATE_TOKEN or BLIT_UPDATE_TOKEN_FILE; optionally set BLIT_AUTH_TOKEN or BLIT_AUTH_TOKEN_FILE," >&2
    echo "BLIT_SMOKE_BASELINE ('<ready> <displays> <panes> <frames> <login-up>' from a failed run) and BLIT_SMOKE_SECONDS (60)" >&2
    exit 2
}

rollback_package=
while [ "$#" -gt 0 ]; do
    case $1 in
        --rollback-to) [ "$#" -ge 2 ] || usage; rollback_package=$2; shift 2 ;;
        --rollback-to=*) rollback_package=${1#*=}; shift ;;
        --) shift; break ;;
        -*) usage ;;
        *) break ;;
    esac
done
[ "$#" -eq 2 ] || usage
host=$1
package=$2
test -f "$package" || { echo "update refused: package not found: $package" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "update refused: curl is required" >&2; exit 2; }
command -v dpkg-deb >/dev/null 2>&1 || { echo "update refused: dpkg-deb is required" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "update refused: python3 is required (the smoke check)" >&2; exit 2; }

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

smoke_seconds=${BLIT_SMOKE_SECONDS:-60}
case "$smoke_seconds" in
    ''|*[!0-9]*|0) echo "update refused: BLIT_SMOKE_SECONDS must be a whole number of seconds" >&2; exit 2 ;;
esac
smoke_baseline=${BLIT_SMOKE_BASELINE:-}
if [ -n "$smoke_baseline" ]; then
    set -f
    # shellcheck disable=SC2086
    set -- $smoke_baseline
    set +f
    words=$*
    case "$#:$words" in
        5:*[!0-9\ ]*) words= ;;
        5:*) ;;
        *) words= ;;
    esac
    [ -n "$words" ] || {
        echo "update refused: BLIT_SMOKE_BASELINE is five whole numbers (ready displays panes frames login-up)" >&2
        exit 2
    }
    smoke_baseline=$words
fi

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/blit-update.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

# check_release PACKAGE WHAT BODY: the pair beside PACKAGE, its .sig and its
# manifest, as one release; sets r_version, r_package_version, r_parts,
# r_signature and r_sha256, r_compositor_sha256, and writes the upload body
# to BODY. WHAT names it in a refusal ("" for the update itself).
check_release() {
    r_package=$1 what=$2
    r_package_version=$(dpkg-deb -f "$r_package" Version)
    r_version=${r_package_version%-*}
    r_sha256=$(sha256sum "$r_package" | awk '{print $1}')
    r_compositor=$(dirname "$r_package")/blit-compositor_${r_package_version}_amd64.deb
    test -f "$r_compositor" || {
        echo "update refused: ${what}$r_compositor not found beside $r_package" >&2
        exit 2
    }
    r_compositor_sha256=$(sha256sum "$r_compositor" | awk '{print $1}')
    r_signature_file=$(dirname "$r_package")/blit-release_${r_package_version}_amd64.sig
    test -f "$r_signature_file" || {
        echo "update refused: ${what}the release's signature $r_signature_file is not beside $r_package" \
             "(tools/release/sign-release.sh writes it; a host refuses an unsigned update)" >&2
        exit 2
    }
    r_signature=$(sed -n '1p' "$r_signature_file")
    case "$r_signature" in
        *[!0-9a-f]*|'') signature_ok=0 ;;
        *) [ "${#r_signature}" -eq 128 ] && signature_ok=1 || signature_ok=0 ;;
    esac
    [ "$signature_ok" = 1 ] || {
        echo "update refused: ${what}$r_signature_file is not a release signature (128 lowercase hex digits)" >&2
        exit 2
    }
    # releases/<version>.yaml; one named for the whole package version wins
    # (the rollback rehearsal's fabricated N-1 is N's version at revision 0).
    manifest="$repo_root/releases/$r_package_version.yaml"
    [ -f "$manifest" ] || manifest="$repo_root/releases/$r_version.yaml"
    test -f "$manifest" || {
        echo "update refused: ${what}release manifest not found: $manifest" >&2
        exit 2
    }
    [ "$(sed -n 's/^package: //p' "$manifest")" = "$(basename "$r_package")" ] || {
        echo "update refused: ${what}package name does not match release manifest" >&2
        exit 2
    }
    [ "$(sed -n 's/^package_sha256: //p' "$manifest")" = "$r_sha256" ] || {
        echo "update refused: ${what}package checksum does not match release manifest" >&2
        exit 2
    }
    [ "$(sed -n 's/^compositor_package: //p' "$manifest")" = "$(basename "$r_compositor")" ] &&
    [ "$(sed -n 's/^compositor_package_sha256: //p' "$manifest")" = "$r_compositor_sha256" ] || {
        echo "update refused: ${what}blit-compositor does not match release manifest" >&2
        exit 2
    }
    # The two packages back to back; X-Blit-Packages says where each ends.
    cat "$r_package" "$r_compositor" >"$3"
    r_parts="$(wc -c <"$r_package" | tr -d ' '):$r_sha256 $(wc -c <"$r_compositor" | tr -d ' '):$r_compositor_sha256"
}

check_release "$package" "" "$tmpdir/body"
package_version=$r_package_version version=$r_version sha256=$r_sha256
compositor_sha256=$r_compositor_sha256 parts=$r_parts signature=$r_signature

if [ -n "$rollback_package" ]; then
    test -f "$rollback_package" || {
        echo "update refused: the rollback package is not found: $rollback_package" >&2
        exit 2
    }
    check_release "$rollback_package" "the rollback pair: " "$tmpdir/rollback-body"
    old_package_version=$r_package_version old_version=$r_version
    old_parts=$r_parts old_signature=$r_signature
    [ "$old_package_version" != "$package_version" ] || {
        echo "update refused: --rollback-to names the release being installed ($package_version)" >&2
        exit 2
    }
    # The helper installs no blit older than its floor, even signed (SR-2):
    # a rollback below it is root's own dpkg, so it cannot be this one.
    oldest=$(sed -n 's/^#define UPDATE_OLDEST_RELEASE "\([^"]*\)"$/\1/p' \
        "$repo_root/src/blit_update_helper.c" 2>/dev/null || true)
    if [ -n "$oldest" ] && dpkg --compare-versions "$old_package_version" lt "$oldest~"; then
        echo "update refused: the rollback pair $old_package_version is older than $oldest," \
             "the oldest release the host's helper installs; that rollback is root's own dpkg" >&2
        exit 2
    fi
fi

base_url=${BLIT_BASE_URL:-http://$host:8080}
api_token=${BLIT_AUTH_TOKEN:-}
if [ "$api_token" = "" ] && [ "${BLIT_AUTH_TOKEN_FILE:-}" != "" ]; then
    api_token=$(sed -n '1p' "$BLIT_AUTH_TOKEN_FILE" | sed 's/[[:space:]]*$//')
fi

curl_auth() {
    if [ "$api_token" != "" ]; then
        curl --silent --show-error --connect-timeout 3 --max-time 120 "$@" -H "Authorization: Bearer $api_token"
    else
        curl --silent --show-error --connect-timeout 3 --max-time 120 "$@"
    fi
}

# Gitea #27: the box as it is, for the smoke check after the update:
# "<displays ready> <display count> <panes showing a surface> <frames>
# <login screen up>". The last is 1 when the login screen is running in a
# pane and waiting for a password: a restart signs the desktop out by design
# (ADR 0205; Hibr keeps it held), so a box that was signed in comes back to
# its login screen, and only a person signing in brings the desktop back.
smoke_state() {
    server=$(curl_auth --max-time 8 --fail "$base_url/v1/server") || return 1
    topology=$(curl_auth --max-time 8 --fail "$base_url/v1/topology") || return 1
    runtime=$(curl_auth --max-time 8 --fail "$base_url/v1/diagnostics/runtime") || return 1
    login=$(curl_auth --max-time 8 "$base_url/v1/login") || login='{}'
    printf '%s\n%s\n%s\n%s\n' "$server" "$topology" "$runtime" "$login" | python3 -c '
import json, sys
server, topology, runtime, login = (json.loads(line or "{}") for line in sys.stdin.read().splitlines()[:4])
attached = sum(1 for p in topology.get("panes", []) if p.get("surface_id"))
session = login.get("session") or {}
login_up = int(login.get("state") == "running" and bool(session.get("surface_id")) and
               (login.get("desktop") or {}).get("reason") == "authentication_required")
print("%d %d %d %d %d" % (server["display_backend"]["ready"], server["display_count"], attached,
                          runtime["performance"]["frames"], login_up))'
}

before=$(smoke_state) || {
    echo "update refused: could not read the box's state (server, topology, runtime) for the smoke check" >&2
    exit 2   # nothing was sent (Gitea #211)
}
# The state the smoke check holds the box to: as it was found, unless the
# operator names the state recorded before an earlier, failed update.
baseline=${smoke_baseline:-$before}

if [ -n "$rollback_package" ]; then
    # The previous pair is the one the box runs now, or it is not a rollback.
    live_version=$(curl_auth --max-time 8 --fail "$base_url/v1/version" |
        sed -n 's/.*"server_version":"\([^"]*\)".*/\1/p') || live_version=
    [ "$live_version" = "$old_version" ] || {
        echo "update refused: --rollback-to names Blit $old_version, but the box runs ${live_version:-an unreadable version}" >&2
        exit 2
    }
fi

# install_pair PACKAGE_VERSION VERSION PARTS SIGNATURE BODY: one supervised
# install through the API (ADR 0232), and the wait until the server says
# VERSION. 0 when it does; otherwise 1, with why on standard error.
install_pair() {
    i_package_version=$1 i_version=$2 i_parts=$3 i_signature=$4 i_body=$5
    status=$(curl_auth --max-time 8 --fail "$base_url/v1/control/update/status") || {
        echo "update refused: could not read update status from $base_url" >&2
        return 1
    }
    echo "$status" | grep -q '"state":"ready"' || {
        echo "update refused: server is not ready: $status" >&2
        return 1
    }
    echo "$status" | grep -q '"active":false' || {
        echo "update refused: another upload is active; no retry was attempted" >&2
        return 1
    }

    http_code=$(curl_auth --request PUT \
        -H "X-Blit-Update-Token: $update_token" \
        -H "X-Blit-Packages: $i_parts" \
        -H "X-Blit-Signature: $i_signature" \
        -H 'Content-Type: application/vnd.debian.binary-package' \
        --data-binary "@$i_body" \
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
            return 1
            ;;
        *)
            echo "update failed: HTTP $http_code; no retry was attempted" >&2
            sed -n '1,3p' "$tmpdir/response" >&2
            return 1
            ;;
    esac

    echo "accepted blit and blit-compositor $i_package_version (server $i_version) sha256=$(printf '%s\n' "$i_parts" | sed 's/[0-9]*://g')"
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
                echo "$live_version" | grep -q "\"server_version\":\"$i_version\"" && {
                    echo "update verified: Blit $i_version"
                    return 0
                }
            fi
        fi
        i=$((i + 1))
    done
    echo "update failed: timed out waiting for Blit $i_version; inspect update status and diagnostics" >&2
    return 1
}

# smoke_check VERSION (Gitea #27, #207): within $smoke_seconds the box is at
# least as it was in $baseline -- as many displays up (more is not a
# regression), a surface in as many panes (the desktop came back) or, for
# a box that was signed in, its login screen up waiting for the password --
# and its frames advancing when it has displays. A box that had no display
# ready (an API-only box, or the broken one a rollback repairs) passes with
# none, or with its displays up and frames advancing. The deploys of
# 0.3.124-0.3.126 passed the old "as many panes" rule only because the
# owner signed in within seconds each time (Hibr's log: "signed in at the
# login screen"); 0.3.127 was deployed with nobody there, and the check
# failed a healthy box. The rollback rehearsal's repaired box (no display
# before, its display back after) failed the old "the same displays" rule.
smoke_check() {
    s_version=$1
    # shellcheck disable=SC2086
    set -- $baseline
    b_ready=$1 b_displays=$2 b_attached=$3
    # "API only, as before" only after a grace: a box read just after its
    # restart may not have its displays yet, and a rollback by hand of a
    # box that had lost them must see them come back, not pass without.
    api_only_after=10
    [ "$smoke_seconds" -gt 10 ] || api_only_after=$((smoke_seconds - 1))
    now=
    i=0
    while [ "$i" -lt "$smoke_seconds" ]; do
        if now=$(smoke_state); then
            # shellcheck disable=SC2086
            set -- $now
            first_frames=$4
            if [ "$1" -ge "$b_ready" ] && [ "$2" -ge "$b_displays" ] &&
               { [ "$3" -ge "$b_attached" ] || [ "$5" = 1 ]; }; then
                if [ "$1" = 0 ]; then
                    if [ "$i" -ge "$api_only_after" ]; then
                        echo "smoke passed: API only, as before; $3 panes showing a surface"
                        return 0
                    fi
                elif sleep 1 && later=$(smoke_state); then
                    # shellcheck disable=SC2086
                    set -- $later
                    [ "$4" -gt "$first_frames" ] && {
                        if [ "$b_ready" = 0 ]; then
                            echo "smoke passed: $2 displays up (none before), $3 panes showing a surface, frames advancing ($first_frames -> $4)"
                        elif [ "$3" -ge "$b_attached" ]; then
                            echo "smoke passed: $2 displays up, $3 panes showing a surface (before $b_attached), frames advancing ($first_frames -> $4)"
                        else
                            echo "smoke passed: $2 displays up, the login screen up (the desktop, $b_attached panes before, waits for a sign-in), frames advancing ($first_frames -> $4)"
                        fi
                        return 0
                    }
                fi
            fi
        else
            now=
        fi
        sleep 1
        i=$((i + 1))
    done
    echo "smoke failed: installed Blit $s_version, but the box is not as it was within $smoke_seconds s:" \
         "before (ready displays panes-with-surface frames login-screen-up) $baseline, now ${now:-unreadable}" >&2
    return 1
}

install_pair "$package_version" "$version" "$parts" "$signature" "$tmpdir/body" || exit 1
smoke_check "$version" && exit 0

if [ -z "$rollback_package" ]; then
    echo "Blit $version is still installed. To roll back by hand (docs/security-review-2026-11.md, Runbook):" \
         "BLIT_SMOKE_BASELINE='$baseline' $0 $host <the previous blit .deb>;" \
         "--rollback-to <the previous blit .deb> does it on the next run" >&2
    exit 1
fi
echo "rolling back to Blit $old_version, the pair the box ran before, signed by its own .sig" >&2
if install_pair "$old_package_version" "$old_version" "$old_parts" "$old_signature" "$tmpdir/rollback-body" &&
   smoke_check "$old_version"; then
    echo "rolled back: Blit $old_version is installed again and the box is as it was; Blit $version failed its smoke check and was not kept" >&2
    exit 3
fi
echo "rollback failed: Blit $version failed its smoke check, and Blit $old_version could not be installed again or failed it too;" \
     "the box needs the runbook (docs/security-review-2026-11.md); the state before the update was '$baseline'" >&2
exit 4
