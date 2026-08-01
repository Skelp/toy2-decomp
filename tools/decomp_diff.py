#!/usr/bin/env python3
"""Print a concise reccmp verbose diff while preserving the complete output."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def mismatch_windows(lines: list[str], maximum: int = 3, context: int = 3) -> list[list[str]]:
    markers = [
        index for index, line in enumerate(lines)
        if re.search(r"(?:^|\s)(?:[-+!<>]|replace|insert|delete|mismatch)", line, re.I)
    ]
    windows: list[tuple[int, int]] = []
    for index in markers:
        start, end = max(0, index - context), min(len(lines), index + context + 1)
        if windows and start <= windows[-1][1]:
            windows[-1] = (windows[-1][0], max(windows[-1][1], end))
        elif len(windows) < maximum:
            windows.append((start, end))
    return [lines[start:end] for start, end in windows[:maximum]]


def summarize(text: str, path: Path) -> str:
    lines = text.splitlines()
    verdict = next((line for line in lines if re.search(r"(?:similar|match|effective|error)", line, re.I)), "No similarity verdict found.")
    change_indexes = [
        index for index, line in enumerate(lines)
        if re.search(r"(?:^|\s)(?:[-+!<>]|replace|insert|delete|mismatch)", line, re.I)
    ]
    hunk_count = sum(
        1 for offset, index in enumerate(change_indexes)
        if offset == 0 or index > change_indexes[offset - 1] + 1
    )
    windows = mismatch_windows(lines)
    output = [
        verdict,
        f"Saved complete diff: {path}",
        f"Mismatch hunks: {hunk_count}; change lines: {len(change_indexes)}; displayed windows: {len(windows)}",
    ]
    for number, window in enumerate(windows, 1):
        output.extend((f"\n--- mismatch {number} ---", *window))
    return "\n".join(output[:160]) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    text = args.path.read_text(encoding="utf-8", errors="replace")
    print(text if args.full else summarize(text, args.path), end="" if text.endswith("\n") else "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
