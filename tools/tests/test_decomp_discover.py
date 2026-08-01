from __future__ import annotations

import unittest
from unittest.mock import patch

from tools import decomp_discover
from tools.decomp_discover import (
    GhidraFunction,
    TransferEvidence,
    discover,
    import_thunk_addresses,
    parse_ghidra_functions,
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


if __name__ == "__main__":
    unittest.main()
