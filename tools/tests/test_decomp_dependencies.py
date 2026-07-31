from __future__ import annotations

import unittest
from unittest.mock import patch

from tools import decomp_binary, decomp_dependencies
from tools.decomp_dependencies import (
    build_call_graph,
    strongly_connected_components,
)


class BinaryReadTests(unittest.TestCase):
    def test_reads_one_mapped_range_without_crossing_the_section(self):
        section = decomp_binary.Section(".text", 0x401000, 4, 2, 4)
        with patch.object(
            decomp_binary, "_image", return_value=(b"XXabcd", 0x400000, (section,))
        ):
            self.assertEqual(decomp_binary.read_bytes(0x401001, 2), b"bc")
            self.assertIsNone(decomp_binary.read_bytes(0x401003, 2))


class ComponentTests(unittest.TestCase):
    def test_collapses_mutual_recursion(self):
        by_node, components = strongly_connected_components(
            {1, 2, 3}, {1: {2}, 2: {1, 3}, 3: set()}
        )
        self.assertEqual(by_node[1], by_node[2])
        self.assertNotEqual(by_node[1], by_node[3])
        self.assertIn(frozenset({1, 2}), components)


class CallGraphTests(unittest.TestCase):
    class Operand:
        def __init__(self, operand_type, immediate=0):
            self.type = operand_type
            self.imm = immediate

    class Instruction:
        def __init__(self, mnemonic, operands):
            self.mnemonic = mnemonic
            self.operands = operands

    class Disassembler:
        detail = False

        def disasm(self, _code, address):
            immediate = 1
            register = 2
            if address == 0x401000:
                return [
                    CallGraphTests.Instruction(
                        "call", [CallGraphTests.Operand(immediate, 0x402000)]
                    ),
                    CallGraphTests.Instruction(
                        "jmp", [CallGraphTests.Operand(immediate, 0x403000)]
                    ),
                    CallGraphTests.Instruction(
                        "call", [CallGraphTests.Operand(register)]
                    ),
                    CallGraphTests.Instruction(
                        "jmp", [CallGraphTests.Operand(register)]
                    ),
                    CallGraphTests.Instruction(
                        "jne", [CallGraphTests.Operand(immediate, 0x403000)]
                    ),
                ]
            if address == 0x403000:
                return [
                    CallGraphTests.Instruction(
                        "call", [CallGraphTests.Operand(immediate, 0x401000)]
                    )
                ]
            return []

    def test_extracts_direct_calls_and_tail_jumps(self):
        entries = [
            (0x401000, "N::A"),
            (0x402000, "N::B"),
            (0x403000, "N::C"),
        ]
        with (
            patch.object(decomp_dependencies.decomp_binary, "available", return_value=True),
            patch.object(
                decomp_dependencies.decomp_binary,
                "sections",
                return_value=(
                    decomp_dependencies.decomp_binary.Section(
                        ".text", 0x401000, 0x3000, 0, 0x3000
                    ),
                ),
            ),
            patch.object(decomp_dependencies.decomp_binary, "read_bytes", return_value=b"x"),
            patch.object(
                decomp_dependencies,
                "_capstone",
                return_value=(self.Disassembler(), 1),
            ),
        ):
            graph = build_call_graph(entries)
        self.assertEqual(graph.callees[0x401000], frozenset({0x402000, 0x403000}))
        self.assertEqual(graph.callees[0x403000], frozenset({0x401000}))
        self.assertEqual(graph.callers[0x402000], frozenset({0x401000}))
        self.assertEqual(graph.callers[0x401000], frozenset({0x403000}))
        self.assertEqual(graph.indirect_calls[0x401000], 1)
        self.assertEqual(graph.indirect_jumps[0x401000], 1)


if __name__ == "__main__":
    unittest.main()
