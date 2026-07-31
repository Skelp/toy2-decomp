#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLING="$ROOT/.tooling"
MSVC_BASE="$TOOLING/msvc600-8168"
MSVC_SP3="$TOOLING/msvc600-8447"
WINEPREFIX="$TOOLING/wineprefix"
REFERENCE_SHA256="023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0"
MSVC_BASE_COMMIT="93d5bbe2582aa5b78a3a8dddd9498530fdd74061"
MSVC_SP3_COMMIT="0bc2b2684140e1516567a818baed13e8add1ff5f"

if [[ $# -ne 0 ]]; then
    echo "Usage: $0" >&2
    echo "Place the supported executable at original/toy2.exe before setup." >&2
    exit 2
fi

REFERENCE="$ROOT/original/toy2.exe"
if [[ ! -f "$REFERENCE" ]]; then
    echo "Reference executable not found: original/toy2.exe" >&2
    echo "Copy your genuine supported toy2.exe there before setup." >&2
    exit 1
fi

actual_sha256="$(sha256sum "$REFERENCE" | awk '{print $1}')"
if [[ "$actual_sha256" != "$REFERENCE_SHA256" ]]; then
    echo "Reference hash mismatch." >&2
    echo "Expected: $REFERENCE_SHA256" >&2
    echo "Actual:   $actual_sha256" >&2
    exit 1
fi

for command in git cmake ninja wine wineserver winepath python3.11 sha256sum; do
    if ! command -v "$command" >/dev/null; then
        echo "Required host command is missing: $command" >&2
        exit 1
    fi
done

mkdir -p "$TOOLING"

if [[ ! -d "$MSVC_BASE/.git" ]]; then
    git clone https://github.com/isledecomp/MSVC600-8168.git "$MSVC_BASE"
    git -C "$MSVC_BASE" checkout --detach "$MSVC_BASE_COMMIT"
elif [[ "$(git -C "$MSVC_BASE" rev-parse HEAD)" != "$MSVC_BASE_COMMIT" ]]; then
    echo "Unexpected MSVC600-8168 revision in $MSVC_BASE" >&2
    echo "Remove that tooling directory and rerun setup." >&2
    exit 1
fi

if [[ ! -d "$MSVC_SP3/.git" ]]; then
    git clone https://github.com/isledecomp/MSVC600-8447.git "$MSVC_SP3"
    git -C "$MSVC_SP3" checkout --detach "$MSVC_SP3_COMMIT"
elif [[ "$(git -C "$MSVC_SP3" rev-parse HEAD)" != "$MSVC_SP3_COMMIT" ]]; then
    echo "Unexpected MSVC600-8447 revision in $MSVC_SP3" >&2
    echo "Remove that tooling directory and rerun setup." >&2
    exit 1
fi

# Apply the SP3 product headers, CRT, and libraries over the RTM compiler
# tree. Preserve the 8168 directories as backups, then replace each directory
# wholesale: merely copying lower-case SP3 names beside upper-case RTM names
# is ambiguous to Wine on a case-sensitive Linux filesystem.
overlay_sp3_dir() {
    local source="$1"
    local destination="$2"
    if [[ ! -d "$destination-8168" ]]; then
        mv "$destination" "$destination-8168"
        mkdir -p "$destination"
    fi
    cp -a "$source/." "$destination/"
}

overlay_sp3_dir "$MSVC_SP3/VC98/include" "$MSVC_BASE/VC98/Include"
overlay_sp3_dir "$MSVC_SP3/VC98/lib" "$MSVC_BASE/VC98/Lib"
overlay_sp3_dir "$MSVC_SP3/VC98/atl/include" "$MSVC_BASE/VC98/ATL/Include"
overlay_sp3_dir "$MSVC_SP3/VC98/mfc/include" "$MSVC_BASE/VC98/MFC/Include"
overlay_sp3_dir "$MSVC_SP3/VC98/mfc/lib" "$MSVC_BASE/VC98/MFC/Lib"

# The pinned Wine wrapper accidentally references its maintainer's local copy
# of the SBR dependency parser. Point it at the identical bundled parser.
sed -i 's|/home/maarten/programming/resbr/parse_sbr.py|"$(dirname "$0")/sbr2inc.py"|' \
    "$MSVC_BASE/wine/x86/cl"

if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
    WINEPREFIX="$WINEPREFIX" WINEDEBUG=-all wineboot -u
    WINEPREFIX="$WINEPREFIX" wineserver -w
fi

if [[ -d "$TOOLING/venv" && ! -x "$TOOLING/venv/bin/python" ]]; then
    echo "The existing .tooling/venv is not a Linux Python environment." >&2
    echo "Move it aside before rerunning setup." >&2
    exit 1
fi
if [[ ! -x "$TOOLING/venv/bin/python" ]]; then
    python3.11 -m venv "$TOOLING/venv"
fi
RECCMP_SUBMODULE="$ROOT/external/submodules/reccmp"
if [[ ! -d "$RECCMP_SUBMODULE/.git" ]]; then
    git submodule update --init --recursive "$RECCMP_SUBMODULE"
elif [[ "$(git -C "$RECCMP_SUBMODULE" rev-parse HEAD)" != "21416ad1938f5c372a578ef7da5175e99695ed07" ]]; then
    echo "Unexpected reccmp submodule revision in $RECCMP_SUBMODULE" >&2
    echo "Remove that directory and rerun setup, or run:" >&2
    echo "  git submodule update --init --recursive external/submodules/reccmp" >&2
    exit 1
fi

# Install reccmp from the pinned submodule in editable mode so the local
# patches below take effect. The submodule is pinned to
# upstream master (commit 21416ad1), which carries the evolved Ghidra importer
# used by `tools/decomp sync`.
"$TOOLING/venv/bin/python" -m pip install -e "$RECCMP_SUBMODULE" colorama==0.4.6

# Apply local reccmp patches against the submodule worktree.
# 1. WANT_CURLY parser fix: the state silently gets stuck on one-line function
#    bodies (e.g. "{ return foo(); }"). Still required on upstream master.
# 2. Union datatype write support: upstream only dereferences existing unions
#    and aborts otherwise; this project needs unions created by the importer.
# 3. Variadic function support: import fixed parameters and set Ghidra's
#    variadic flag instead of rejecting the complete signature.
# 4. Containing global refresh: remove stale generated data that contains a
#    corrected global's start address.
apply_reccmp_patch() {
    local patch_file="$1"
    local name
    name="$(basename "$patch_file" .patch)"
    if patch --dry-run -p1 -d "$RECCMP_SUBMODULE" < "$patch_file" >/dev/null 2>&1; then
        patch -p1 -d "$RECCMP_SUBMODULE" < "$patch_file"
    else
        echo "reccmp patch '$name' already applied or does not fit — skipping." >&2
    fi
}
apply_reccmp_patch "$ROOT/tools/patches/reccmp-0.1.6-want-curly-fix.patch"
apply_reccmp_patch "$ROOT/tools/patches/reccmp-union-write.patch"
apply_reccmp_patch "$ROOT/tools/patches/reccmp-variadic-functions.patch"
apply_reccmp_patch "$ROOT/tools/patches/reccmp-containing-global-refresh.patch"

# Recover the SDK bundle previously used by this project. The DirectDraw and
# Direct3D 3 interfaces used by the game are ABI-compatible with these headers.
python3.11 "$ROOT/tools/provision-directx.py" --root "$ROOT"

cd "$ROOT"
"$TOOLING/venv/bin/reccmp-project" detect --search-path original

echo
echo "Linux decompilation environment is ready."
echo "Build with:   tools/decomp build"
echo "Compare with: tools/decomp compare"
