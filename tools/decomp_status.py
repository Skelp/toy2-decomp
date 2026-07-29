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
_IMMEDIATE_OPERAND = re.compile(r"^(?:[-+])?0x[0-9a-fA-F]+$")


def _instruction_parts(text: str) -> tuple[str, list[str]]:
	parts = text.split(None, 1)
	opcode = parts[0].lower() if parts else ""
	operands = [] if len(parts) == 1 else [item.strip() for item in parts[1].split(",")]
	return opcode, operands


def _symbol_operand(value: str) -> bool:
	return bool(_SYMBOL_MARKER.search(value) or _IMMEDIATE_OPERAND.fullmatch(value))


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
				original_opcode, original_operands = _instruction_parts(original_text)
				recompiled_opcode, recompiled_operands = _instruction_parts(recompiled_text)
				if original_opcode != recompiled_opcode or len(original_operands) != len(recompiled_operands):
					return False
				different = [
					index
					for index, operands in enumerate(zip(original_operands, recompiled_operands))
					if operands[0] != operands[1]
				]
				if len(different) != 1:
					return False
				index = different[0]
				if not (
					_symbol_operand(original_operands[index])
					and _symbol_operand(recompiled_operands[index])
				):
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
