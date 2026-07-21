#!/usr/bin/env bash

# Source this file to activate the Linux-hosted VC6 SP3 decompilation
# environment. The compiler and linker run through the project-local Wine
# prefix; CMake, Ninja, Python, and reccmp run natively.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Source this script instead of executing it:" >&2
    echo "  source tools/linux-decomp-env.sh" >&2
    exit 1
fi

TOY2_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TOY2_TOOLING="$TOY2_ROOT/.tooling"
TOY2_MSVC_BASE="$TOY2_TOOLING/msvc600-8168"
TOY2_MSVC_SP3="$TOY2_TOOLING/msvc600-8447"

for required in \
    "$TOY2_MSVC_BASE/activate_x86" \
    "$TOY2_MSVC_SP3/VC98/lib/libc.lib" \
    "$TOY2_TOOLING/venv/bin/reccmp-reccmp" \
    "$TOY2_TOOLING/wineprefix/system.reg"; do
    if [[ ! -e "$required" ]]; then
        echo "Missing decompilation dependency: $required" >&2
        echo "Run tools/setup-linux-decomp.sh first." >&2
        return 1
    fi
done

export WINEPREFIX="$TOY2_TOOLING/wineprefix"
export WINEDEBUG="${WINEDEBUG:--all}"

# Wait for each Wine-hosted tool itself instead of the wrapper package's
# optional FIFO forwarding helper. The helper can return before VC6 closes its
# object files, leaving Ninja with stale dependency timestamps and causing
# needless recompilation on the next invocation.
export WINE_MSVC_RAW_STDOUT=1

# The 8168 package supplies CL.EXE 12.00.8168, LINK.EXE 6.00.8168, and
# Linux/Wine wrappers. This is the compiler generation recorded in toy2.exe.
source "$TOY2_MSVC_BASE/activate_x86"

# Visual Studio 6 SP3 kept compiler build 8168 but updated the product CRT,
# headers, and libraries to build 8447. Both build numbers occur in the
# reference executable's Rich header. setup-linux-decomp.sh overlays those
# SP3 files onto the Wine-ready 8168 tree without replacing its compiler.
export PATH="$TOY2_TOOLING/venv/bin:$PATH"

unset required
