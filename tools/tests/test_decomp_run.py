"""Wrapper-level checks of `tools/decomp handoff` and `tools/decomp campaigns run`.

Each test runs a copy of the wrapper in a temporary root with an empty
environment script, so no toolchain, build or model is needed.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WrapperTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / "tools").mkdir()
        shutil.copy(ROOT / "tools" / "decomp", self.root / "tools" / "decomp")
        (self.root / "tools" / "linux-decomp-env.sh").write_text("", encoding="utf-8")

    def run_tool(self, *args: str, stdin: str = "") -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(self.root / "tools" / "decomp"), *args],
            cwd=self.root, input=stdin, capture_output=True, text=True, check=False,
        )

    def test_handoff_writes_stdin_under_the_canonical_address(self):
        result = self.run_tool("handoff", "0x1234", stdin="a\nb\n")
        self.assertEqual((result.returncode, result.stdout), (0, "handoff: written (2 lines)\n"))
        path = self.root / "build" / "decomp-cache" / "handoff" / "0x00001234.md"
        self.assertEqual(path.read_text(encoding="utf-8"), "a\nb\n")
        self.assertEqual(self.run_tool("handoff", "0x1234").returncode, 2)
        self.assertEqual(path.read_text(encoding="utf-8"), "a\nb\n")
        self.assertEqual(self.run_tool("handoff", "nope", stdin="x\n").returncode, 2)

    def test_run_prints_its_usage_and_rejects_bad_options(self):
        result = self.run_tool("campaigns", "run", "--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("campaigns run [--count N]", result.stdout)
        for args in (("--count", "0"), ("--mode", "data"), ("--address", "x"), ("--bogus", "1"),
                     ("--budget", "1"), ("--budget", "x")):
            with self.subTest(args=args):
                self.assertEqual(self.run_tool("campaigns", "run", *args).returncode, 2)

    def test_run_refuses_to_start_on_a_dirty_source_tree(self):
        (self.root / "src").mkdir()
        (self.root / "src" / "a.cpp").write_text("int a;\n", encoding="utf-8")
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t"]
        for command in (["init", "-q"], ["add", "src"], ["commit", "-q", "-m", "base"]):
            subprocess.run(git + command, cwd=self.root, check=True)
        (self.root / "src" / "a.cpp").write_text("int b;\n", encoding="utf-8")
        result = self.run_tool("campaigns", "run", "--writer", "true")
        self.assertEqual(result.returncode, 1)
        self.assertIn("uncommitted changes", result.stderr)
        self.assertIn("run: 0 campaigns", result.stdout)


if __name__ == "__main__":
    unittest.main()
