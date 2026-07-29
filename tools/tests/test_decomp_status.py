import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1]))
from decomp_status import (
    MatchStatus,
    is_symbol_only_diff,
    read_match_statuses,
    verification_status,
)


class DecompStatusTests(unittest.TestCase):
    def test_reads_effective_without_calling_it_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(
                '{"data":[{"address":"0x401000","matching":0.92,"effective":true}]}',
                encoding="utf-8",
            )
            status = read_match_statuses(path)[0x401000]
            self.assertFalse(status.exact)
            self.assertTrue(status.effective)

    def test_tool_artifact_accepts_symbol_only_rows(self):
        status = MatchStatus(
            matching=0.9,
            effective=False,
            diff=[
                [
                    0,
                    [
                        {
                            "orig": [[0, "mov eax, (OFFSET) g_name"]],
                            "recomp": [[0, "mov eax, (DATA) DAT_1234"]],
                        }
                    ],
                ]
            ],
        )
        self.assertTrue(is_symbol_only_diff(status))
        self.assertEqual(verification_status(status, tool_artifact=True), "tool")

    def test_tool_artifact_rejects_register_or_instruction_changes(self):
        status = MatchStatus(
            matching=0.9,
            effective=False,
            diff=[
                [
                    0,
                    [
                        {
                            "orig": [[0, "mov eax, ebx"]],
                            "recomp": [[0, "mov ecx, ebx"]],
                        }
                    ],
                ]
            ],
        )
        self.assertFalse(is_symbol_only_diff(status))

    def test_tool_artifact_rejects_a_register_change_beside_symbols(self):
        status = MatchStatus(
            matching=0.9,
            diff=[
                [
                    0,
                    [
                        {
                            "orig": [[0, "mov eax, g_first (OFFSET)"]],
                            "recomp": [[0, "mov ecx, g_second (OFFSET)"]],
                        }
                    ],
                ]
            ],
        )
        self.assertFalse(is_symbol_only_diff(status))

    def test_tool_artifact_rejects_frame_and_scheduling_differences(self):
        for original, recompiled in (
            ("mov ebp, g_first (OFFSET)", "mov esp, g_second (OFFSET)"),
            ("push g_first (OFFSET)", "pop g_second (OFFSET)"),
            ("jne g_first (OFFSET)", "je g_second (OFFSET)"),
        ):
            with self.subTest(original=original, recompiled=recompiled):
                status = MatchStatus(
                    matching=0.9,
                    diff=[[0, [{"orig": [[0, original]], "recomp": [[0, recompiled]]}]]],
                )
                self.assertFalse(is_symbol_only_diff(status))

    def test_tool_artifact_accepts_an_immediate_rendered_as_a_symbol(self):
        status = MatchStatus(
            matching=0.9,
            diff=[
                [
                    0,
                    [
                        {
                            "orig": [[0, "cmp eax, 0x52ad85"]],
                            "recomp": [[0, "cmp eax, g_table+45 (OFFSET)"]],
                        }
                    ],
                ]
            ],
        )
        self.assertTrue(is_symbol_only_diff(status))

    def test_source_debt_makes_an_effective_match_provisional(self):
        status = MatchStatus(matching=0.92, effective=True)
        self.assertEqual(verification_status(status, source_clean=False), "provisional")


if __name__ == "__main__":
    unittest.main()
