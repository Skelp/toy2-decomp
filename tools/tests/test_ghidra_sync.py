from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from ghidra_sync.manifest import (
    FunctionRecord,
    _instruction_pairs,
    _source_ranges,
    canonical_address,
)
from ghidra_sync.headless_apply import _partition_owned_maps


class _Lines:
    def __init__(self, values):
        self.values = values

    def find_line_of_recomp_address(self, address):
        return self.values.get(address)


class ManifestTests(unittest.TestCase):
    def test_canonical_address(self):
        self.assertEqual(canonical_address("0x4a1bb0"), "0x004a1bb0")
        self.assertEqual(canonical_address(0x401000), "0x00401000")

    def test_instruction_pairs_only_uses_equal_blocks(self):
        raw = SimpleNamespace(
            codes=[("equal", 0, 2, 0, 2), ("replace", 2, 3, 2, 3)],
            orig_inst=[("0x401000", "a"), ("0x401002", "b"), ("0x401004", "c")],
            recomp_inst=[("0x501000", "a"), ("0x501002", "b"), ("0x501004", "x")],
        )
        diff = SimpleNamespace(result=SimpleNamespace(diff=raw))
        self.assertEqual(
            _instruction_pairs(diff), [(0x401000, 0x501000), (0x401002, 0x501002)]
        )

    def test_source_ranges_propagate_and_coalesce_pdb_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src" / "Example.cpp"
            source.parent.mkdir()
            source.write_text("one\ntwo\n", encoding="utf-8")
            raw = SimpleNamespace(
                codes=[("equal", 0, 3, 0, 3)],
                orig_inst=[
                    ("0x401000", "a"),
                    ("0x401002", "b"),
                    ("0x401004", "c"),
                ],
                recomp_inst=[
                    ("0x501000", "a"),
                    ("0x501002", "b"),
                    ("0x501004", "c"),
                ],
            )
            diff = SimpleNamespace(result=SimpleNamespace(diff=raw))
            compare = SimpleNamespace(
                _lines_db=_Lines(
                    {
                        0x501000: (source, 10),
                        0x501004: (source, 11),
                    }
                )
            )
            ranges = _source_ranges(compare, diff, 0x401006, root)
            self.assertEqual(
                [(item.address, item.length, item.line) for item in ranges],
                [("0x00401000", 4, 10), ("0x00401004", 2, 11)],
            )
            self.assertEqual(ranges[0].path, "/toy2-decomp/src/Example.cpp")
            self.assertEqual(
                ranges[0].sha256, hashlib.sha256(source.read_bytes()).hexdigest()
            )


class CliStatusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location(
            "toy2_ghidra_sync_cli", TOOLS / "ghidra_sync.py"
        )
        assert spec is not None and spec.loader is not None
        cls.cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.cli)

    def test_non_exact_record_is_always_skipped(self):
        record = FunctionRecord(
            address="0x00401000",
            recomp_address="0x00501000",
            name="Example",
            source="Example.cpp",
            annotation_line=1,
            size=1,
            matching=0.95,
            exact=False,
            effective=True,
            stub=False,
            signature_available=True,
            reason="effective match is not exact",
        )
        status, _ = self.cli._record_status(record, {})
        self.assertEqual(status, "SKIP")

    def test_targeted_source_map_ownership_preserves_other_functions(self):
        owned = [
            {"function": "0x00401000", "address": "0x00401000"},
            {"function": "0x00402000", "address": "0x00402000"},
        ]
        removed, retained = _partition_owned_maps(owned, {"0x00401000"})
        self.assertEqual(removed, owned[:1])
        self.assertEqual(retained, owned[1:])


if __name__ == "__main__":
    unittest.main()
