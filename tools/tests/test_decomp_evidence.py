from __future__ import annotations

import io
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

from tools import decomp_evidence
from tools.decomp_evidence import (
    UnmappedStartEvidence,
    resolve_padding_alignment,
    resolve_unmapped_start,
)


class UnmappedStartTests(unittest.TestCase):
    def test_accepts_an_exact_ghidra_start(self):
        evidence = resolve_unmapped_start(
            0x401000,
            (64, "FUN_00401000"),
            {0x401000: frozenset({0x500000})},
        )
        self.assertEqual(
            evidence,
            UnmappedStartEvidence(64, "FUN_00401000", (0x500000,)),
        )

    def test_accepts_an_annotated_global_relocation_target(self):
        evidence = resolve_unmapped_start(
            0x401000,
            None,
            {0x401000: frozenset({0x500008, 0x500004})},
        )
        self.assertEqual(
            evidence,
            UnmappedStartEvidence(None, "", (0x500004, 0x500008)),
        )

    def test_rejects_an_address_without_start_evidence(self):
        self.assertIsNone(resolve_unmapped_start(0x401000, None, {}))

    def test_resolves_nop_padding_to_the_next_mapped_start(self):
        self.assertEqual(
            resolve_padding_alignment(
                0x401008,
                [0x401000, 0x401010, 0x402000],
                lambda _address, size: b"\x90" * size,
            ),
            0x401010,
        )

    def test_rejects_nonpadding_or_distant_mapped_starts(self):
        self.assertIsNone(
            resolve_padding_alignment(
                0x401008,
                [0x401000, 0x401010],
                lambda _address, size: b"\x90" * (size - 1) + b"\x00",
            )
        )
        self.assertIsNone(
            resolve_padding_alignment(
                0x401000,
                [0x401000, 0x401020],
                lambda _address, size: b"\x90" * size,
            )
        )

    def test_cli_accepts_a_relocation_only_start(self):
        output = io.StringIO()
        with (
            patch.object(
                decomp_evidence.sys,
                "argv",
                ["decomp_evidence.py", "0x00402000", "--unmapped"],
            ),
            patch.object(
                decomp_evidence,
                "parse_map",
                return_value=[(0x401000, "N::A"), (0x403000, "N::D")],
            ),
            patch.object(
                decomp_evidence,
                "annotation_index",
                return_value=({}, {0x500000: ("Renderer.cpp", 10)}),
            ),
            patch.object(
                decomp_evidence,
                "global_symbol_names",
                return_value={0x500000: "g_dispatch"},
            ),
            patch.object(decomp_evidence, "read_match_percentages", return_value={}),
            patch.object(decomp_evidence, "read_original_sizes", return_value={}),
            patch.object(decomp_evidence, "ghidra_function_at", return_value=None),
            patch.object(
                decomp_evidence,
                "scan_annotated_relocation_targets",
                return_value={0x402000: frozenset({0x500004})},
            ),
            patch.object(decomp_evidence, "run_ghidra", return_value=None),
            patch.object(
                decomp_evidence.decomp_binary, "available", return_value=False
            ),
            redirect_stdout(output),
        ):
            result = decomp_evidence.main()

        self.assertEqual(result, 0)
        self.assertIn("annotated-global relocation target", output.getvalue())
        self.assertIn("g_dispatch (Renderer.cpp:10)", output.getvalue())


if __name__ == "__main__":
    unittest.main()
