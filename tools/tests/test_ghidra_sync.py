from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

spec = importlib.util.spec_from_file_location(
    "toy2_ghidra_sync_cli", TOOLS / "ghidra_sync.py"
)
assert spec is not None and spec.loader is not None
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class ParseMapTests(unittest.TestCase):
    def test_parse_map_handles_hex_and_decimal_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.txt"
            path.write_text(
                "0x00401000 First\n"
                "00402000 Second\n"
                "  0x403000  Third  \n"
                "\n"
                "0x00404000\n",
                encoding="utf-8",
            )
            entries = cli._parse_map(path)
        self.assertEqual(
            [(addr, name) for _value, addr, name in entries],
            [
                ("0x00401000", "First"),
                ("0x00402000", "Second"),
                ("0x00403000", "Third"),
                ("0x00404000", ""),
            ],
        )

    def test_parse_map_rejects_unparseable_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.txt"
            path.write_text("not-an-address\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                cli._parse_map(path)


class StructureTests(unittest.TestCase):
    def setUp(self):
        # Isolate structure checks from the host's retail executable.
        self._real_ranges = cli._code_section_ranges
        cli._code_section_ranges = lambda: [(0x00401000, 0x00500000)]

    def tearDown(self):
        cli._code_section_ranges = self._real_ranges

    def _entries(self, *pairs):
        return [(value, f"0x{value:08x}", name) for value, name in pairs]

    def test_clean_map_has_no_problems(self):
        self.assertEqual(
            cli._check_structure(self._entries((0x00401000, "a"), (0x00402000, "b"))),
            [],
        )

    def test_unsorted_entries_are_flagged(self):
        problems = cli._check_structure(
            self._entries((0x00402000, "b"), (0x00401000, "a"))
        )
        self.assertTrue(any("out of order" in p for p in problems))

    def test_duplicate_addresses_are_flagged(self):
        problems = cli._check_structure(
            self._entries((0x00401000, "a"), (0x00401000, "b"))
        )
        self.assertTrue(any("duplicate" in p for p in problems))

    def test_missing_name_is_flagged(self):
        problems = cli._check_structure(self._entries((0x00401000, "")))
        self.assertTrue(any("has no name" in p for p in problems))

    def test_address_outside_text_is_flagged(self):
        problems = cli._check_structure(self._entries((0x00600000, "far")))
        self.assertTrue(any("outside the .text section" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
