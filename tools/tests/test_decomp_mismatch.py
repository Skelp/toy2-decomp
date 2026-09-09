from __future__ import annotations

import unittest

from tools import decomp_mismatch as mismatch


def report_diff(
    original: list[str], recompiled: list[str]
) -> list[list[object]]:
    return [
        [
            "@@ -0x401000,1 +0x501000,1 @@",
            [
                {
                    "orig": [
                        [f"0x{0x401000 + index:08x}", instruction]
                        for index, instruction in enumerate(original)
                    ],
                    "recomp": [
                        [f"0x{0x501000 + index:08x}", instruction]
                        for index, instruction in enumerate(recompiled)
                    ],
                }
            ],
        ]
    ]


def routes(result: dict[str, object]) -> list[str]:
    return [signal["route"] for signal in result["signals"]]


class MismatchTaxonomyTests(unittest.TestCase):
    def test_call_signal(self):
        result = mismatch.classify_report_diff(
            report_diff(["call Original"], ["call Recompiled"])
        )
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(result["primary_route"], "call")
        self.assertEqual(routes(result), ["call"])

    def test_branch_and_loop_signals(self):
        result = mismatch.classify_report_diff(
            report_diff(["jne 0x10", "loop 0x20"], ["je 0x12", "loopne 0x24"])
        )
        self.assertEqual(result["primary_route"], "control-flow")
        self.assertEqual(routes(result), ["control-flow"])
        self.assertEqual(result["signals"][0]["changed_rows"], 2)

    def test_stack_signal(self):
        result = mismatch.classify_report_diff(
            report_diff(["mov eax, dword ptr [esp + 4]"], ["mov ecx, dword ptr [esp + 4]"])
        )
        self.assertEqual(result["primary_route"], "stack")
        self.assertEqual(routes(result), ["stack"])

    def test_global_and_field_signals(self):
        result = mismatch.classify_report_diff(
            report_diff(
                ["mov dword ptr [Global::g_state+4 (OFFSET)], eax"],
                ["mov dword ptr [ecx + 4], eax"],
            )
        )
        self.assertEqual(result["primary_route"], "memory")
        self.assertEqual(routes(result), ["memory", "side-effect"])

    def test_x87_signal(self):
        result = mismatch.classify_report_diff(
            report_diff(["fadd st(0), st(1)"], ["fsub st(0), st(1)"])
        )
        self.assertEqual(result["primary_route"], "floating-point")
        self.assertEqual(routes(result), ["floating-point"])

    def test_integer_immediate_signal(self):
        result = mismatch.classify_report_diff(
            report_diff(["mov eax, 1"], ["mov eax, 2"])
        )
        self.assertEqual(result["primary_route"], "integer")
        self.assertEqual(routes(result), ["integer"])

    def test_register_only_signal_is_codegen(self):
        result = mismatch.classify_report_diff(
            report_diff(["mov eax, ecx"], ["mov edx, ecx"])
        )
        self.assertEqual(result["primary_route"], "codegen")
        self.assertEqual(routes(result), ["codegen"])

    def test_ambiguous_signals_use_stable_route_order(self):
        result = mismatch.classify_report_diff(
            report_diff(["cmp dword ptr [eax], 1"], ["cmp dword ptr [eax], 2"])
        )
        self.assertEqual(result["primary_route"], "control-flow")
        self.assertEqual(routes(result), ["control-flow", "integer", "memory"])

    def test_evidence_is_bounded(self):
        result = mismatch.classify_report_diff(
            report_diff(
                [f"call Original{index}" for index in range(12)],
                [f"call Recompiled{index}" for index in range(12)],
            )
        )
        signal = result["signals"][0]
        self.assertEqual(signal["changed_rows"], 12)
        self.assertEqual(len(signal["evidence"]), mismatch.DEFAULT_EVIDENCE_LIMIT)
        self.assertTrue(signal["evidence_truncated"])

    def test_insert_and_delete_rows_keep_exact_locators(self):
        diff = [
            [
                "@@ -0x401000,2 +0x501000,2 @@",
                [
                    {"orig": [["0x401000", "nop"]], "recomp": []},
                    {"orig": [], "recomp": [["0x501000", "int 3"]]},
                ],
            ]
        ]
        result = mismatch.classify_report_diff(diff)
        self.assertEqual(
            result["row_changes"], {"replace": 0, "insert": 1, "delete": 1}
        )
        evidence = result["signals"][0]["evidence"]
        self.assertEqual(evidence[0]["change"], "delete")
        self.assertEqual(evidence[0]["original"]["locator"], "/diff/0/1/0/orig/0")
        self.assertEqual(evidence[1]["change"], "insert")
        self.assertEqual(evidence[1]["recompiled"]["locator"], "/diff/0/1/1/recomp/0")

    def test_text_diff_keeps_insert_and_delete_lines(self):
        result = mismatch.classify_text_diff(
            [
                "@@ -0x401000,1 +0x501000,1 @@",
                "0x401000 : -call Original",
                "         : +call Recompiled",
            ]
        )
        self.assertEqual(
            result["row_changes"], {"replace": 0, "insert": 1, "delete": 1}
        )
        evidence = result["signals"][0]["evidence"]
        self.assertEqual(evidence[0]["original"]["locator"], "/lines/2")
        self.assertEqual(evidence[1]["recompiled"]["locator"], "/lines/3")

    def test_legacy_candidate_labels_are_unchanged(self):
        lines = [
            "0x401000 : call eax ",
            "0x401002 : mov eax, dword ptr [value] ",
            "0x401004 : mov eax, esp ",
            "0x401006 : fld st(0) ",
        ]
        self.assertEqual(
            mismatch.legacy_candidate_classifications(lines),
            ("control-flow", "data-access", "stack-frame", "floating-point"),
        )
        self.assertEqual(
            mismatch.legacy_candidate_classifications(["-nop", "+int 3"]),
            ("instruction",),
        )

    def test_abi_precedes_an_immediate_signal(self):
        result = mismatch.classify_report_diff(
            report_diff(["ret 4"], ["ret 8"])
        )
        self.assertEqual(result["primary_route"], "abi")
        self.assertEqual(routes(result), ["abi"])


if __name__ == "__main__":
    unittest.main()
