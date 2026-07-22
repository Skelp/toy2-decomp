import tempfile
import unittest
from pathlib import Path

from tools.decomp_annotations import active_source_lines, read_source_annotations


class ActiveSourceTests(unittest.TestCase):
    def test_ignores_if_zero_and_keeps_else(self):
        source = """// FUNCTION: TOY2 0x401000
#if 0
// FUNCTION: TOY2 0x401010
#if 1
// STUB: TOY2 0x401020
#endif
#else
// FUNCTION: TOY2 0x401030
#endif
"""
        active = "\n".join(line for _, line in active_source_lines(source))
        self.assertIn("0x401000", active)
        self.assertNotIn("0x401010", active)
        self.assertNotIn("0x401020", active)
        self.assertIn("0x401030", active)

    def test_reads_locations_and_kinds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sample.cpp").write_text(
                "// GLOBAL: TOY2 0x500000\n// STUB: TOY2 0x401000\n",
                encoding="utf-8",
            )
            annotations = read_source_annotations(root)
        self.assertEqual(
            [(item.kind, item.address, item.source, item.line) for item in annotations],
            [
                ("global", "0x500000", "sample.cpp", 1),
                ("stub", "0x401000", "sample.cpp", 2),
            ],
        )


if __name__ == "__main__":
    unittest.main()
