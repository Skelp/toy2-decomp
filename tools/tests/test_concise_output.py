import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


evidence = load("decomp_evidence")
diff = load("decomp_diff")
notes = load("decomp_notes")


class ConciseOutputTests(unittest.TestCase):
    def test_decompilation_head_tail_and_full(self):
        body = "\n".join(f"line {number}" for number in range(200))
        bounded = evidence.decomp_lines(body, False, None)
        self.assertEqual(len(bounded), 121)
        self.assertIn("80 lines omitted", bounded[80])
        self.assertEqual(evidence.decomp_lines(body, True, None), body.splitlines())
        self.assertEqual(evidence.decomp_lines(body, False, (10, 12)), ["line 9", "line 10", "line 11"])

    def test_diff_caps_windows_and_full_file_is_lossless(self):
        lines = [f"context {number}" for number in range(300)]
        for number in (20, 120, 220, 260): lines[number] = "! mismatch"
        text = "Similarity: 50%\n" + "\n".join(lines) + "\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diff.txt"
            path.write_text(text)
            summary = diff.summarize(text, path)
            self.assertLessEqual(len(summary.splitlines()), 160)
            self.assertEqual(summary.count("--- mismatch"), 3)
            self.assertEqual(path.read_text(), text)

    def test_diff_shows_score_ceiling_and_ceiling_relative_score(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            function_map.write_text(
                "0x00401000 Target\n0x00402000 Next\n", encoding="utf-8"
            )
            function_sizes.write_text(
                json.dumps([{"address": "00401000", "size": 2048}]),
                encoding="utf-8",
            )
            ceiling = diff.read_score_ceiling(
                0x00401000, function_map, function_sizes
            )
            self.assertEqual(ceiling, 0.5)
            context = diff.score_context(
                "Target is only 20.00% similar to the original.", ceiling
            )
            self.assertEqual(
                context,
                "Score ceiling: 50.00%. Ceiling-relative score: 40.00%.",
            )

    def test_diff_skips_bad_coverage_ceilings_for_function_and_library(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "src"
            source_root.mkdir()
            (source_root / "Target.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
                "// LIBRARY: TOY2 0x00402000\n",
                encoding="utf-8",
            )
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            function_map.write_text(
                "0x00401000 Target\n0x00402000 Library\n0x00403000 Next\n",
                encoding="utf-8",
            )
            function_sizes.write_text(
                json.dumps(
                    [
                        {"address": "00401000", "size": 35},
                        {"address": "00402000", "size": 35},
                    ]
                ),
                encoding="utf-8",
            )
            context = diff.comparison_score_context(
                "Target is only 93.94% similar to the original.",
                0x00401000,
                function_map,
                function_sizes,
                source_root,
            )
            self.assertEqual(
                context,
                "Score ceiling: not applicable. Ceiling-relative score: not applicable.",
            )
            library_context = diff.comparison_score_context(
                "Library is only 93.94% similar to the original.",
                0x00402000,
                function_map,
                function_sizes,
                source_root,
            )
            self.assertEqual(library_context, context)

    def test_score_ceiling_cannot_exceed_one(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            function_map.write_text(
                "0x00401000 Target\n0x00402000 Next\n", encoding="utf-8"
            )
            function_sizes.write_text(
                json.dumps([{"address": "00401000", "size": 8192}]),
                encoding="utf-8",
            )
            self.assertEqual(
                diff.read_score_ceiling(0x00401000, function_map, function_sizes),
                1.0,
            )

    def test_notes_default_cap_and_full(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "notes.md"
            path.write_text("\n".join("needle" for _ in range(20)))
            with unittest.mock.patch.object(notes, "SOURCES", {"names": (path,)}):
                self.assertEqual(len(notes.search("needle", "names", 8)), 8)
                self.assertEqual(len(notes.search("needle", "names", 0)), 20)


if __name__ == "__main__":
    unittest.main()
