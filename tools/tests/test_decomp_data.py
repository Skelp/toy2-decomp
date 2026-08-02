import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "decomp_data.py"
SPEC = importlib.util.spec_from_file_location("decomp_data", SCRIPT)
DATA = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(DATA)


class DataEvidenceTests(unittest.TestCase):
    def test_parse_address_accepts_hexadecimal(self):
        self.assertEqual(DATA.parse_address("0x004DF040"), 0x004DF040)

    def test_mismatch_count_uses_scalar_results(self):
        variable = {
            "fields": [
                {"match": True},
                {"match": False},
                {"match": False},
            ]
        }
        self.assertEqual(DATA.mismatch_count(variable), 2)


if __name__ == "__main__":
    unittest.main()
