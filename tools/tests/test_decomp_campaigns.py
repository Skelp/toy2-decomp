from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "toy2_decomp_campaigns", TOOLS / "decomp_campaigns.py"
)
assert spec is not None and spec.loader is not None
campaigns = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = campaigns
spec.loader.exec_module(campaigns)


class CampaignTests(unittest.TestCase):
    def test_read_records_and_address_stats_split_bundle_yield(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaigns.jsonl"
            records = [
                {
                    "mode": "refinement",
                    "result": "source",
                    "addresses": ["0x00401000", "0x00402000"],
                    "minutes": 10,
                    "effective_bytes": 120,
                    "initialized_bytes": 0,
                },
                {
                    "mode": "coverage",
                    "result": "no-source",
                    "addresses": ["0x00401000"],
                    "minutes": 8,
                    "effective_bytes": 0,
                    "initialized_bytes": 0,
                },
            ]
            path.write_text(
                "".join(json.dumps(item) + "\n" for item in records),
                encoding="utf-8",
            )
            stats = campaigns.address_stats(campaigns.read_records(path))
            self.assertEqual(stats[0x00401000].attempts, 2)
            self.assertEqual(stats[0x00401000].zero_yield_attempts, 1)
            self.assertEqual(stats[0x00401000].effective_bytes, 60)
            self.assertEqual(stats[0x00401000].minutes, 13)
            self.assertEqual(stats[0x00402000].attempts, 1)
            self.assertEqual(stats[0x00402000].effective_bytes, 60)

    def test_invalid_json_reports_the_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaigns.jsonl"
            path.write_text("{}\nnot-json\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "line 2"):
                campaigns.read_records(path)


if __name__ == "__main__":
    unittest.main()
