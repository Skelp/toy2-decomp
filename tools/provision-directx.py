#!/usr/bin/env python3
"""Provision the ignored DirectX SDK files used by the game and helper tools."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path


SDK_COMMIT = "7fc7601b4aa43b8a9ba5724f21df18531ce5b33d"
GAME_LIBRARIES = ("ddraw.lib", "dinput.lib", "dxguid.lib")


def run_git(root: Path, *args: str, stdout=None) -> None:
    subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        stdout=stdout,
        stderr=subprocess.PIPE if stdout is not None else None,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Repository root")
    args = parser.parse_args()
    root = args.root.resolve()

    try:
        run_git(root, "cat-file", "-e", f"{SDK_COMMIT}^{{commit}}")
    except subprocess.CalledProcessError as exc:
        raise SystemExit(
            "The DirectX SDK snapshot is not present in this Git clone. "
            "Use a normal clone, or run 'git fetch --unshallow' before setup."
        ) from exc

    with tempfile.TemporaryDirectory(prefix="toy2-dxsdk-") as temp_name:
        temp = Path(temp_name)
        archive = temp / "sdk.tar"
        with archive.open("wb") as output:
            run_git(
                root,
                "archive",
                "--format=tar",
                SDK_COMMIT,
                "external/include/directx7",
                "external/libs",
                stdout=output,
            )
        with tarfile.open(archive) as sdk_tar:
            sdk_tar.extractall(temp / "files")

        extracted = temp / "files" / "external"
        headers = extracted / "include" / "directx7"
        libraries = extracted / "libs"
        directx6_include = root / "external" / "include" / "directx6"
        directx7_include = root / "external" / "include" / "directx7"
        directx6_lib = root / "external" / "libs" / "directx6"
        directx7_lib = root / "external" / "libs" / "directx7"

        for destination in (directx6_include, directx7_include):
            shutil.copytree(headers, destination, dirs_exist_ok=True)
        directx6_lib.mkdir(parents=True, exist_ok=True)
        directx7_lib.mkdir(parents=True, exist_ok=True)
        for source in libraries.glob("*.lib"):
            shutil.copy2(source, directx7_lib / source.name)
        for filename in GAME_LIBRARIES:
            shutil.copy2(libraries / filename, directx6_lib / filename)

    print(
        f"Provisioned DirectX headers and {len(list(directx7_lib.glob('*.lib')))} "
        "tool libraries."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
