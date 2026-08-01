#!/usr/bin/env python3
"""Search the large reconstruction note files without loading their full text."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "codegen": (ROOT / ".notes/codegen-patterns.md",),
    "debt": (ROOT / ".notes/refactor-debt.md",),
    "names": (ROOT / ".notes/original-names.md",),
}


@dataclass(order=True)
class Match:
    score: int
    path: Path
    line: int
    text: str


def search(query: str, source: str = "all", limit: int = 8) -> list[Match]:
    terms = [term.casefold() for term in re.findall(r"[A-Za-z0-9_:.-]+", query)]
    paths = tuple(dict.fromkeys(path for key, group in SOURCES.items() if source in ("all", key) for path in group))
    matches: list[Match] = []
    for path in paths:
        if not path.exists():
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            folded = text.casefold()
            hits = sum(term in folded for term in terms)
            if not hits:
                continue
            exact = 4 if query.casefold() in folded else 0
            heading = 2 if text.lstrip().startswith("#") else 0
            matches.append(Match(-(hits * 10 + exact + heading), path, number, text.strip()))
    matches.sort()
    return matches if limit == 0 else matches[:limit]


def main() -> int:
    parser = argparse.ArgumentParser(description="Search bounded reconstruction notes.")
    parser.add_argument("query")
    parser.add_argument("--source", choices=(*SOURCES, "all"), default="all")
    parser.add_argument("--limit", type=int, default=8, help="maximum matches (0 = all)")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("--limit must be zero or greater")
    matches = search(args.query, args.source, args.limit)
    if not matches:
        print("No note matches found.")
        return 0
    for item in matches:
        print(f"{item.path.relative_to(ROOT)}:{item.line}: {item.text}")
    if args.limit and len(search(args.query, args.source, 0)) > len(matches):
        print("More matches exist. Use --limit 0 to show all matches.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
