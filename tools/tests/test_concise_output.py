import importlib.util
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

    def test_notes_default_cap_and_full(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "notes.md"
            path.write_text("\n".join("needle" for _ in range(20)))
            with unittest.mock.patch.object(notes, "SOURCES", {"names": (path,)}):
                self.assertEqual(len(notes.search("needle", "names", 8)), 8)
                self.assertEqual(len(notes.search("needle", "names", 0)), 20)


if __name__ == "__main__":
    unittest.main()
