"""Rewrite VC6 compiler arguments for the repository launcher."""

from __future__ import annotations

import os
import sys


def rewrite_arguments(arguments: list[str]) -> list[str]:
    object_path = ""
    for argument in arguments:
        if argument.startswith(("/Fo", "-Fo")):
            object_path = argument[3:]

    rewritten = []
    for argument in arguments:
        if object_path and argument.startswith(("/Fd", "-Fd")):
            argument = f"/Fd{object_path}.pdb"
        rewritten.append(argument)
    return rewritten


def main() -> int:
    for argument in rewrite_arguments(sys.argv[1:]):
        os.write(sys.stdout.fileno(), os.fsencode(argument) + b"\0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
