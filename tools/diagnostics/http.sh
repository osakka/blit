#!/usr/bin/env bash

# Shared read-only HTTP transport for diagnostic captures. This file is
# sourced by the capture tools; it is not an installed runtime dependency.

diag_auth_token=''
diag_auth_mode='none'

diag_init_auth() {
    if [[ -n "${BLIT_AUTH_TOKEN:-}" && -n "${BLIT_AUTH_TOKEN_FILE:-}" ]]; then
        echo 'diagnostics: set only BLIT_AUTH_TOKEN or BLIT_AUTH_TOKEN_FILE' >&2
        return 2
    fi
    if [[ -n "${BLIT_AUTH_TOKEN:-}" ]]; then
        diag_auth_token=$BLIT_AUTH_TOKEN
        diag_auth_mode='bearer'
    elif [[ -n "${BLIT_AUTH_TOKEN_FILE:-}" ]]; then
        [[ -r "$BLIT_AUTH_TOKEN_FILE" ]] || {
            echo "diagnostics: token file is not readable: $BLIT_AUTH_TOKEN_FILE" >&2
            return 2
        }
        diag_auth_token=$(sed -n '1p' "$BLIT_AUTH_TOKEN_FILE" | sed 's/[[:space:]]*$//')
        [[ -n "$diag_auth_token" ]] || {
            echo 'diagnostics: token file is empty' >&2
            return 2
        }
        diag_auth_mode='bearer-file'
    fi
}

diag_curl() {
    if [[ -n "$diag_auth_token" ]]; then
        curl "$@" -H "Authorization: Bearer $diag_auth_token"
    else
        curl "$@"
    fi
}

diag_init_auth
