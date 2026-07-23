"""Data classes for Ghidra sync state."""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional


class SyncStatus(Enum):
    """Comparison status between Ghidra and source."""
    OK = "OK"                          # Names match (or Ghidra already has the source name)
    PULLABLE = "PULLABLE"              # Ghidra has FUN_*, 100% match, source has real name — pull to Ghidra
    GHIDRA_UNNAMED = "GHIDRA_UNNAMED"  # Ghidra still has FUN_*, match < 100% — not safe to pull
    SOURCE_UNNAMED = "SOURCE_UNNAMED"  # Source has STUB (unimplemented)
    GHIDRA_HAS_NAME_STUB_SOURCE = "GHIDRA_HAS_NAME_STUB_SOURCE"  # Ghidra has name, source is STUB
    SIGNATURE_MISMATCH = "SIGNATURE_MISMATCH"  # Names match but signature/calling convention differs
    MISSING = "MISSING"                # Address in Ghidra but not in source
    ORPHAN = "ORPHAN"                  # Address in source but not in Ghidra


@dataclass(frozen=True)
class GhidraFunction:
    """A function as known from Ghidra."""
    address: str                       # e.g. "0x00414720"
    name: str                          # e.g. "FUN_00414720" or "InitLevelPlay"
    calling_convention: str
    comment: Optional[str]
    signature: str
    size: int

    @property
    def is_unnamed(self) -> bool:
        return self.name.startswith("FUN_")


@dataclass(frozen=True)
class SourceAnnotation:
    """A function annotation as found in source code."""
    address: str                       # e.g. "0x00414720"
    kind: str                          # "function", "stub", "global"
    source: str                        # e.g. "src/Toy2/Toy2.cpp"
    line: int
    name: Optional[str] = None         # Extracted from surrounding context if available
    signature: Optional[str] = None    # Full C-style signature, e.g. "void SetTint(uint8_t, uint8_t, uint8_t, uint8_t)"
    calling_convention: Optional[str] = None  # e.g. "__stdcall"

    @property
    def is_stub(self) -> bool:
        return self.kind == "stub"

    @property
    def is_function(self) -> bool:
        return self.kind == "function"


@dataclass
class SyncEntry:
    """A single address comparison entry for the diff report."""
    address: str
    ghidra: Optional[GhidraFunction]
    source: Optional[SourceAnnotation]
    status: SyncStatus
    match_score: float = 0.0           # From reccmp, 0.0 to 1.0

    def short_name(self) -> str:
        """Get a short display name for this entry."""
        if self.ghidra and self.source:
            gh_name = self.ghidra.name
            src_name = self._source_display_name()
            if gh_name == src_name:
                return gh_name
            return f"{gh_name} / {src_name}"
        elif self.ghidra:
            return self.ghidra.name
        elif self.source:
            return self._source_display_name()
        return "<unknown>"

    def _source_display_name(self) -> str:
        if self.source and self.source.kind == "function":
            return self.source.name or "unnamed"
        elif self.source and self.source.kind == "stub":
            return "STUB"
        return "STUB"


@dataclass
class SyncAction:
    """A proposed change to synchronize Ghidra and source."""
    action_type: str                   # "push_name", "push_comment", "pull_name", "pull_comment"
    address: str
    description: str
    current_value: str
    new_value: str
    source_file: Optional[str] = None  # For push operations
    ghidra_target: Optional[str] = None  # For pull operations


@dataclass
class SyncReport:
    """Complete sync report for an address or all addresses."""
    entries: list[SyncEntry] = field(default_factory=list)
    actions: list[SyncAction] = field(default_factory=list)
    summary: str = ""
