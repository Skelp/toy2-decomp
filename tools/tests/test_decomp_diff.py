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
        self.assertIn("(regions: `bc ADDRESS --hunk N`)", summary)
        self.assertNotIn("read the compact file", summary)
        self.assertIn("compact: c.txt", summary)
        self.assertLessEqual(len(lines), 7)

    def test_region_index_is_never_truncated(self):
        lines = [f"0x{0x401000 + number:x} : push ebx" for number in range(600)]
        for number in range(0, 600, 4):  # every fourth line changes: 150 regions
            lines[number] = f"0x{0x401000 + number:x} : -mov eax, edi"
        found = diff.regions(lines)
        self.assertEqual(len(found), 150)
        index = diff.region_index(found)
        self.assertEqual(len(index), 150)
        self.assertTrue(index[-1].startswith("150  0x"), index[-1])
        self.assertFalse(any("more regions" in row for row in index))
        summary = diff.summarize("\n".join(lines), Path("diff.txt"))
        self.assertEqual(len(summary.splitlines()), 150 + 3)

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


# SAMPLE after one attempt: the region at 0x401007 grew, the one at 0x401100 is
# gone and a new one appeared at 0x401200. The region at 0x401000 is the same.
CURRENT = """\
Target is only 50.00% similar to the original, diff above

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
0x40100f : -add cx, dx
         : +sub cx, ax \t(Target.cpp:13)
         : +add cx, ax
0x401012 : ret
@@ -0x401200,8 +0x501200,8 @@
0x401200 : push ebp
0x401201 : -push edi
         : +push esi \t(Target.cpp:40)
0x401202 : ret
"""


HUNK = "@@ -0x401000,20 +0x501000,20 @@\n"


class AttemptViewTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.src = Path(directory.name) / "src"
        (self.src / "Game").mkdir(parents=True)
        lines = [f"\tline {number}\tvalue;" for number in range(1, 51)]
        (self.src / "Game" / "Target.cpp").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def view(self, text, previous, **options):
        return diff.attempt_view(text, previous, self.src, label="0x00401000", **options)

    def test_changed_and_resolved_regions_with_source_lines(self):
        changed, resolved = diff.changed_regions(
            diff.regions(CURRENT.splitlines()), diff.regions(SAMPLE.splitlines())
        )
        self.assertEqual([region.address for region in changed], ["0x401007", "0x401200"])
        self.assertEqual(resolved, ["0x401100"])
        text = self.view(CURRENT, SAMPLE)
        self.assertTrue(
            text.startswith("changed since the last diff: 2 region(s); resolved: 0x401100\n"), text
        )
        # Largest changed region first. The unchanged region is not printed
        # again, because the pack or an earlier attempt already showed it.
        self.assertLess(text.index("-- region 2:"), text.index("-- region 3:"))
        self.assertNotIn("-- region 1:", text)
        source = f"--- source {self.src.as_posix()}/Game/Target.cpp:9-16 ---\n"
        self.assertIn(source + "\tline 9\tvalue;\n", text)
        self.assertIn("\tline 16\tvalue;\n", text)
        self.assertIn(f"Target.cpp:37-43 ---\n\tline 37\tvalue;", text)
        self.assertIn("\tline 16\tvalue;\n-- region 3:", text)
        self.assertNotIn("not shown", text)

    def test_unchanged_regions_print_only_the_count(self):
        text = self.view(CURRENT, CURRENT.replace("50.00%", "49.00%"))
        self.assertEqual(text, "changed since the last diff: 0 region(s)\n")

    def test_a_region_that_shrinks_or_merges_is_not_resolved(self):
        before = HUNK + "0x401000 : push ebx\n0x401001 : -mov eax, edi\n         : +mov eax, ebx\n"
        before += "0x401003 : -test eax, eax\n         : +test ebx, ebx\n0x401005 : ret\n"
        after = before.replace("0x401001 : -mov eax, edi\n         : +mov eax, ebx", "0x401001 : mov eax, edi")
        self.assertTrue(self.view(after, before).startswith("changed since the last diff: 1 region(s)\n"))
        # Two regions three unchanged lines apart become one region.
        apart = HUNK + "0x401000 : push ebx\n0x401001 : -mov eax, edi\n         : +mov eax, ebx\n"
        apart += "0x401003 : nop\n0x401004 : nop\n0x401005 : nop\n0x401006 : -pop ebx\n"
        apart += "         : +pop esi\n0x401007 : ret\n"
        merged = apart.replace("0x401004 : nop", "0x401004 : -nop\n         : +int3")
        self.assertEqual(len(diff.regions(apart.splitlines())), 2)
        self.assertEqual(len(diff.regions(merged.splitlines())), 1)
        self.assertTrue(self.view(merged, apart).startswith("changed since the last diff: 1 region(s)\n"))

    def test_budget_skips_a_region_and_names_it(self):
        big = [f"0x{0x401300 + number:x} : -nop" for number in range(600)]
        text = CURRENT + "@@ -0x401300,600 +0x501300,600 @@\n" + "\n".join(big) + "\n"
        view = self.view(text, SAMPLE)
        self.assertLessEqual(len(view), diff.VIEW_BUDGET)
        self.assertIn("-- region 2:", view)
        self.assertNotIn("-- region 4:", view)
        self.assertTrue(
            view.endswith("not shown: regions 4 (tools/decomp bc 0x00401000 --hunk N)\n"), view
        )
        small = self.view(CURRENT, SAMPLE, budget=400)
        self.assertLessEqual(len(small), 400)
        self.assertIn("not shown: regions", small)
        # With no previous diff the largest region that fits takes the place
        # of the one that does not.
        first = self.view(text, None)
        self.assertTrue(first.startswith("-- region 2:"), first)
        self.assertTrue(first.endswith("not shown: regions 4 (tools/decomp bc 0x00401000 --hunk N)\n"))

    def test_a_hunk_that_fits_without_its_source_prints_alone(self):
        hunk = diff.region_text(CURRENT.splitlines(), diff.regions(CURRENT.splitlines()), 2)
        self.assertEqual(self.view(CURRENT, None, budget=len(hunk) + 10), hunk)

    def test_a_region_without_a_reference_takes_the_nearest_in_its_hunk(self):
        text = HUNK + "0x401000 : push ebx \t(Target.cpp:20)\n0x401001 : mov eax, edi\n"
        text += "0x401003 : test eax, eax\n0x401005 : -je 0x10\n         : +jne 0x10\n0x401007 : ret\n"
        text += HUNK + "0x401100 : -push edi\n         : +push esi\n0x401101 : nop\n0x401102 : nop\n"
        text += "0x401103 : mov eax, 1 \t(Target.cpp:30)\n"
        first, second = diff.regions(text.splitlines())
        self.assertEqual((first.source_span(), first.nearby), ("-", {"Target.cpp": [20]}))
        self.assertEqual(second.nearby, {"Target.cpp": [30]})  # the later one, at a hunk top
        self.assertIn("Target.cpp:17-23 ---\n\tline 17\tvalue;", self.view(text, None))

    def test_identical_diff_prints_nothing(self):
        self.assertEqual(self.view(CURRENT, CURRENT), "")

    def test_no_previous_diff_prints_the_largest_region_only(self):
        text = self.view(CURRENT, None)
        self.assertTrue(text.startswith("-- region 2: 0x401007 -2 +2 Target.cpp:12-13 --"), text)
        self.assertNotIn("changed since", text)
        self.assertNotIn("-- region 1:", text)
        self.assertNotIn("-- region 3:", text)

    def test_shared_file_names_resolve_by_the_target_annotation(self):
        (self.src / "Other").mkdir()
        (self.src / "Other" / "Target.cpp").write_text("// FUNCTION: TOY2 0x00401000\n", encoding="utf-8")
        diff.source_files.cache_clear()
        self.assertIsNone(diff.resolve_source(self.src, "Target.cpp", None))
        self.assertEqual(
            diff.resolve_source(self.src, "Target.cpp", 0x401000), self.src / "Other" / "Target.cpp"
        )
        self.assertIsNone(diff.resolve_source(self.src, "Missing.cpp", 0x401000))

    def test_cli_reads_the_previous_diff_and_names_the_saved_diff(self):
        current = self.src.parent / "0x0041C640.txt"
        current.write_text(CURRENT, encoding="utf-8")
        missing = self.src.parent / "0x0041C640.prev.txt"
        argv = ["decomp_diff.py", str(current), "--attempt-view", "--previous", str(missing),
                "--source-root", str(self.src)]
        with mock.patch.object(sys, "argv", argv), mock.patch("sys.stdout") as stdout:
            self.assertEqual(diff.main(), 0)
        printed = "".join(call.args[0] for call in stdout.write.call_args_list)
        self.assertTrue(printed.startswith("-- region 2:"), printed)


if __name__ == "__main__":
    unittest.main()
