#!/usr/bin/env python3
"""Ghidra synchronization and map consistency for Toy Story 2.

The Ghidra sync is driven by upstream reccmp's headless importer
(`reccmp-ghidra-import`), which imports every matched function, global,
vftable, and PDB type into the local Ghidra project in a single transaction.
This wrapper resolves the project's Ghidra project location from the `ghidra`
CLI configuration, stops the interactive Ghidra bridge so the local project can
be opened exclusively, runs the importer, then restarts the bridge.

`check` validates the order, names, and retail code range in
`functions_map.txt`. It also checks saved retail body sizes for source-work
targets. A body must cover at least 80 percent of its map gap. The check does
not query Ghidra or rebuild the project.
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from typing import Optional

ROOT = Path(__file__).resolve().parents[1]
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"
EXE_PATH = ROOT / "original" / "toy2.exe"
BUILD_DIR = ROOT / "build"
SOURCE_ROOT = ROOT / "src"
FUNCTION_SIZES_PATH = BUILD_DIR / "decomp-function-sizes.json"
MINIMUM_COVERAGE_BODY_RATIO = 0.8

sys.path.insert(0, str(ROOT))
from tools.decomp_annotations import read_source_annotations  # noqa: E402

# Matches the address/name format produced by decomp_utils.parse_functions_map.
_MAP_LINE_RE = __import__("re").compile(r"^(?:0x)?([0-9A-Fa-f]{6,8})(?:\s+(.+))?$")


# --------------------------------------------------------------------------- #
# sync: reccmp-ghidra-import orchestration
# --------------------------------------------------------------------------- #

def _ghidra_config(name: str) -> str:
    result = subprocess.run(
        ["ghidra", "config", "get", name],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0 or not result.stdout.strip():
        raise RuntimeError(
            f"could not read Ghidra configuration key {name!r}; "
            "run `ghidra config set` first"
        )
    return result.stdout.strip()


def _bridge_running() -> bool:
    return subprocess.run(
        ["ghidra", "status"], capture_output=True, text=True, check=False
    ).returncode == 0


def _build_is_current() -> None:
    """Fail closed if Ninja would rebuild the comparison executable/PDB."""
    required = (
        BUILD_DIR / "toy2.exe",
        BUILD_DIR / "toy2.pdb",
        BUILD_DIR / "reccmp-build.yml",
    )
    missing = [str(p.relative_to(ROOT)) for p in required if not p.is_file()]
    if missing:
        raise RuntimeError(
            f"missing build artifacts: {', '.join(missing)}; run tools/decomp build"
        )
    result = subprocess.run(
        ["ninja", "-C", str(BUILD_DIR), "-n", "toy2decomp"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    output = (result.stdout + result.stderr).strip()
    if result.returncode != 0:
        raise RuntimeError(f"could not validate build freshness: {output}")
    if "no work to do" not in output.lower():
        raise RuntimeError("comparison artifacts are stale; run tools/decomp build")


def cmd_sync(args: SimpleNamespace) -> int:
    # `--help` (or `-h`) is forwarded to reccmp-ghidra-import without touching
    # the build or the Ghidra bridge, so introspection stays cheap and safe.
    if any(token in args.remainder for token in ("-h", "--help")):
        importer = Path(sys.executable).parent / "reccmp-ghidra-import"
        return subprocess.call([str(importer), "--help"], cwd=BUILD_DIR)

    _build_is_current()

    install_dir = _ghidra_config("ghidra_install_dir")
    project_dir = _ghidra_config("ghidra_project_dir")
    project = _ghidra_config("default_project")
    program = _ghidra_config("default_program")
    # reccmp expects the program path inside the Ghidra project with a leading slash.
    file_path = program if program.startswith("/") else f"/{program}"

    # A local Ghidra project can only be opened by one process at a time. Stop
    # the interactive `ghidra` CLI bridge if it is running so the headless
    # importer can acquire the project, then restart it afterwards.
    bridge_was_running = _bridge_running()
    if bridge_was_running:
        stopped = subprocess.run(["ghidra", "stop"], check=False)
        if stopped.returncode != 0:
            raise RuntimeError(
                "could not stop the Ghidra bridge before the headless import"
            )

    env = os.environ.copy()
    env["GHIDRA_INSTALL_DIR"] = install_dir
    # Resolve the importer from the same venv as this interpreter so PATH does
    # not matter (tools/decomp sources the venv, but be explicit and robust).
    importer = Path(sys.executable).parent / "reccmp-ghidra-import"
    command = [
        str(importer),
        "--target",
        "TOY2",
        "--local-project-name",
        project,
        "--local-project-dir",
        project_dir,
        "--file",
        file_path,
        *args.remainder,
    ]
    try:
        return subprocess.call(command, cwd=BUILD_DIR, env=env)
    finally:
        if bridge_was_running:
            subprocess.run(["ghidra", "start"], check=False)


# --------------------------------------------------------------------------- #
# check: functions_map.txt consistency validation
# --------------------------------------------------------------------------- #

def _parse_map(path: Path = MAP_PATH) -> list[tuple[int, str, str]]:
    """Return [(address_int, canonical_address, name)] in file order."""
    if not path.is_file():
        raise RuntimeError(f"functions map not found: {path}")
    entries: list[tuple[int, str, str]] = []
    for lineno, raw in enumerate(
        path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1
    ):
        line = raw.strip()
        if not line:
            continue
        match = _MAP_LINE_RE.match(line)
        if not match:
            raise RuntimeError(f"{path}:{lineno}: unparseable line: {raw!r}")
        value = int(match.group(1), 16)
        name = (match.group(2) or "").strip()
        entries.append((value, f"0x{value:08x}", name))
    return entries


def _code_section_ranges(path: Path = EXE_PATH) -> list[tuple[int, int]]:
    """Return [(start, end)] RVA ranges of executable sections from the PE."""
    data = path.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise RuntimeError(f"{path}: not a valid PE image")
    image_base = struct.unpack_from("<I", data, e_lfanew + 24 + 28)[0]
    num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    opt_header_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    table = e_lfanew + 24 + opt_header_size
    ranges: list[tuple[int, int]] = []
    for i in range(num_sections):
        offset = table + i * 40
        name = data[offset : offset + 8].rstrip(b"\0")
        if name != b".text":
            continue
        virtual_size = struct.unpack_from("<I", data, offset + 8)[0]
        virtual_address = struct.unpack_from("<I", data, offset + 12)[0]
        start = image_base + virtual_address
        ranges.append((start, start + virtual_size))
    if not ranges:
        raise RuntimeError(f"{path}: no .text section found")
    return ranges


def _check_structure(entries: list[tuple[int, str, str]]) -> list[str]:
    problems: list[str] = []
    try:
        ranges = _code_section_ranges()
    except (OSError, RuntimeError) as error:
        ranges = None
        print(f"warning: could not read code section: {error}", file=sys.stderr)

    seen: dict[int, str] = {}
    prev: Optional[int] = None
    for value, addr, name in entries:
        if not name:
            problems.append(f"  {addr}  has no name")
        if value in seen:
            problems.append(f"  {addr}  duplicate of {seen[value]}")
        else:
            seen[value] = addr
        if prev is not None and value < prev:
            problems.append(f"  {addr}  out of order (follows 0x{prev:08X})")
        prev = value
        if ranges is not None and not any(lo <= value < hi for lo, hi in ranges):
            problems.append(f"  {addr}  outside the .text section")
    return problems


def _read_snapshot_sizes(path: Path = FUNCTION_SIZES_PATH) -> dict[int, int]:
    """Return mapped retail body sizes from the saved Ghidra snapshot."""

    if not path.is_file():
        raise ValueError(
            f"function-size snapshot is missing: {path}. "
            "Run tools/decomp baseline with Ghidra available."
        )
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(
            f"function-size snapshot is invalid: {path}: {error}. "
            "Run tools/decomp baseline with Ghidra available."
        ) from error
    rows = payload.get("data", []) if isinstance(payload, dict) else payload
    if not isinstance(rows, list):
        raise ValueError(
            f"function-size snapshot must contain a JSON list: {path}. "
            "Run tools/decomp baseline with Ghidra available."
        )
    sizes: dict[int, int] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        try:
            address = int(str(row.get("address", "")), 16)
            size = int(row.get("original_size", row.get("size", 0)))
        except (TypeError, ValueError):
            continue
        if size > 1:
            sizes[address] = size
    if not sizes:
        raise ValueError(
            f"function-size snapshot has no usable address and size rows: {path}. "
            "Run tools/decomp baseline with Ghidra available."
        )
    return sizes


def _non_coverage_addresses(source_root: Path = SOURCE_ROOT) -> frozenset[int]:
    """Return addresses that cannot enter the coverage queue."""

    return frozenset(
        int(annotation.address, 16)
        for annotation in read_source_annotations(source_root)
        if annotation.kind in ("function", "library")
    )


def _check_coverage_map_gaps(
    entries: list[tuple[int, str, str]],
    snapshot_sizes: dict[int, int],
    non_coverage_addresses: frozenset[int],
    minimum_ratio: float = MINIMUM_COVERAGE_BODY_RATIO,
) -> list[str]:
    """Flag stale map gaps only for STUB or unstarted source-work targets."""

    problems: list[str] = []
    for entry, following in zip(entries, entries[1:]):
        address, canonical, name = entry
        if address in non_coverage_addresses or address not in snapshot_sizes:
            continue
        gap_size = following[0] - address
        body_size = snapshot_sizes[address]
        if gap_size <= 0 or body_size >= gap_size * minimum_ratio:
            continue
        ratio = body_size / gap_size
        problems.append(
            f"  {canonical}  {name}: retail body {body_size} bytes / "
            f"map gap {gap_size} bytes ({ratio:.1%})\n"
            "    Add the missing function start before source work."
        )
    return problems


def cmd_check(args: SimpleNamespace) -> int:
    entries = _parse_map()
    structure = _check_structure(entries)
    print(f"\n== structure (sorted / deduplicated / named / in .text): "
          f"{len(structure)} problem(s)")
    for line in structure:
        print(line)
    snapshot_problem = ""
    try:
        snapshot_sizes = _read_snapshot_sizes()
    except ValueError as error:
        snapshot_sizes = {}
        snapshot_problem = f"  {error}"
    coverage_gaps = _check_coverage_map_gaps(
        entries, snapshot_sizes, _non_coverage_addresses()
    )
    if snapshot_problem:
        coverage_gaps.insert(0, snapshot_problem)
    print(
        "\n== source-work map gaps (retail body / map gap >= 80%): "
        f"{len(coverage_gaps)} problem(s)"
    )
    for line in coverage_gaps:
        print(line)
    print("\n" + "=" * 60)
    problem_count = len(structure) + len(coverage_gaps)
    if problem_count:
        print(f"{problem_count} consistency problem(s) found.")
        if structure:
            print("Keep each named address in .text. Sort and deduplicate the map.")
        if coverage_gaps:
            print("Fix each source-work map gap before reconstruction.")
        return 1
    print("functions_map.txt is consistent.")
    return 0


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #

def main() -> int:
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        print("\nUsage: tools/ghidra_sync.py <command> [args]")
        print("  sync [reccmp-ghidra-import args]  import into Ghidra via reccmp")
        print("  check                            validate functions_map.txt consistency")
        return 0 if not argv else 0

    command, remainder = argv[0], argv[1:]

    if command == "sync":
        args = SimpleNamespace(remainder=remainder)
        try:
            return cmd_sync(args)
        except (RuntimeError, ValueError) as error:
            print(f"Error: {error}", file=sys.stderr)
            return 1

    if command == "check":
        try:
            return cmd_check(SimpleNamespace())
        except (RuntimeError, ValueError) as error:
            print(f"Error: {error}", file=sys.stderr)
            return 1

    print(f"Unknown command: {command}", file=sys.stderr)
    print("Commands: sync, check", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
