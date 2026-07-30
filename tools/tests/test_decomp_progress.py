import sys
import types
import unittest

sys.modules.setdefault(
    "colorama",
    types.SimpleNamespace(
        Fore=types.SimpleNamespace(RED=""),
        Style=types.SimpleNamespace(RESET_ALL=""),
        init=lambda **_: None,
    ),
)
sys.modules.setdefault("build", types.SimpleNamespace(track_process=lambda *_: None))

import decomp_utils
from tools.decomp_status import MatchStatus


class ProgressBreakdownTests(unittest.TestCase):
    def test_separates_verified_and_provisional_score_bands(self):
        source_functions = {
            "00401000": {"verification": "matched"},
            "00402000": {"verification": "effective"},
            "00403000": {"verification": "tool"},
            "00404000": {"verification": "provisional"},
            "00405000": {"verification": "provisional"},
            "00406000": {"verification": "provisional"},
            "00407000": {"verification": "provisional"},
        }
        statuses = {
            0x404000: MatchStatus(0.8),
            0x405000: MatchStatus(0.6),
            0x406000: MatchStatus(0.4),
        }

        counts = decomp_utils.progress_breakdown(
            source_functions, set(source_functions), statuses
        )

        self.assertEqual(counts["matched"], 1)
        self.assertEqual(counts["effective"], 1)
        self.assertEqual(counts["tool"], 1)
        self.assertEqual(counts["provisional_75_plus"], 1)
        self.assertEqual(counts["provisional_50_to_75"], 1)
        self.assertEqual(counts["provisional_below_50"], 1)
        self.assertEqual(counts["provisional_unscored"], 1)


if __name__ == "__main__":
    unittest.main()
