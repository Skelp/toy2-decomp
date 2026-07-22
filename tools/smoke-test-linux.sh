#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_SHA256="023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0"
REGISTRY_KEY='HKLM\Software\TravellersTalesToyStory2'
WOW64_REGISTRY_KEY='HKLM\Software\Wow6432Node\TravellersTalesToyStory2'

usage() {
    echo "Usage: tools/smoke-test-linux.sh <installed-game-directory> [timeout-seconds]" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 2
fi

GAME_DIR="$(realpath -- "$1")"
TIMEOUT_SECONDS="${2:-20}"

if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Timeout must be a positive integer." >&2
    exit 2
fi

for required in toy2.exe validate.tta data; do
    if [[ ! -e "$GAME_DIR/$required" ]]; then
        echo "Missing runtime file or directory: $GAME_DIR/$required" >&2
        exit 1
    fi
done

actual_sha256="$(sha256sum "$GAME_DIR/toy2.exe" | awk '{print $1}')"
if [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
    echo "Unsupported toy2.exe in $GAME_DIR" >&2
    echo "Expected SHA-256: $EXPECTED_SHA256" >&2
    echo "Actual SHA-256:   $actual_sha256" >&2
    exit 1
fi

if [[ ! -f "$ROOT/build/toy2.exe" ]]; then
    "$ROOT/tools/decomp" build
fi

source "$ROOT/tools/linux-decomp-env.sh"

SMOKE_ROOT="$(mktemp -d /tmp/toy2-smoke.XXXXXX)"
SMOKE_GAME="$SMOKE_ROOT/game"
NATIVE_BACKUP="$SMOKE_ROOT/native.reg"
WOW64_BACKUP="$SMOKE_ROOT/wow64.reg"
RUN_LOG="$SMOKE_ROOT/runtime.log"

mkdir -p "$SMOKE_GAME"

backup_registry_key() {
    local key="$1"
    local output="$2"
    if wine reg query "$key" >/dev/null 2>&1; then
        local windows_output
        windows_output="$(winepath -w "$output")"
        wine reg export "$key" "$windows_output" /y >/dev/null
    fi
}

restore_registry_key() {
    local key="$1"
    local backup="$2"
    wine reg delete "$key" /f >/dev/null 2>&1 || true
    if [[ -f "$backup" ]]; then
        local windows_backup
        windows_backup="$(winepath -w "$backup")"
        wine reg import "$windows_backup" >/dev/null
    fi
}

cleanup() {
    trap - EXIT INT TERM
    local restore_failed=0
    restore_registry_key "$REGISTRY_KEY" "$NATIVE_BACKUP" || restore_failed=1
    restore_registry_key "$WOW64_REGISTRY_KEY" "$WOW64_BACKUP" || restore_failed=1
    if [[ $restore_failed -eq 0 && "$SMOKE_ROOT" == /tmp/toy2-smoke.* ]]; then
        rm -rf -- "$SMOKE_ROOT"
    elif [[ $restore_failed -ne 0 ]]; then
        echo "Unable to restore Wine registry state; backups remain in $SMOKE_ROOT" >&2
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

backup_registry_key "$REGISTRY_KEY" "$NATIVE_BACKUP"
backup_registry_key "$WOW64_REGISTRY_KEY" "$WOW64_BACKUP"

echo "Staging a temporary runtime copy..."
cp -a "$GAME_DIR/." "$SMOKE_GAME/"
cp "$ROOT/build/toy2.exe" "$SMOKE_GAME/toy2.exe"
rm -f -- "$SMOKE_GAME/toy2.err"

windows_game_path="$(winepath -w "$SMOKE_GAME")\\"
for key in "$REGISTRY_KEY" "$WOW64_REGISTRY_KEY"; do
    wine reg add "$key" /v path /t REG_SZ /d "$windows_game_path" /f >/dev/null
    wine reg add "$key" /v cdpath /t REG_SZ /d "$windows_game_path" /f >/dev/null
done

echo "Launching the rebuilt game for ${TIMEOUT_SECONDS}s; a window may appear briefly."
set +e
(
    cd "$SMOKE_GAME"
    timeout --signal=TERM --kill-after=5s "${TIMEOUT_SECONDS}s" wine toy2.exe
) 2>&1 | tee "$RUN_LOG"
run_status=${PIPESTATUS[0]}
set -e

if [[ -s "$SMOKE_GAME/toy2.err" ]]; then
    echo "Runtime smoke test produced toy2.err:" >&2
    cat "$SMOKE_GAME/toy2.err" >&2
    exit 1
fi

if ! grep -q "BEGIN SELECT DRIVERS" "$RUN_LOG"; then
    echo "The game did not reach Direct3D driver selection." >&2
    exit 1
fi

if [[ $run_status -ne 0 && $run_status -ne 124 ]]; then
    echo "The game exited unexpectedly with status $run_status." >&2
    exit "$run_status"
fi

echo "Runtime smoke test passed: Direct3D driver selection was reached."
