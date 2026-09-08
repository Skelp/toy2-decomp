from __future__ import annotations

import unittest
from unittest.mock import patch

from tools import decomp_discover
from tools.decomp_discover import (
    GhidraFunction,
    TransferEvidence,
    add_relocation_target_starts,
    discover,
    import_thunk_addresses,
    parse_ghidra_functions,
    scan_annotated_relocation_targets,
    scan_data_references,
    scan_transfers,
)


class ParseTests(unittest.TestCase):
    def test_parses_and_sorts_valid_ghidra_rows(self):
        rows = parse_ghidra_functions(
            [
                {"address": "00402000", "size": 8, "name": "second"},
                {"address": "0x00401000", "size": "4", "name": "first"},
                {"address": "not-an-address", "size": 4},
                {"address": "00403000", "size": 0},
            ]
        )
        self.assertEqual(
            rows,
            [
                GhidraFunction(0x401000, 4, "first"),
                GhidraFunction(0x402000, 8, "second"),
            ],
        )


class TransferTests(unittest.TestCase):
    class Operand:
        def __init__(self, operand_type, immediate=0):
            self.type = operand_type
            self.imm = immediate

    class Instruction:
        def __init__(self, mnemonic, operand_type, target):
            self.mnemonic = mnemonic
            self.operands = [TransferTests.Operand(operand_type, target)]

    class Disassembler:
        def disasm(self, _code, address):
            if address != 0x401000:
                return []
            return [
                TransferTests.Instruction("call", 1, 0x402000),
                TransferTests.Instruction("jmp", 1, 0x403000),
                TransferTests.Instruction("jmp", 1, 0x401008),
                TransferTests.Instruction("call", 2, 0),
            ]

    def test_keeps_calls_and_cross_function_jumps_only(self):
        functions = [
            GhidraFunction(0x401000, 0x10, "source"),
            GhidraFunction(0x402000, 4, "call_target"),
            GhidraFunction(0x403000, 4, "jump_target"),
        ]
        with (
            patch.object(decomp_discover.decomp_binary, "available", return_value=True),
            patch.object(decomp_discover.decomp_binary, "read_bytes", return_value=b"x"),
            patch.object(
                decomp_discover, "_capstone", return_value=(self.Disassembler(), 1)
            ),
        ):
            evidence = scan_transfers(functions)
        self.assertEqual(evidence[0x402000].call_callers, frozenset({0x401000}))
        self.assertEqual(evidence[0x403000].jump_callers, frozenset({0x401000}))
        self.assertNotIn(0x401008, evidence)

    def test_identifies_only_indirect_import_jumps(self):
        functions = [
            GhidraFunction(0x401000, 6, "import"),
            GhidraFunction(0x402000, 6, "small_project_function"),
            GhidraFunction(0x403000, 8, "other"),
        ]

        def read_bytes(address, _size):
            return {
                0x401000: b"\xFF\x25\x00\x20\x50\x00",
                0x402000: b"\xB8\x01\x00\x00\x00\xC3",
                0x403000: b"\xFF\x25\x00\x20\x50\x00\x90\x90",
            }[address]

        with patch.object(
            decomp_discover.decomp_binary, "read_bytes", side_effect=read_bytes
        ):
            self.assertEqual(import_thunk_addresses(functions), frozenset({0x401000}))

    def test_finds_aligned_function_pointers_in_data(self):
        functions = [GhidraFunction(0x401000, 8, "callback")]
        section = decomp_discover.decomp_binary.Section(
            ".data", 0x500000, 8, 0, 8
        )
        with (
            patch.object(
                decomp_discover.decomp_binary,
                "sections",
                return_value=(section,),
            ),
            patch.object(
                decomp_discover.decomp_binary,
                "read_bytes",
                return_value=b"\x00\x10\x40\x00\x78\x56\x34\x12",
            ),
        ):
            self.assertEqual(
                scan_data_references(functions),
                {0x401000: frozenset({0x500000})},
            )

    def test_finds_code_targets_in_annotated_global_relocation_runs(self):
        text = decomp_discover.decomp_binary.Section(
            ".text", 0x401000, 0x3000, 0, 0x3000, 0x20000000
        )
        data = decomp_discover.decomp_binary.Section(
            ".data", 0x500000, 0x1000, 0, 0x1000
        )
        words = {
            0x500000: b"\x00\x10\x40\x00",
            0x500004: b"\x00\x20\x40\x00",
            0x50000C: b"\x10\x10\x40\x00",
            0x500020: b"\x00\x00\x50\x00",
        }
        with (
            patch.object(decomp_discover.decomp_binary, "available", return_value=True),
            patch.object(
                decomp_discover.decomp_binary,
                "sections",
                return_value=(text, data),
            ),
            patch.object(
                decomp_discover.decomp_binary,
                "read_bytes",
                side_effect=lambda address, _size: words.get(address),
            ),
        ):
            references = scan_annotated_relocation_targets(
                frozenset({0x500000, 0x500020}),
                frozenset({0x500000, 0x500004, 0x50000C, 0x500020}),
            )

        self.assertEqual(
            references,
            {
                0x401000: frozenset({0x500000}),
                0x402000: frozenset({0x500004}),
            },
        )


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.entries = [(0x401000, "N::A"), (0x404000, "N::D")]
        self.functions = [
            GhidraFunction(0x401000, 0x20, "A"),
            GhidraFunction(0x401010, 4, "interior_false_positive"),
            GhidraFunction(0x402000, 0x20, "call_target"),
            GhidraFunction(0x403000, 0x20, "jump_target"),
            GhidraFunction(0x403800, 0x20, "ghidra_only"),
            GhidraFunction(0x404000, 0x10, "D"),
            GhidraFunction(0x405000, 0x10, "crt"),
        ]
        self.transfers = {
            0x402000: TransferEvidence(frozenset({0x401000}), frozenset()),
            0x403000: TransferEvidence(frozenset(), frozenset({0x404000})),
        }

    def test_ranks_call_then_tail_jump_and_hides_weak_starts(self):
        results = discover(self.entries, self.functions, self.transfers)
        self.assertEqual([item.address for item in results], [0x402000, 0x403000])
        self.assertEqual([item.confidence for item in results], ["high", "medium"])

    def test_all_includes_ghidra_only_but_rejects_overlaps_and_crt(self):
        results = discover(
            self.entries, self.functions, self.transfers, minimum_confidence="low"
        )
        self.assertEqual(
            [item.address for item in results], [0x402000, 0x403000, 0x403800]
        )

    def test_outbound_project_call_promotes_an_unmapped_caller(self):
        transfers = dict(self.transfers)
        transfers[0x403800] = TransferEvidence(
            frozenset(), frozenset(), frozenset({0x401000})
        )
        results = discover(self.entries, self.functions, transfers)
        promoted = next(item for item in results if item.address == 0x403800)
        self.assertEqual(promoted.confidence, "medium")
        self.assertEqual(promoted.called_project_targets, (0x401000,))

    def test_relocation_adds_a_start_that_ghidra_did_not_define(self):
        references = {0x402800: frozenset({0x500004})}
        functions = add_relocation_target_starts(self.functions, references)
        added = next(function for function in functions if function.address == 0x402800)
        self.assertEqual(added.size, 0)
        self.assertEqual(added.name, "")

        results = discover(
            self.entries,
            functions,
            self.transfers,
            relocation_references=references,
        )
        found = next(item for item in results if item.address == 0x402800)
        self.assertEqual(found.confidence, "medium")
        self.assertEqual(found.relocation_references, (0x500004,))
        self.assertIn("annotated-global relocation", found.reason)

    def test_relocation_overrides_a_stale_ghidra_body_range(self):
        functions = [
            GhidraFunction(0x401000, 0x20, "A"),
            GhidraFunction(0x404000, 0x10, "D"),
        ]
        references = {0x401010: frozenset({0x500000})}
        results = discover(
            self.entries,
            add_relocation_target_starts(functions, references),
            {},
            relocation_references=references,
        )
        self.assertEqual([item.address for item in results], [0x401010])


if __name__ == "__main__":
    unittest.main()
