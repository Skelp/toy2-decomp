"""Build the source-of-truth manifest used by the Ghidra sync commands."""

from __future__ import annotations

import hashlib
import json
import subprocess
from dataclasses import asdict, dataclass, field
from pathlib import Path, PurePath
from typing import Any, Iterable

from decomp_annotations import read_source_annotations


ROOT = Path(__file__).resolve().parents[2]
MANIFEST_VERSION = 1


def canonical_address(value: str | int) -> str:
    """Return an address in the form used by Ghidra and sync JSON."""
    number = int(value, 0) if isinstance(value, str) else value
    return f"0x{number:08x}"


@dataclass(frozen=True)
class SourceRange:
    path: str
    sha256: str
    line: int
    address: str
    length: int


@dataclass
class FunctionRecord:
    address: str
    recomp_address: str
    name: str
    source: str
    annotation_line: int
    size: int
    matching: float
    exact: bool
    effective: bool
    stub: bool
    signature_available: bool
    source_ranges: list[SourceRange] = field(default_factory=list)
    reason: str | None = None


@dataclass
class SyncManifest:
    version: int
    target: str
    original_sha256: str
    recompiled_sha256: str
    functions: list[FunctionRecord]
    type_scope: str = "exact"

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def write(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), indent=2) + "\n", encoding="utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_build_is_current(root: Path = ROOT) -> None:
    """Fail closed if Ninja would rebuild the comparison executable."""
    required = (
        root / "build" / "toy2.exe",
        root / "build" / "toy2.pdb",
        root / "build" / "reccmp-build.yml",
    )
    missing = [str(path.relative_to(root)) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError(f"missing build artifacts: {', '.join(missing)}; run tools/decomp build")

    result = subprocess.run(
        ["ninja", "-C", "build", "-n", "toy2decomp"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    output = (result.stdout + result.stderr).strip()
    if result.returncode != 0:
        raise RuntimeError(f"could not validate build freshness: {output}")
    if "no work to do" not in output.lower():
        raise RuntimeError("comparison artifacts are stale; run tools/decomp build")


def _annotations(root: Path) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for annotation in read_source_annotations(root / "src"):
        result[canonical_address(annotation.address)] = annotation
    return result


def load_target(root: Path = ROOT) -> Any:
    """Load TOY2 while honoring this repository's build/ config location."""
    from reccmp.project.detect import RecCmpProject

    # Starting the search in build/ lets reccmp 0.1.6 combine the generated
    # build file with the project/user files in the repository root.
    project = RecCmpProject.from_directory(root / "build")
    return project.get("TOY2")


def _relative_source_path(path: PurePath, root: Path) -> Path | None:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        resolved = candidate.resolve()
        resolved.relative_to((root / "src").resolve())
        return resolved
    except (OSError, ValueError):
        return None


def _instruction_pairs(diff: Any) -> list[tuple[int, int]]:
    """Extract paired instruction addresses from a raw 100% reccmp diff."""
    pairs: list[tuple[int, int]] = []
    raw = diff.result.diff
    for tag, i1, i2, j1, j2 in raw.codes:
        if tag != "equal":
            continue
        for (orig_addr, _), (recomp_addr, _) in zip(
            raw.orig_inst[i1:i2], raw.recomp_inst[j1:j2]
        ):
            if orig_addr and recomp_addr:
                pairs.append((int(orig_addr, 16), int(recomp_addr, 16)))
    return pairs


def _source_ranges(compare: Any, diff: Any, function_end: int, root: Path) -> list[SourceRange]:
    pairs = _instruction_pairs(diff)
    if not pairs:
        return []

    line_state: tuple[Path, int] | None = None
    mapped: list[tuple[int, int, Path, int]] = []
    for index, (orig_addr, recomp_addr) in enumerate(pairs):
        found = compare._lines_db.find_line_of_recomp_address(recomp_addr)  # pinned reccmp adapter
        if found is not None:
            source_path = _relative_source_path(found[0], root)
            if source_path is not None:
                line_state = (source_path, found[1])
        if line_state is None:
            continue
        next_addr = pairs[index + 1][0] if index + 1 < len(pairs) else function_end
        if next_addr > orig_addr:
            mapped.append((orig_addr, next_addr, line_state[0], line_state[1]))

    coalesced: list[tuple[int, int, Path, int]] = []
    for start, end, source_path, line in mapped:
        if coalesced:
            old_start, old_end, old_path, old_line = coalesced[-1]
            if old_end == start and old_path == source_path and old_line == line:
                coalesced[-1] = (old_start, end, old_path, old_line)
                continue
        coalesced.append((start, end, source_path, line))

    hashes: dict[Path, str] = {}
    result: list[SourceRange] = []
    for start, end, source_path, line in coalesced:
        hashes.setdefault(source_path, _sha256(source_path))
        logical = "/toy2-decomp/" + source_path.relative_to(root).as_posix()
        result.append(
            SourceRange(
                path=logical,
                sha256=hashes[source_path],
                line=line,
                address=canonical_address(start),
                length=end - start,
            )
        )
    return result


def build_manifest(
    targets: Iterable[str] | None = None,
    *,
    type_scope: str = "exact",
    root: Path = ROOT,
) -> SyncManifest:
    """Run reccmp once and describe every requested function conservatively."""
    check_build_is_current(root)

    from reccmp.ghidra.importer.pdb_extraction import PdbFunctionExtractor
    from reccmp.compare.core import Compare
    from reccmp.types import ImageId

    target = load_target(root)
    compare = Compare.from_target(target)
    annotations = _annotations(root)
    requested = {canonical_address(value) for value in targets} if targets else None
    pdb_functions = {
        canonical_address(item.match_info.orig_addr): item
        for item in PdbFunctionExtractor(compare).get_function_list()
    }

    records: list[FunctionRecord] = []
    seen: set[str] = set()
    for match in compare.get_functions():
        address = canonical_address(match.orig_addr)
        if requested is not None and address not in requested:
            continue
        seen.add(address)
        annotation = annotations.get(address)
        diff = compare.compare_address(match.orig_addr)
        exact = bool(diff is not None and diff.ratio == 1.0 and not diff.is_effective_match)
        effective = bool(diff is not None and diff.is_effective_match)
        stub = bool(match.get("stub", False))
        reason = None
        if annotation is None or annotation.kind != "function":
            reason = "missing FUNCTION annotation"
        elif stub:
            reason = "source is marked STUB"
        elif not exact:
            reason = "effective match is not exact" if effective else "machine code is not an exact match"
        elif address not in pdb_functions:
            reason = "function is absent from the recompiled PDB"

        size = match.size(ImageId.ORIG) or match.size(ImageId.RECOMP) or 0
        ranges: list[SourceRange] = []
        if reason is None and diff is not None and size > 0:
            ranges = _source_ranges(compare, diff, match.orig_addr + size, root)

        source = annotation.source if annotation is not None else ""
        records.append(
            FunctionRecord(
                address=address,
                recomp_address=canonical_address(match.recomp_addr),
                name=match.best_name() or match.name or address,
                source=source,
                annotation_line=annotation.line if annotation is not None else 0,
                size=size,
                matching=diff.ratio if diff is not None else 0.0,
                exact=exact,
                effective=effective,
                stub=stub,
                signature_available=bool(
                    address in pdb_functions and pdb_functions[address].signature is not None
                ),
                source_ranges=ranges,
                reason=reason,
            )
        )

    if requested is not None:
        for address in sorted(requested - seen):
            annotation = annotations.get(address)
            records.append(
                FunctionRecord(
                    address=address,
                    recomp_address="0x00000000",
                    name=address,
                    source=annotation.source if annotation is not None else "",
                    annotation_line=annotation.line if annotation is not None else 0,
                    size=0,
                    matching=0.0,
                    exact=False,
                    effective=False,
                    stub=bool(annotation is not None and annotation.kind == "stub"),
                    signature_available=False,
                    reason="address was not paired by reccmp",
                )
            )

    records.sort(key=lambda item: int(item.address, 16))
    return SyncManifest(
        version=MANIFEST_VERSION,
        target="TOY2",
        original_sha256=_sha256(target.original_path),
        recompiled_sha256=_sha256(target.recompiled_path),
        functions=records,
        type_scope=type_scope,
    )


def load_manifest(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("version") != MANIFEST_VERSION:
        raise RuntimeError(f"unsupported sync manifest version: {data.get('version')}")
    return data
