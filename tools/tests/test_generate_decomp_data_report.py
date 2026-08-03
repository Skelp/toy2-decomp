import importlib.util
import unittest
from pathlib import Path
from types import SimpleNamespace

from reccmp.compare.db import EntityDb
from reccmp.compare.variables import BssState, DataBlock, VariableComparator
from reccmp.types import ImageId


SCRIPT = Path(__file__).parents[1] / "generate-decomp-data-report.py"
SPEC = importlib.util.spec_from_file_location("generate_decomp_data_report", SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(REPORT)


class RelocationImage:
    def __init__(self, relocations=()):
        self.relocations = set(relocations)


class UnionStorageComparisonTests(unittest.TestCase):
    def make_comparator(self, original_relocations=(), recompiled_relocations=()):
        database = EntityDb()
        comparator = VariableComparator(
            database,
            SimpleNamespace(),
            RelocationImage(original_relocations),
            RelocationImage(recompiled_relocations),
        )
        return database, comparator

    def compare(self, comparator, original, recompiled):
        variable = SimpleNamespace(orig_addr=0, recomp_addr=0)
        storage = REPORT.UnionStorage(0, 4, "value (union)")
        return REPORT.union_storage_comparison(
            comparator,
            variable,
            DataBlock(0, original, BssState.NO),
            DataBlock(0, recompiled, BssState.NO),
            storage,
        )

    def test_integer_member_uses_identical_raw_storage(self):
        _, comparator = self.make_comparator()

        result = self.compare(
            comparator,
            (150).to_bytes(4, "little"),
            (150).to_bytes(4, "little"),
        )

        self.assertTrue(result.match)
        self.assertEqual(result.name, "value (union)")
        self.assertIn("Raw union bytes 96000000", result.values[0])

    def test_pointer_member_uses_target_match(self):
        database, comparator = self.make_comparator(original_relocations=(0,))
        with database.batch() as batch:
            batch.set(ImageId.RECOMP, 0x20, name="target")
            batch.match(0x10, 0x20)

        result = self.compare(comparator, b"\x10\0\0\0", b"\x20\0\0\0")

        self.assertTrue(result.match)
        self.assertIn("Pointer to target", result.values[0])
        self.assertIn("relocations +0x0", result.values[0])

    def test_pointer_member_rejects_a_target_mismatch(self):
        _, comparator = self.make_comparator(original_relocations=(0,))

        result = self.compare(comparator, b"\x10\0\0\0", b"\x20\0\0\0")

        self.assertFalse(result.match)

    def test_ambiguous_storage_rejects_different_raw_bytes(self):
        _, comparator = self.make_comparator()

        result = self.compare(comparator, b"\x10\0\0\0", b"\x20\0\0\0")

        self.assertFalse(result.match)
        self.assertNotIn("relocations", result.values[0])


if __name__ == "__main__":
    unittest.main()
