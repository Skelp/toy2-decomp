"""Read reccmp match data from the build report JSON."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Optional

from .models import SyncEntry


def _normalize_address(address: str) -> str:
    """Normalize an address to 0x00414720 format."""
    addr = address.strip().lower()
    if addr.startswith("0x"):
        addr = addr[2:]
    return f"0x{int(addr, 16):08x}"


def load_match_scores(report_path: Optional[str] = None) -> dict[str, float]:
    """Load match scores from the reccmp report JSON.

    Args:
        report_path: Path to decomp-report-data.json. Defaults to build/decomp-report-data.json.

    Returns:
        Dict mapping normalized address to match score (0.0 to 1.0).
    """
    if report_path is None:
        report_path = str(Path("build/decomp-report-data.json"))

    path = Path(report_path)
    if not path.exists():
        print(f"Warning: {path} not found. Run `tools/decomp compare` first.", file=sys.stderr)
        return {}

    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        items = data.get("data", [])
        scores = {}
        for item in items:
            addr = _normalize_address(item.get("address", ""))
            score = item.get("matching", 0.0)
            if addr:
                scores[addr] = score
        return scores
    except (json.JSONDecodeError, KeyError) as e:
        print(f"Warning: Could not parse {path}: {e}", file=sys.stderr)
        return {}


def get_match_score(address: str, scores: dict[str, float]) -> float:
    """Get the match score for a specific address.

    Args:
        address: Address string
        scores: Pre-loaded match scores dict

    Returns:
        Match score (0.0 to 1.0), or 0.0 if not found.
    """
    addr = _normalize_address(address)
    return scores.get(addr, 0.0)


def enrich_with_match_scores(
    entries: list[SyncEntry],
    scores: dict[str, float]
) -> list[SyncEntry]:
    """Add match scores to sync entries.

    Args:
        entries: List of SyncEntry objects
        scores: Pre-loaded match scores dict

    Returns:
        Same list with match_score populated.
    """
    for entry in entries:
        entry.match_score = get_match_score(entry.address, scores)
    return entries
