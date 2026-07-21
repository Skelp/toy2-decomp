"""Compatibility entry point for the platform decompilation commands.

New contributors should prefer tools/decomp on Linux or tools/decomp.ps1 on
Windows. This wrapper remains for existing workflows that invoke build.py.
"""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


PROJECT_NAME = "toy2"
ROOT = Path(__file__).resolve().parent
BUILD_FOLDER = ROOT / "build"


def track_process(command: list[str], custom_name: str = "", cwd: Path = ROOT) -> None:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    name = custom_name or command[0]
    assert process.stdout is not None
    for line in process.stdout:
        print(f"[{name}]: {line.rstrip()}")
    return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, command)


def decomp_command(command: str) -> list[str]:
    if os.name == "nt":
        return [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / "tools" / "decomp.ps1"),
            command,
        ]
    return [str(ROOT / "tools" / "decomp"), command]


def build_tools(build_type: str) -> None:
    if os.name != "nt":
        raise SystemExit("The optional injected/debugging tools currently build on Windows only.")
    tools_build = BUILD_FOLDER / "build_tools"
    track_process(
        [
            "cmake",
            "-S",
            str(ROOT / "tools"),
            "-B",
            str(tools_build),
            "-G",
            "Ninja",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
        ],
        "configure-tools",
    )
    track_process(["cmake", "--build", str(tools_build)], "build-tools")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build Toy Story 2 decompilation targets.")
    parser.add_argument("--nl", action="store_true", help="Do not launch the game after building")
    parser.add_argument("--cs", action="store_true", help="Print the executable size after building")
    parser.add_argument("--tools", action="store_true", help="Build the optional Windows helper tools")
    parser.add_argument(
        "--bt",
        choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
        default="RelWithDebInfo",
        help="Build type for optional helper tools; the matching game build uses RelWithDebInfo",
    )
    args = parser.parse_args()

    if args.tools:
        build_tools(args.bt)
        return 0

    if args.bt != "RelWithDebInfo":
        parser.error("The matching game build is fixed to RelWithDebInfo")
    track_process(decomp_command("build"), "build")
    executable = BUILD_FOLDER / "toy2.exe"
    if args.cs:
        size = executable.stat().st_size
        print(f"[size]: Executable is {size / 1024:.2f} KiB ({size / 1024 / 1024:.2f} MiB)")
    if not args.nl and not args.cs:
        track_process(decomp_command("run"), "run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
