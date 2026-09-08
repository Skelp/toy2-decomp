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
from reccmp.types import EntityType, ImageId


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

    def test_pointer_member_uses_identical_string_targets(self):
        database, comparator = self.make_comparator(
            original_relocations=(0,), recompiled_relocations=(0,)
        )
        with database.batch() as batch:
            batch.set(
                ImageId.ORIG, 0x10, type=EntityType.STRING, name='"x"', size=2
            )
            batch.set(
                ImageId.RECOMP, 0x20, type=EntityType.STRING, name='"x"', size=2
            )

        result = self.compare(comparator, b"\x10\0\0\0", b"\x20\0\0\0")

        self.assertTrue(result.match)

    def test_pointer_member_rejects_one_sided_string_relocation(self):
        database, comparator = self.make_comparator(original_relocations=(0,))
        with database.batch() as batch:
            batch.set(
                ImageId.ORIG, 0x10, type=EntityType.STRING, name='"x"', size=2
            )
            batch.set(
                ImageId.RECOMP, 0x20, type=EntityType.STRING, name='"x"', size=2
            )

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


class RelocatedStringPointerComparisonTests(unittest.TestCase):
    def make_comparator(
        self, database, original_relocated=True, recompiled_relocated=True
    ):
        original_relocations = (0x104,) if original_relocated else ()
        recompiled_relocations = (0x204,) if recompiled_relocated else ()
        return VariableComparator(
            database,
            SimpleNamespace(),
            RelocationImage(original_relocations),
            RelocationImage(recompiled_relocations),
        )

    def compare(self, comparator, original_pointer, recompiled_pointer):
        variable = SimpleNamespace(orig_addr=0x100, recomp_addr=0x200)
        scalar = SimpleNamespace(offset=4, size=4, is_pointer=True)
        compared = ComparedOffset(
            offset=4,
            name="[1]",
            match=False,
            values=("Original semantic pointer", "Recompiled semantic pointer"),
        )
        return REPORT.pointer_literal_comparison(
            comparator,
            variable,
            DataBlock(
                0x100,
                b"\0" * 4 + original_pointer.to_bytes(4, "little"),
                BssState.NO,
            ),
            DataBlock(
                0x200,
                b"\0" * 4 + recompiled_pointer.to_bytes(4, "little"),
                BssState.NO,
            ),
            scalar,
            compared,
        )

    def add_entity(self, database, image, address, entity_type, name, size=2):
        with database.batch() as batch:
            batch.set(image, address, type=entity_type, name=name, size=size)

    def test_duplicate_narrow_string_uses_exact_pointer_targets(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x10, EntityType.STRING, '"("')
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"("')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"("')
        with database.batch() as batch:
            batch.match(0x10, 0x30)

        result = self.compare(self.make_comparator(database), 0x20, 0x30)

        self.assertTrue(result.match)

    def test_wide_and_narrow_strings_do_not_match_across_widths(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.WIDECHAR, '"x"')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"x"')

        result = self.compare(self.make_comparator(database), 0x20, 0x30)

        self.assertFalse(result.match)

    def test_narrow_targets_match_when_a_wide_string_coexists(self):
        database = EntityDb()
        self.add_entity(
            database, ImageId.ORIG, 0x10, EntityType.WIDECHAR, 'L"x"', 4
        )
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"x"')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"x"')

        result = self.compare(self.make_comparator(database), 0x20, 0x30)

        self.assertTrue(result.match)

    def test_different_narrow_strings_do_not_match(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"x"')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"y"')

        result = self.compare(self.make_comparator(database), 0x20, 0x30)

        self.assertFalse(result.match)

    def test_offsets_into_strings_do_not_match(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"xy"', 3)
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"xy"', 3)

        result = self.compare(self.make_comparator(database), 0x21, 0x31)

        self.assertFalse(result.match)

    def test_non_string_targets_do_not_match_by_name(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.DATA, "target", 4)
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.DATA, "target", 4)

        result = self.compare(self.make_comparator(database), 0x20, 0x30)

        self.assertFalse(result.match)

    def test_unrelocated_string_pointers_do_not_use_semantic_fallback(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"x"')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"x"')

        result = self.compare(
            self.make_comparator(
                database, original_relocated=False, recompiled_relocated=False
            ),
            0x20,
            0x30,
        )

        self.assertFalse(result.match)

    def test_one_sided_relocation_does_not_use_semantic_fallback(self):
        database = EntityDb()
        self.add_entity(database, ImageId.ORIG, 0x20, EntityType.STRING, '"x"')
        self.add_entity(database, ImageId.RECOMP, 0x30, EntityType.STRING, '"x"')

        result = self.compare(
            self.make_comparator(database, recompiled_relocated=False), 0x20, 0x30
        )

        self.assertFalse(result.match)

    def test_duplicate_result_is_independent_of_insertion_order(self):
        results = []
        for original_addresses in ((0x10, 0x20), (0x20, 0x10)):
            database = EntityDb()
            for address in original_addresses:
                self.add_entity(
                    database, ImageId.ORIG, address, EntityType.STRING, '"x"'
                )
            self.add_entity(
                database, ImageId.RECOMP, 0x30, EntityType.STRING, '"x"'
            )
            with database.batch() as batch:
                batch.match(0x10, 0x30)
            results.append(
                self.compare(self.make_comparator(database), 0x20, 0x30).match
            )

        self.assertEqual(results, [True, True])

if __name__ == "__main__":
    unittest.main()
