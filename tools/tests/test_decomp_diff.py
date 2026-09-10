import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


diff = load("decomp_diff")

# Two hunks. The first holds two changes separated by three unchanged lines,
# which is wider than REGION_GAP, so they are separate regions.
SAMPLE = """\
Target is only 40.00% similar to the original, diff above

---
+++
@@ -0x401000,20 +0x501000,20 @@
0x401000 : push ebx
0x401001 : -mov eax, edi
         : +mov eax, ebx \t(Target.cpp:10)
0x401003 : test eax, eax
0x401005 : je 0x10
0x401007 : mov ecx, 5 \t(Target.cpp:12)
0x40100c : -sub cx, dx
         : +sub cx, ax \t(Target.cpp:13)
0x40100f : ret
@@ -0x401100,8 +0x501100,8 @@
0x401100 : push ebp
0x401101 : -push edi
         : +push esi \t(Target.cpp:30)
0x401102 : ret
"""


class DiffRegionTests(unittest.TestCase):
    def setUp(self):
        self.lines = SAMPLE.splitlines()
        self.found = diff.regions(self.lines)

    def test_regions_split_on_gaps_and_hunk_headers(self):
        self.assertEqual([r.number for r in self.found], [1, 2, 3])
        first, second, third = self.found
        self.assertEqual((first.minus, first.plus), (1, 1))
        self.assertEqual(first.address, "0x401000")
        self.assertEqual(first.source_span(), "Target.cpp:10")
        self.assertEqual(second.address, "0x401007")
        self.assertEqual(second.source_span(), "Target.cpp:12-13")
        self.assertEqual((third.minus, third.plus), (1, 1))
        self.assertEqual(third.source_span(), "Target.cpp:30")

    def test_adjacent_changes_merge_into_one_region(self):
        lines = [
            "0x401000 : push ebx",
            "0x401001 : -mov eax, edi",
            "         : +mov eax, ebx",
            "0x401003 : test eax, eax",
            "0x401005 : -je 0x10",
            "         : +je 0x12",
        ]
        found = diff.regions(lines)
        self.assertEqual(len(found), 1)
        self.assertEqual((found[0].minus, found[0].plus), (2, 2))

    def test_region_index_and_summary_are_short(self):
        index = diff.region_index(self.found)
        self.assertEqual(len(index), 3)
        self.assertIn("Target.cpp:12-13", index[1])
        summary = diff.summarize(SAMPLE, Path("diff.txt"), compact_path=Path("c.txt"))
        lines = summary.splitlines()
        self.assertTrue(lines[0].startswith("Target is only 40.00%"))
        self.assertIn("Regions: 3; changed lines: 6 of", summary)
        self.assertIn("compact: c.txt", summary)
        self.assertLessEqual(len(lines), 7)

    def test_compact_keeps_changed_lines_and_anchors_only(self):
        text = diff.compact(self.lines, self.found)
        self.assertIn("@@ -0x401000,20", text)
        self.assertIn("-- region 1:", text)
        self.assertIn("+mov eax, ebx", text)
        self.assertIn("0x401007 : mov ecx, 5", text)  # anchor before region 2
        self.assertNotIn("0x401005 : je 0x10", text)  # gap line, not adjacent
        self.assertLess(len(text.splitlines()), len(self.lines))

    def test_region_text_gives_context_and_reports_unknown_numbers(self):
        text = diff.region_text(self.lines, self.found, 3, context=1)
        self.assertTrue(text.startswith("-- region 3: 0x401100 -1 +1 Target.cpp:30 --"))
        self.assertIn("0x401100 : push ebp", text)
        self.assertEqual(
            diff.region_text(self.lines, self.found, 9), "no region 9; the index has 3 regions\n"
        )

    def test_write_compact_and_hunks_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "diff.txt"
            source.write_text(SAMPLE, encoding="utf-8")
            compact_path = root / "diff.compact.txt"
            argv = ["decomp_diff.py", str(source), "--write-compact", str(compact_path), "--hunks"]
            with mock.patch.object(sys, "argv", argv), mock.patch("sys.stdout"):
                self.assertEqual(diff.main(), 0)
            self.assertTrue(compact_path.exists())
            self.assertIn("-- region 3:", compact_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
