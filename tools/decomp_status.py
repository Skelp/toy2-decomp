#!/usr/bin/env python3
"""Shared verification-state helpers for Toy Story 2 decompilation tools."""

from __future__ import annotations

import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class MatchStatus:
	matching: float
	effective: bool = False
	name: str = ""
	diff: object | None = None

	@property
	def exact(self) -> bool:
		return self.matching == 1.0

	@property
	def binary_status(self) -> str:
		if self.matching == 1.0:
			return "exact"
		if self.effective:
			return "effective"
		if self.matching > 0.0:
			return "partial"
		return "zero"


def read_match_statuses(path: Path) -> dict[int, MatchStatus]:
	if not path.exists():
		return {}
	try:
		payload = json.loads(path.read_text(encoding="utf-8"))
	except (json.JSONDecodeError, OSError):
		return {}
	statuses: dict[int, MatchStatus] = {}
	for entry in payload.get("data", []):
		address = entry.get("address")
		matching = entry.get("matching")
		if address is None or matching is None:
			continue
		try:
			key = int(str(address), 16)
			statuses[key] = MatchStatus(
				matching=float(matching),
				effective=bool(entry.get("effective")),
				name=str(entry.get("name", "")),
				diff=entry.get("diff"),
			)
		except (TypeError, ValueError):
			continue
	return statuses


def read_tool_artifacts(path: Path) -> dict[int, str]:
	if not path.exists():
		return {}
	artifacts: dict[int, str] = {}
	with path.open(encoding="utf-8", newline="") as handle:
		for row in csv.reader(handle, delimiter="\t"):
			if not row or row[0].lstrip().startswith("#"):
				continue
			try:
				artifacts[int(row[0].strip(), 16)] = row[1].strip()
			except (IndexError, ValueError):
				continue
	return artifacts


_SYMBOL_MARKER = re.compile(r"(?:\((?:OFFSET|DATA|UNK)\)|<OFFSET\d*>)")
_IMMEDIATE_OPERAND = re.compile(r"(?:^|[, ])0x[0-9a-fA-F]+$")


def is_symbol_only_diff(status: MatchStatus) -> bool:
	"""Return true when each changed row only changes a rendered data symbol.

	The tracked tool-artifact ledger remains the allowlist. This check prevents
	register, frame, block-order, and instruction changes from entering it.
	"""

	if status.matching == 1.0 or status.effective or not isinstance(status.diff, list):
		return False
	changed = False
	for _, groups in status.diff:
		for group in groups:
			original = group.get("orig", [])
			recompiled = group.get("recomp", [])
			if not original and not recompiled:
				continue
			if len(original) != len(recompiled):
				return False
			for original_row, recompiled_row in zip(original, recompiled):
				original_text = original_row[1].split("\t", 1)[0].strip()
				recompiled_text = recompiled_row[1].split("\t", 1)[0].strip()
				original_symbol = _SYMBOL_MARKER.search(original_text)
				recompiled_symbol = _SYMBOL_MARKER.search(recompiled_text)
				if not (
					(original_symbol and recompiled_symbol)
					or (original_symbol and _IMMEDIATE_OPERAND.search(recompiled_text))
					or (recompiled_symbol and _IMMEDIATE_OPERAND.search(original_text))
				):
					return False
				if original_text.split(None, 1)[0] != recompiled_text.split(None, 1)[0]:
					return False
				changed = True
	return changed


def verification_status(
	status: MatchStatus | None, *, tool_artifact: bool = False, source_clean: bool = True
) -> str:
	if status is None:
		return "unmatched"
	if not source_clean:
		return "provisional"
	if status.matching == 1.0:
		return "exact"
	if status.effective:
		return "effective"
	if tool_artifact and is_symbol_only_diff(status):
		return "tool"
	return "provisional"
