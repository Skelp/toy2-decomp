import importlib.util
import unittest
from pathlib import Path
from types import SimpleNamespace

from reccmp.compare.db import EntityDb
from reccmp.compare.variables import (
    BssState,
    ComparedOffset,
    DataBlock,
    VariableComparator,
)
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


class PointerLiteralComparisonTests(unittest.TestCase):
    def make_comparator(self, original_relocations=(), recompiled_relocations=()):
        return VariableComparator(
            EntityDb(),
            SimpleNamespace(),
            RelocationImage(original_relocations),
            RelocationImage(recompiled_relocations),
        )

    def compare(
        self,
        comparator,
        original,
        recompiled,
        semantic_match=False,
    ):
        variable = SimpleNamespace(orig_addr=0x100, recomp_addr=0x200)
        scalar = SimpleNamespace(offset=4, size=4, is_pointer=True)
        compared = ComparedOffset(
            offset=4,
            name="[1]",
            match=semantic_match,
            values=("Original semantic pointer", "Recompiled semantic pointer"),
        )
        return REPORT.pointer_literal_comparison(
            comparator,
            variable,
            DataBlock(0x100, b"\0" * 4 + original, BssState.NO),
            DataBlock(0x200, b"\0" * 4 + recompiled, BssState.NO),
            scalar,
            compared,
        )

    def test_identical_non_relocated_sentinel_matches(self):
        result = self.compare(
            self.make_comparator(),
            b"\xff\xff\xff\xff",
            b"\xff\xff\xff\xff",
        )

        self.assertTrue(result.match)
        self.assertEqual(result.values, ("Pointer literal 0xffffffff",) * 2)

    def test_different_non_relocated_literals_do_not_match(self):
        result = self.compare(
            self.make_comparator(),
            b"\x11\x11\x11\x11",
            b"\x22\x22\x22\x22",
            semantic_match=True,
        )

        self.assertFalse(result.match)
        self.assertEqual(result.values[0], "Pointer literal 0x11111111")
        self.assertEqual(result.values[1], "Pointer literal 0x22222222")

    def test_relocated_semantic_pointer_match_is_preserved(self):
        result = self.compare(
            self.make_comparator(
                original_relocations=(0x104,), recompiled_relocations=(0x204,)
            ),
            b"\x10\0\0\0",
            b"\x20\0\0\0",
            semantic_match=True,
        )

        self.assertTrue(result.match)
        self.assertEqual(result.values[0], "Original semantic pointer")
        self.assertEqual(result.values[1], "Recompiled semantic pointer")

    def test_relocated_target_mismatch_rejects_raw_fallback(self):
        result = self.compare(
            self.make_comparator(original_relocations=(0x104,)),
            b"\xff\xff\xff\xff",
            b"\xff\xff\xff\xff",
        )

        self.assertFalse(result.match)
        self.assertEqual(result.values[0], "Original semantic pointer")
        self.assertEqual(result.values[1], "Recompiled semantic pointer")


if __name__ == "__main__":
    unittest.main()
