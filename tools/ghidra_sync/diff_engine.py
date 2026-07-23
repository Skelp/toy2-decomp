"""Compare Ghidra and source annotations to produce a diff report."""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from decomp_annotations import read_source_annotations
from .models import (
    GhidraFunction,
    SourceAnnotation,
    SyncEntry,
    SyncStatus,
)
from .ghidra_client import get_function
from .reccmp_reader import load_match_scores, get_match_score


def _normalize_address(address: str) -> str:
    """Normalize an address to 0x00414720 format."""
    addr = address.strip().lower()
    if addr.startswith("0x"):
        addr = addr[2:]
    return f"0x{int(addr, 16):08x}"


def _extract_name_from_source(annotation: SourceAnnotation, source_text: str) -> Optional[str]:
    """Try to extract a meaningful name from the source context around an annotation.

    Looks at the function signature line following the annotation.
    """
    lines = source_text.split('\n')
    # Find the line after the annotation
    for i in range(annotation.line, min(annotation.line + 20, len(lines))):
        line = lines[i].strip()
        # Skip empty lines, comments, braces
        if not line or line.startswith('//') or line in ('{', '}', '}', 'void', 'int32_t', 'int', 'uint32_t', 'bool', 'char', 'float', 'double'):
            continue
        # Look for function name pattern: Name( or Name::Name(
        import re
        match = re.match(r'(?:[\w:]+::)?(\w+)\s*\(', line)
        if match:
            return match.group(1)
        break
    return None


def load_source_annotations() -> dict[str, SourceAnnotation]:
    """Load all source annotations.

    Returns:
        Dict mapping normalized address to SourceAnnotation.
    """
    src_path = Path("src")
    annotations = read_source_annotations(src_path)

    result = {}
    for ann in annotations:
        addr = _normalize_address(ann.address)
        result[addr] = SourceAnnotation(
            address=addr,
            kind=ann.kind,
            source=ann.source,
            line=ann.line,
        )
    return result


def load_functions_map() -> dict[str, str]:
    """Load the IDA functions_map.txt.

    Returns:
        Dict mapping normalized address to function name.
    """
    map_path = Path("tools/Resources/functions_map.txt")
    if not map_path.exists():
        return {}

    result = {}
    import re
    with open(map_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            match = re.match(r'^(?:0x)?([0-9A-Fa-f]{6,8})(?:\s+(.+))?$', line)
            if match:
                addr = _normalize_address(match.group(1))
                name = match.group(2).strip() if match.group(2) else ""
                result[addr] = name
    return result


def compare_address(
    address: str,
    ghidra_func: Optional[GhidraFunction],
    source_ann: Optional[SourceAnnotation],
    match_score: float = 0.0,
) -> SyncEntry:
    """Compare a single address between Ghidra and source.

    Args:
        address: Normalized address
        ghidra_func: GhidraFunction or None
        source_ann: SourceAnnotation or None
        match_score: reccmp match score (0.0 to 1.0)

    Returns:
        SyncEntry with the appropriate status.
    """
    if ghidra_func is None and source_ann is None:
        return SyncEntry(
            address=address,
            ghidra=None,
            source=None,
            status=SyncStatus.MISSING,
            match_score=match_score,
        )

    if ghidra_func is None:
        return SyncEntry(
            address=address,
            ghidra=None,
            source=source_ann,
            status=SyncStatus.ORPHAN,
            match_score=match_score,
        )

    if source_ann is None:
        return SyncEntry(
            address=address,
            ghidra=ghidra_func,
            source=None,
            status=SyncStatus.MISSING,
            match_score=match_score,
        )

    # Both exist — determine status
    gh_is_unnamed = ghidra_func.is_unnamed
    src_is_stub = source_ann.is_stub
    src_is_function = source_ann.is_function

    if gh_is_unnamed and src_is_stub:
        status = SyncStatus.GHIDRA_UNNAMED
    elif gh_is_unnamed and src_is_function:
        # Ghidra has FUN_*, source has a real function.
        # If match is 100%, this is pullable to Ghidra.
        if match_score >= 1.0:
            status = SyncStatus.PULLABLE
        else:
            status = SyncStatus.GHIDRA_UNNAMED
    elif not gh_is_unnamed and src_is_stub:
        # Ghidra has a real name but source is STUB
        status = SyncStatus.GHIDRA_HAS_NAME_STUB_SOURCE
    elif not gh_is_unnamed and src_is_function:
        # Both have names — check if they match
        # They match if Ghidra already has the same name as reccmp reports
        status = SyncStatus.OK
    else:
        status = SyncStatus.MISSING

    return SyncEntry(
        address=address,
        ghidra=ghidra_func,
        source=source_ann,
        status=status,
        match_score=match_score,
    )


def build_diff(
    target_addresses: Optional[list[str]] = None,
) -> list[SyncEntry]:
    """Build a full diff report.

    Args:
        target_addresses: If provided, only check these addresses.
                         Otherwise, checks all addresses in functions_map.txt.

    Returns:
        List of SyncEntry objects sorted by address.
    """
    # Load source annotations
    source_annotations = load_source_annotations()

    # Load functions map for target list
    functions_map = load_functions_map()

    # Load match scores first (needed for status determination)
    scores = load_match_scores()

    if target_addresses:
        addresses = [_normalize_address(a) for a in target_addresses]
    else:
        addresses = sorted(functions_map.keys())

    entries = []
    for addr in addresses:
        ghidra_func = get_function(addr)
        source_ann = source_annotations.get(addr)
        match = get_match_score(addr, scores)
        entry = compare_address(addr, ghidra_func, source_ann, match)
        entries.append(entry)

    return entries


def format_diff_report(entries: list[SyncEntry]) -> str:
    """Format a diff report as a human-readable string.

    Args:
        entries: List of SyncEntry objects

    Returns:
        Formatted report string.
    """
    from colorama import Fore, Style, init
    init(autoreset=True)

    lines = []
    lines.append("")
    lines.append("=" * 80)
    lines.append("GHIDRA SYNC DIFF REPORT")
    lines.append("=" * 80)
    lines.append("")
    lines.append(
        f"{'Address':<12} {'Ghidra Name':<30} {'Source':<25} {'Status':<18}"
    )
    lines.append("-" * 85)

    status_colors = {
        SyncStatus.OK: Fore.GREEN,
        SyncStatus.PULLABLE: Fore.GREEN,
        SyncStatus.GHIDRA_HAS_NAME_STUB_SOURCE: Fore.RED,
        SyncStatus.MISSING: Fore.RED,
        SyncStatus.ORPHAN: Fore.YELLOW,
        SyncStatus.GHIDRA_UNNAMED: Fore.CYAN,
        SyncStatus.SOURCE_UNNAMED: Fore.CYAN,
    }

    for entry in entries:
        addr = entry.address

        # Ghidra name
        if entry.ghidra:
            gh_name = entry.ghidra.name
            if entry.ghidra.is_unnamed:
                gh_name = f"{Fore.CYAN}{gh_name}{Style.RESET_ALL}"
        else:
            gh_name = f"{Fore.RED}—{Style.RESET_ALL}"

        # Source info
        if entry.source:
            src_info = _source_display(entry.source)
        else:
            src_info = f"{Fore.RED}—{Style.RESET_ALL}"

        # Status
        color = status_colors.get(entry.status, Fore.WHITE)
        status_str = f"{color}{entry.status.value}{Style.RESET_ALL}"

        # Match score
        if entry.match_score > 0:
            score = f" ({entry.match_score:.0%})"
        else:
            score = ""

        lines.append(
            f"{addr:<12} {gh_name:<30} {src_info:<25} {status_str}{score}"
        )

    # Summary
    total = len(entries)
    ok_count = sum(1 for e in entries if e.status == SyncStatus.OK)
    pullable_count = sum(1 for e in entries if e.status == SyncStatus.PULLABLE)
    mismatch_count = sum(1 for e in entries if e.status == SyncStatus.GHIDRA_HAS_NAME_STUB_SOURCE)
    unnamed_count = sum(1 for e in entries if e.status == SyncStatus.GHIDRA_UNNAMED)
    missing_count = sum(1 for e in entries if e.status == SyncStatus.MISSING)
    orphan_count = sum(1 for e in entries if e.status == SyncStatus.ORPHAN)

    lines.append("-" * 85)
    lines.append(f"Total: {total}  |  "
                 f"{Fore.GREEN}OK: {ok_count}{Style.RESET_ALL}  |  "
                 f"{Fore.GREEN}Pullable to Ghidra: {pullable_count}{Style.RESET_ALL}  |  "
                 f"{Fore.CYAN}Unnamed: {unnamed_count}{Style.RESET_ALL}  |  "
                 f"{Fore.RED}Mismatch: {mismatch_count}{Style.RESET_ALL}  |  "
                 f"{Fore.RED}Missing: {missing_count}{Style.RESET_ALL}  |  "
                 f"{Fore.YELLOW}Orphan: {orphan_count}{Style.RESET_ALL}")
    lines.append("=" * 80)
    lines.append("")

    return "\n".join(lines)


def _source_display(source: SourceAnnotation) -> str:
    """Get a display string for a source annotation."""
    if source.kind == "function":
        return f"[FUNC] {source.source}:{source.line}"
    elif source.kind == "stub":
        return f"[STUB] {source.source}:{source.line}"
    elif source.kind == "global":
        return f"[GLOBAL] {source.source}:{source.line}"
    return f"[{source.kind}] {source.source}:{source.line}"
