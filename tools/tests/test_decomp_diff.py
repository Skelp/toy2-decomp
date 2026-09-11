import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent


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
        lines = [f"\tline {number}\tvalue;" for number in range(1, 151)]
        (self.src / "Game" / "Target.cpp").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def view(self, text, previous, **options):
        return diff.attempt_view(text, previous, self.src, label="0x00401000", **options)[0]

    def test_changed_and_resolved_regions_with_source_lines(self):
        changed, resolved = diff.changed_regions(
            diff.regions(CURRENT.splitlines()), diff.regions(SAMPLE.splitlines())
        )
        self.assertEqual([region.address for region in changed], ["0x401007", "0x401200"])
        self.assertEqual(resolved, ["0x401100"])
        text = self.view(CURRENT, SAMPLE, seen=[(0x401000, 0x401003)])
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
        text = self.view(CURRENT, CURRENT.replace("50.00%", "49.00%"), seen=[(0x401000, 0x401300)])
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
            view.endswith("not shown: changed 4 (tools/decomp bc 0x00401000 --hunk N)\n"), view
        )
        small = self.view(CURRENT, SAMPLE, budget=400)
        self.assertLessEqual(len(small), 400)
        self.assertIn("not shown: changed", small)
        # With no previous diff the largest region that fits takes the place
        # of the one that does not.
        first = self.view(text, None)
        self.assertTrue(first.startswith("-- region 2:"), first)
        self.assertTrue(first.endswith("not shown: not seen 4 (tools/decomp bc 0x00401000 --hunk N)\n"))

    def test_a_hunk_that_fits_without_its_source_prints_alone(self):
        hunk = diff.region_text(CURRENT.splitlines(), diff.regions(CURRENT.splitlines()), 2)
        view, shown = diff.attempt_view(CURRENT, None, self.src, budget=len(hunk) + 10)
        self.assertEqual(view, hunk)
        self.assertEqual(shown, [])  # its source did not print, so it stays unseen

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


def diff_of(spans):
    """Return one hunk with a region of `size` removed lines at each (address, size).

    Each region refers to one line of Target.cpp, and four unchanged lines
    separate the regions, which is wider than REGION_GAP.
    """
    lines = ["@@ -0x401000,4096 +0x501000,4096 @@"]
    for address, size in spans:
        lines.append(f"0x{address:x} : push ebx \t(Target.cpp:{(address - 0x401000) // 0x20 + 8})")
        lines += [f"0x{address + 1 + index:x} : -mov eax, {index}" for index in range(size)]
        lines += [f"0x{address + 0x80 + index:x} : nop" for index in range(4)]
    return "\n".join(lines) + "\n"


def spans(*sizes):
    return [(0x401000 + 0x100 * index, size) for index, size in enumerate(sizes)]


class SeenRegionTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.src = self.root / "src"
        (self.src / "Game").mkdir(parents=True)
        lines = [f"\tline {number}\tvalue;" for number in range(1, 151)]
        (self.src / "Game" / "Target.cpp").write_text("\n".join(lines) + "\n", encoding="utf-8")
        diff.source_files.cache_clear()
        self.diff_path = self.root / "0x00401000.txt"
        self.seen_path = self.root / "0x00401000.seen"

    def main(self, text, *options):
        self.diff_path.write_text(text, encoding="utf-8")
        argv = ["decomp_diff.py", str(self.diff_path), "--source-root", str(self.src),
                "--address", "0x401000", "--seen", str(self.seen_path), *options]
        output = io.StringIO()
        with mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(output):
            self.assertEqual(diff.main(), 0)
        return output.getvalue()

    def seen_rows(self):
        return self.seen_path.read_text(encoding="utf-8").splitlines()

    def view(self, text, previous, seen, **options):
        return diff.attempt_view(text, previous, self.src, label="0x00401000", seen=seen, **options)

    def test_hunk_prints_each_region_with_its_source_lines(self):
        text = self.main(CURRENT, "--hunk", "1,2,9")
        source = f"--- source {self.src.as_posix()}/Game/Target.cpp:7-13 ---\n\tline 7\tvalue;\n"
        self.assertTrue(text.startswith("-- region 1: 0x401000 -1 +1 Target.cpp:10 --\n"), text)
        self.assertIn(source, text)
        self.assertIn("\tline 13\tvalue;\n-- region 2:", text)
        # Region 2 needs lines 9-16; lines 9-13 printed with region 1.
        self.assertIn("Target.cpp:14-16 ---\n\tline 14\tvalue;", text)
        self.assertEqual(text.count("\tline 9\tvalue;"), 1)
        self.assertTrue(text.endswith("no region 9; the index has 3 regions\n"), text)
        self.assertEqual(self.seen_rows(), ["0x401000 0x401003", "0x401007 0x401012"])

    def test_references_far_apart_print_as_separate_windows(self):
        wide = HUNK + "0x401000 : push ebx\n0x401001 : -mov eax, edi \t(Target.cpp:10)\n"
        wide += "0x401003 : -mov ecx, 1 \t(Target.cpp:120)\n0x401005 : ret\n"
        text = self.main(wide, "--hunk", "1")
        path = f"{self.src.as_posix()}/Game/Target.cpp"
        self.assertIn(f"--- source {path}:7-13 ---\n\tline 7\tvalue;", text)
        self.assertTrue(text.endswith(f"--- source {path}:117-123 ---\n" + "".join(
            f"\tline {number}\tvalue;\n" for number in range(117, 124))), text)
        self.assertNotIn("more lines", text)
        # Gaps of up to SOURCE_GAP lines stay in one window.
        region = diff.regions((HUNK + "0x401000 : -nop \t(Target.cpp:10)\n"
                               "         : +nop \t(Target.cpp:22)\n").splitlines())[0]
        self.assertIn(f"{path}:7-25 ---", diff.source_text(region, self.src, None))

    def test_hunk_caps_the_source_and_prints_the_busiest_window_first(self):
        wide = HUNK + "0x401000 : push ebx \t(Target.cpp:5)\n"
        wide += "".join(f"0x{0x401001 + n:x} : -mov eax, {n} \t(Target.cpp:{40 + 10 * n})\n"
                        for n in range(8))
        text = self.main(wide + "0x401010 : ret\n", "--hunk", "1")
        path = f"{self.src.as_posix()}/Game/Target.cpp"
        # The window with one reference (lines 2-8) gets no room; the one with
        # eight (lines 37-113) gets the 60 lines, and each cut part is named.
        self.assertIn(f"--- 7 more lines: sed -n 2,8p {path} ---\n--- source {path}:37-96 ---\n", text)
        self.assertTrue(text.endswith(f"\tline 96\tvalue;\n--- 17 more lines: sed -n 97,113p {path} ---\n"))
        self.assertEqual(text.count("\tvalue;"), diff.HUNK_SOURCE_LINES)

    def test_the_pack_starts_the_seen_set_and_the_view_and_hunk_add_to_it(self):
        first = diff_of(spans(6, 5, 4, 3, 2, 1))
        pack = self.main(first, "--pack-regions", "24000")
        self.assertIn("-- region 4:", pack)
        self.assertTrue(pack.endswith("not included (tools/decomp bc 0x00401000 --hunk N): regions 5 6 ---\n"))
        self.assertEqual(self.seen_rows(), [f"0x{0x401000 + 0x100 * n:x} 0x{0x401080 + 0x100 * n:x}" for n in range(4)])
        previous = self.root / "0x00401000.prev.txt"
        previous.write_text(first, encoding="utf-8")
        view = self.main(diff_of(spans(6, 5, 4, 3, 2, 2)), "--attempt-view", "--previous", str(previous))
        self.assertIn("changed since the last diff: 1 region(s)\n-- region 6:", view)
        self.assertIn("not seen yet:\n-- region 5:", view)
        self.assertEqual(len(self.seen_rows()), 6)
        self.main(diff_of(spans(6, 5, 4, 3, 2, 2)), "--hunk", "1")
        self.assertEqual(len(self.seen_rows()), 7)
        # A new batch starts with a new pack and a new seen set.
        self.main(first, "--pack-regions", "24000")
        self.assertEqual(len(self.seen_rows()), 4)

    def test_the_view_adds_the_largest_region_not_seen(self):
        text, previous = diff_of(spans(6, 5, 4, 3, 2, 2)), diff_of(spans(6, 5, 4, 3, 2, 1))
        seen = [(0x401000, 0x401080), (0x401100, 0x401180)]
        view, shown = self.view(text, previous, seen)
        self.assertEqual([region.number for region in shown], [6, 3])
        self.assertIn("-- region 6:", view)
        self.assertIn("Target.cpp:48 --", view)
        self.assertIn("not seen yet:\n-- region 3:", view)
        for number in (1, 2, 4, 5):
            self.assertNotIn(f"-- region {number}:", view)
        self.assertNotIn("not shown", view)
        # A changed region prints although it was seen.
        _, shown = self.view(text, previous, seen + [(0x401500, 0x401580)])
        self.assertEqual([region.number for region in shown], [6, 3])
        # With every region seen, only the changed one prints.
        _, shown = self.view(text, previous, [(0x401000, 0x402000)])
        self.assertEqual([region.number for region in shown], [6])

    def test_changed_regions_past_the_two_slots_are_named_not_printed(self):
        text, previous = diff_of(spans(2, 2, 2, 9)), diff_of(spans(1, 1, 1, 9))
        view, shown = self.view(text, previous, [])
        self.assertEqual([region.number for region in shown], [1, 2, 4])
        self.assertIn("not seen yet:\n-- region 4:", view)
        self.assertTrue(view.endswith("not shown: changed 3 (tools/decomp bc 0x00401000 --hunk N)\n"), view)
        # The extra slot is for a region not seen; a third changed one stays named.
        view, shown = self.view(text, previous, [(0x401000, 0x402000)])
        self.assertEqual([region.number for region in shown], [1, 2])
        self.assertNotIn("not seen yet", view)
        self.assertTrue(view.endswith("not shown: changed 3 (tools/decomp bc 0x00401000 --hunk N)\n"), view)

    def test_room_for_the_unseen_region_comes_before_seen_source(self):
        text, previous = diff_of(spans(40, 30, 20)), diff_of(spans(39, 29, 20))
        found = diff.regions(text.splitlines())
        lines = text.splitlines()
        hunks = [diff.region_text(lines, found, number) for number in (1, 2, 3)]
        sources = [diff.source_text(region, self.src, 0x401000) for region in found]
        header = "changed since the last diff: 2 region(s)\n"
        budget = len(header) + sum(map(len, hunks)) + len("not seen yet:\n") + len(sources[2]) + 10
        # The writer saw regions 1 and 2: their source gives way to region 3.
        seen = [(0x401000, 0x401080), (0x401100, 0x401180)]
        view, shown = self.view(text, previous, seen, budget=budget)
        self.assertIn("not seen yet:\n" + hunks[2] + sources[2], view)
        self.assertNotIn(sources[0], view)
        self.assertEqual([region.number for region in shown], [3])
        self.assertNotIn("not shown", view)
        # The second changed region must leave room for region 3, so it is named.
        budget = len(header) + len(hunks[0]) + len(hunks[1]) + len(hunks[2]) // 2
        view, shown = self.view(text, previous, seen, budget=budget)
        self.assertEqual([region.number for region in shown], [1, 3])
        self.assertIn("not seen yet:\n" + hunks[2], view)
        self.assertTrue(view.endswith("not shown: changed 2 (tools/decomp bc 0x00401000 --hunk N)\n"), view)
        self.assertLessEqual(len(view), budget)

    def test_seen_regions_match_by_retail_range_when_numbers_change(self):
        before = diff_of([(0x401100, 5), (0x401200, 3)])
        after = diff_of([(0x401000, 4), (0x401100, 5), (0x401200, 3)])
        old = diff.regions(before.splitlines())[0]
        seen = [(old.low, old.high)]
        new = diff.regions(after.splitlines())
        self.assertEqual(old.number, 1)
        self.assertEqual([diff.is_seen(region, seen) for region in new], [False, True, False])
        view, shown = self.view(after, before, seen)
        self.assertEqual([region.number for region in shown], [1, 3])
        self.assertNotIn("-- region 2:", view)

    def test_an_unseen_region_that_does_not_fit_is_named_and_the_next_one_prints(self):
        text, previous = diff_of(spans(200, 3, 2)), diff_of(spans(200, 3, 1))
        view, shown = self.view(text, previous, [], budget=1200)
        self.assertLessEqual(len(view), 1200)
        self.assertEqual([region.number for region in shown], [3, 2])
        self.assertTrue(view.endswith("not shown: not seen 1 (tools/decomp bc 0x00401000 --hunk N)\n"), view)
        for budget in (300, 500, 800):
            view, _ = self.view(text, previous, [], budget=budget)
            self.assertLessEqual(len(view), budget)

    def test_the_wrapper_passes_the_seen_set_to_the_pack_hunk_and_view(self):
        script = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        self.assertIn('--pack-regions', script)
        self.assertEqual(script.count('--source-root src --seen "build/decomp-diffs/'), 3)
        # A new campaign rewrites the baseline, so an older seen set is dropped.
        self.assertIn('.seen -ot build/decomp-baseline-report.json ]]', script)


if __name__ == "__main__":
    unittest.main()


class TargetSourceTests(unittest.TestCase):
    def test_the_target_source_runs_from_its_annotation_to_the_next_one(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "src"
            (root / "Toy2").mkdir(parents=True)
            (root / "Toy2" / "A.cpp").write_text(
                "int x;\n// FUNCTION: TOY2 0x00401000\nvoid A()\n{\n\tx = 1;\n}\n\n"
                "// FUNCTION: TOY2 0x00401100\nvoid B() {}\n", encoding="utf-8")
            text = diff.target_source(root, 0x00401000)
            self.assertTrue(text.startswith("--- target source "), text)
            self.assertIn(":2-6 (current tree) ---\n", text)
            self.assertIn("\tx = 1;", text)
            self.assertNotIn("void B()", text)
            self.assertEqual(diff.target_source(root, 0x00401000, limit=10), "")
            self.assertEqual(diff.target_source(root, 0x00409999), "")
