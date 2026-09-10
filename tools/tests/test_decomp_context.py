from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import decomp_context as context  # noqa: E402
from tools import decomp_doctor as doctor  # noqa: E402


class ContextPackTests(unittest.TestCase):
    def test_direct_script_help_bootstraps_repository_imports(self):
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/decomp_context.py"), "--help"],
                cwd=directory,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--brief BRIEF", result.stdout)

    @unittest.skipUnless(
        (ROOT / ".tooling/venv/bin/python").is_file(),
        "the repository wrapper needs the configured Linux tool environment",
    )
    def test_linux_wrapper_context_help_uses_the_read_only_dispatch(self):
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        result = subprocess.run(
            [str(ROOT / "tools/decomp"), "context", "--help"],
            cwd=ROOT,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--brief BRIEF", result.stdout)

    @staticmethod
    def artifact(root: Path, rows: list[dict[str, object]]) -> dict[str, object]:
        return doctor._store_ghidra_artifact(
            root, "0x00401000", "disassembly", rows
        )

    @staticmethod
    def campaign_rows(
        campaign_id: str,
        address: int,
        *,
        rejected: bool = False,
        pushed: bool = True,
    ) -> list[dict[str, object]]:
        finalize_hash = "a" * 64
        delivery_hash = "b" * 64
        commit = "c" * 40
        receipt_key = context._snapshot_hash(campaign_id)
        rows = [
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": campaign_id,
                "result": "source",
                "mode": "refinement",
                "lane": "production",
                "active_addresses": [f"0x{address:08X}"],
                "finalize_receipt": {
                    "path": f"build/decomp-cache/finalize/{campaign_id}.json",
                    "content_sha256": finalize_hash,
                    "file_sha256": "d" * 64,
                    "receipt_key": receipt_key,
                },
            }
        ]
        for status in ("staged", "accepted"):
            rows.append(
                {
                    "schema_version": 3,
                    "record_type": "delivery",
                    "campaign_id": campaign_id,
                    "status": status,
                    "phase": status,
                    "mode": "refinement",
                    "lane": "production",
                    "addresses": [f"0x{address:08X}"],
                    "delivery_id": f"{campaign_id}-{status}",
                    "receipt_sha256": finalize_hash,
                }
            )
        for status in ("integrated", "committed") + (("pushed",) if pushed else ()):
            rows.append(
                {
                    "schema_version": 3,
                    "record_type": "delivery",
                    "campaign_id": campaign_id,
                    "status": status,
                    "phase": "commit" if status == "committed" else "push" if status == "pushed" else status,
                    "mode": "refinement",
                    "lane": "production",
                    "addresses": [f"0x{address:08X}"],
                    "delivery_id": f"{campaign_id}-{status}",
                    "receipt_sha256": delivery_hash,
                    "delivery_receipt_path": f"build/decomp-cache/delivery/{campaign_id}/{delivery_hash}.json",
                    "commit": commit,
                }
            )
        if rejected:
            rows.append(
                {
                    "schema_version": 3,
                    "record_type": "delivery",
                    "campaign_id": campaign_id,
                    "status": "rejected",
                    "phase": "rejected",
                    "mode": "refinement",
                    "lane": "production",
                    "addresses": [f"0x{address:08X}"],
                    "delivery_id": f"{campaign_id}-rejected",
                }
            )
        return rows

    @staticmethod
    def materialize_receipts(root: Path, rows: list[dict[str, object]]) -> None:
        source_snapshot = {
            path.relative_to(root).as_posix(): context._sha256(path)
            for path in sorted((root / "src").rglob("*"))
            if path.is_file()
        }
        campaigns = {
            row["campaign_id"]: row
            for row in rows
            if row.get("record_type") == "campaign"
        }
        for campaign_id, campaign in campaigns.items():
            finalize = campaign["finalize_receipt"]
            key_payload = {
                "receipt_version": 2,
                "campaign_id": campaign_id,
                "result": "source",
                "mode": "refinement",
                "lane": "production",
                "active_addresses": campaign["active_addresses"],
                "source_worktree_snapshot": source_snapshot,
                "source_worktree_sha256": context._snapshot_hash(source_snapshot),
                "repository_worktree_snapshot": source_snapshot,
                "repository_worktree_sha256": context._snapshot_hash(
                    source_snapshot
                ),
            }
            finalize["receipt_key"] = context._snapshot_hash(key_payload)
            finalize_document = {
                "schema_version": 3,
                "receipt_version": 2,
                "record_type": "finalize-receipt",
                "status": "passed",
                "campaign_id": campaign_id,
                "result": "source",
                "mode": "refinement",
                "lane": "production",
                "receipt_key": finalize["receipt_key"],
                "key_payload": key_payload,
            }
            finalize_document["receipt_sha256"] = context._snapshot_hash(
                finalize_document
            )
            finalize_path = (
                root
                / "build/decomp-cache/finalize"
                / f"{finalize['receipt_key']}.json"
            )
            finalize_path.parent.mkdir(parents=True, exist_ok=True)
            finalize_path.write_text(
                json.dumps(finalize_document, sort_keys=True), encoding="utf-8"
            )
            finalize["content_sha256"] = finalize_document["receipt_sha256"]
            finalize["file_sha256"] = context._sha256(finalize_path)
            finalize["path"] = finalize_path.relative_to(root).as_posix()
            for row in rows:
                if row.get("campaign_id") == campaign_id and row.get("status") in {
                    "staged",
                    "accepted",
                }:
                    row["receipt_sha256"] = finalize_document["receipt_sha256"]

            commit = "c" * 40
            delivery_document = {
                "schema_version": 3,
                "receipt_version": 2,
                "record_type": "delivery-receipt",
                "status": "passed",
                "campaign_id": campaign_id,
                "campaign_record_sha256": context._snapshot_hash(campaign),
                "source_commit": commit,
            }
            delivery_document["content_sha256"] = context._snapshot_hash(
                delivery_document
            )
            delivery_hash = delivery_document["content_sha256"]
            delivery_path = (
                root
                / "build/decomp-cache/delivery"
                / campaign_id
                / f"{delivery_hash}.json"
            )
            delivery_path.parent.mkdir(parents=True, exist_ok=True)
            delivery_path.write_text(
                json.dumps(delivery_document, sort_keys=True), encoding="utf-8"
            )
            for row in rows:
                if row.get("campaign_id") == campaign_id and row.get("status") in {
                    "integrated",
                    "committed",
                    "pushed",
                }:
                    row["receipt_sha256"] = delivery_hash
                    row["delivery_receipt_path"] = delivery_path.relative_to(root).as_posix()
                    row["commit"] = commit

    @staticmethod
    def write_retrieval_inputs(root: Path) -> None:
        source = root / "src/Examples.cpp"
        source.parent.mkdir(parents=True)
        source.write_text(
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
            "void Target(int value) {}\n"
            "// FUNCTION: TOY2 0x00402000 [MATCHED]\n"
            "void Accepted(int value) {}\n"
            "// FUNCTION: TOY2 0x00403000 [MATCHED]\n"
            "void Rejected(int value) {}\n"
            "// FUNCTION: TOY2 0x00404000 [PROVISIONAL]\n"
            "void Provisional(int value) {}\n"
            "// FUNCTION: TOY2 0x00405000 [MATCHED]\n"
            "void Debt(int value) {}\n"
            "// STUB: TOY2 0x00406000\n"
            "void Stub() {}\n"
            "// FUNCTION: TOY2 0x00407000 [MATCHED]\n"
            "void MissingPush() {}\n",
            encoding="utf-8",
        )
        resources = root / "tools/Resources"
        resources.mkdir(parents=True)
        addresses = range(0x401000, 0x408000, 0x1000)
        (resources / "functions_map.txt").write_text(
            "".join(f"0x{address:08X} Ns::Function{address:08X}\n" for address in addresses),
            encoding="utf-8",
        )
        sizes = root / "build/decomp-function-sizes.json"
        sizes.parent.mkdir(parents=True)
        sizes.write_text(
            json.dumps(
                [{"address": f"0x{address:08X}", "size": 64} for address in addresses]
            ),
            encoding="utf-8",
        )
        report_rows = [
            {"address": "0x00401000", "matching": 0.7},
            {"address": "0x00402000", "matching": 1.0},
            {"address": "0x00403000", "matching": 1.0},
            {"address": "0x00404000", "matching": 0.8},
            {"address": "0x00405000", "matching": 1.0},
            {"address": "0x00406000", "matching": 1.0},
            {"address": "0x00407000", "matching": 1.0},
        ]
        (root / "build/decomp-current-report.json").write_text(
            json.dumps({"data": report_rows}), encoding="utf-8"
        )
        ledger_rows: list[dict[str, object]] = []
        ledger_rows.extend(ContextPackTests.campaign_rows("accepted", 0x402000))
        ledger_rows.extend(
            ContextPackTests.campaign_rows("rejected", 0x403000, rejected=True)
        )
        ledger_rows.extend(ContextPackTests.campaign_rows("provisional", 0x404000))
        ledger_rows.extend(ContextPackTests.campaign_rows("debt", 0x405000))
        ledger_rows.extend(ContextPackTests.campaign_rows("stub", 0x406000))
        ledger_rows.extend(
            ContextPackTests.campaign_rows("missing-push", 0x407000, pushed=False)
        )
        ContextPackTests.materialize_receipts(root, ledger_rows)
        (resources / "campaign-ledger.jsonl").write_text(
            "".join(json.dumps(row, sort_keys=True) + "\n" for row in ledger_rows),
            encoding="utf-8",
        )

    @staticmethod
    def write_context_brief(root: Path) -> tuple[Path, dict[str, object]]:
        resources = root / "tools/Resources"
        resources.mkdir(parents=True, exist_ok=True)
        (resources / "functions_map.txt").write_text(
            "0x00401000 Ns::Target\n", encoding="utf-8"
        )
        (resources / "campaign-ledger.jsonl").write_text("", encoding="utf-8")
        source = root / "src/Target.cpp"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid Target() {}\n",
            encoding="utf-8",
        )
        build = root / "build"
        build.mkdir(parents=True, exist_ok=True)
        (build / "decomp-function-sizes.json").write_text(
            '[{"address":"0x00401000","size":2}]', encoding="utf-8"
        )
        (build / "decomp-current-report.json").write_text(
            '{"data":[{"address":"0x00401000","matching":0.5}]}',
            encoding="utf-8",
        )
        retail = root / "original/toy2.exe"
        retail.parent.mkdir(parents=True)
        retail.write_bytes(b"retail")
        rows = [
            {"address": "00401000", "bytes": "90", "mnemonic": "NOP"},
            {"address": "00401001", "bytes": "c3", "mnemonic": "RET"},
        ]
        artifact = ContextPackTests.artifact(root, rows)
        receipt_id = "d" * 64
        receipt = (
            root
            / "build/decomp-cache/doctor/receipts"
            / f"{receipt_id}.json"
        )
        receipt.parent.mkdir(parents=True, exist_ok=True)
        receipt.write_text('{"status":"ready"}\n', encoding="utf-8")
        doctor_descriptor = {
            "path": str(receipt),
            "sha256": context._sha256(receipt),
            "receipt_id": receipt_id,
            "source_artifacts": {"disassembly": artifact},
        }
        decoded = {
            0x401000: context.DecodedInstruction(0x401000, 1, "nop", ""),
            0x401001: context.DecodedInstruction(0x401001, 1, "ret", ""),
        }
        pack = context.build_context_pack(
            root,
            0x401000,
            doctor=doctor_descriptor,
            evidence={"abi": {"return_type": "void"}},
            decoder=lambda _code, address: decoded[address],
            graph=context.DependencyGraph({}, {}, {}, {}),
            debt_by_address={},
        )
        document: dict[str, object] = {
            "schema": 1,
            "lane": "production",
            "target": "0x00401000",
            "evidence": {"context_pack": pack},
        }
        document["content_sha256"] = context._snapshot_hash(document)
        brief_path = root / "build/decomp-cache/briefs/production-fixture.json"
        brief_path.parent.mkdir(parents=True, exist_ok=True)
        brief_path.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")
        return brief_path, pack

    @staticmethod
    def instruction(
        address: int,
        mnemonic: str,
        *,
        target: int | None = None,
        indirect: bool = False,
    ) -> dict[str, object]:
        return {
            "address": f"0x{address:08X}",
            "bytes": "90",
            "size": 1,
            "mnemonic": mnemonic,
            "operands": "",
            "direct_target": f"0x{target:08X}" if target is not None else None,
            "indirect": indirect,
            "memory": [],
        }

    def test_normalization_is_stable_for_shuffled_rows(self):
        decoded = {
            0x401000: context.DecodedInstruction(0x401000, 1, "NOP", ""),
            0x401001: context.DecodedInstruction(0x401001, 1, "RET", ""),
        }

        def decoder(_code: bytes, address: int) -> context.DecodedInstruction:
            return decoded[address]

        ordered = [
            {"address": "00401000", "bytes": "90", "mnemonic": "NOP"},
            {"address": "00401001", "bytes": "c3", "mnemonic": "RET"},
        ]
        shuffled = list(reversed(ordered))

        self.assertEqual(
            context.normalize_disassembly(ordered, decoder=decoder),
            context.normalize_disassembly(shuffled, decoder=decoder),
        )

    def test_normalization_accepts_equivalent_x86_condition_aliases(self):
        aliases = [
            ("jz", "je", "7400"),
            ("jnz", "jne", "7500"),
            ("jc", "jb", "7200"),
            ("jnae", "jb", "7200"),
            ("jnb", "jae", "7300"),
            ("jnc", "jae", "7300"),
            ("jna", "jbe", "7600"),
            ("jnbe", "ja", "7700"),
            ("jpe", "jp", "7a00"),
            ("jpo", "jnp", "7b00"),
            ("jnge", "jl", "7c00"),
            ("jnl", "jge", "7d00"),
            ("jng", "jle", "7e00"),
            ("jnle", "jg", "7f00"),
            ("loopz", "loope", "e100"),
            ("loopnz", "loopne", "e000"),
        ]
        for alias, capstone_name, encoded in aliases:
            for artifact_name, decoded_name in (
                (alias.upper(), capstone_name),
                (capstone_name.upper(), alias),
            ):
                with self.subTest(
                    artifact=artifact_name,
                    decoded=decoded_name,
                ):
                    code = bytes.fromhex(encoded)
                    decoded = context.DecodedInstruction(
                        0x401000,
                        len(code),
                        decoded_name,
                        "0x401002",
                        direct_target=0x401002,
                    )
                    result = context.normalize_disassembly(
                        [
                            {
                                "address": "00401000",
                                "bytes": encoded,
                                "mnemonic": artifact_name,
                            }
                        ],
                        decoder=lambda _code, _address: decoded,
                    )

                    self.assertEqual(result[0]["mnemonic"], decoded_name)

    def test_normalization_accepts_the_zipline_jz_artifact(self):
        decoded = context.DecodedInstruction(
            0x4359DF,
            6,
            "je",
            "0x435f18",
            direct_target=0x435F18,
        )

        result = context.normalize_disassembly(
            [
                {
                    "address": "004359df",
                    "bytes": "0f8433050000",
                    "mnemonic": "JZ",
                    "operands": ["0x00435f18"],
                }
            ],
            decoder=lambda _code, _address: decoded,
        )

        self.assertEqual(result[0]["mnemonic"], "je")
        self.assertEqual(result[0]["direct_target"], "0x00435F18")

    def test_normalization_rejects_a_different_x86_condition(self):
        decoded = context.DecodedInstruction(0x401000, 2, "jne", "0x401002")

        with self.assertRaisesRegex(context.ContextError, "artifact row"):
            context.normalize_disassembly(
                [
                    {
                        "address": "00401000",
                        "bytes": "7400",
                        "mnemonic": "JZ",
                    }
                ],
                decoder=lambda _code, _address: decoded,
            )

    def test_straight_line_call_and_return_stay_in_one_block(self):
        rows = [
            self.instruction(0x401000, "mov"),
            self.instruction(0x401001, "call", target=0x402000),
            self.instruction(0x401002, "ret"),
        ]

        graph = context.build_control_flow(rows, target="0x00401000", size=3)

        self.assertEqual(len(graph["basic_blocks"]), 1)
        self.assertEqual(graph["edges"], [])
        self.assertEqual(graph["calls"][0]["target"], "0x00402000")
        self.assertEqual(graph["returns"], [{"address": "0x00401002"}])

    def test_diamond_has_stable_blocks_edges_and_dominators(self):
        rows = [
            self.instruction(0x401000, "jne", target=0x401003),
            self.instruction(0x401001, "nop"),
            self.instruction(0x401002, "jmp", target=0x401004),
            self.instruction(0x401003, "nop"),
            self.instruction(0x401004, "ret"),
        ]

        graph = context.build_control_flow(rows, target=0x401000, size=5)

        self.assertEqual(
            [block["start"] for block in graph["basic_blocks"]],
            ["0x00401000", "0x00401001", "0x00401003", "0x00401004"],
        )
        join = next(row for row in graph["dominators"] if row["block"] == "0x00401004")
        self.assertEqual(join["dominators"], ["0x00401000", "0x00401004"])

    def test_loop_backedge_requires_dominance(self):
        rows = [
            self.instruction(0x401000, "nop"),
            self.instruction(0x401001, "jne", target=0x401000),
            self.instruction(0x401002, "ret"),
        ]

        graph = context.build_control_flow(rows, target=0x401000, size=3)

        self.assertEqual(
            graph["loop_backedges"],
            [{"from": "0x00401000", "to": "0x00401000", "kind": "branch"}],
        )

    def test_dominators_exclude_unreachable_blocks_and_exits_are_explicit(self):
        rows = [
            self.instruction(0x401000, "jmp", target=0x401003),
            self.instruction(0x401001, "jmp", target=0x401003),
            self.instruction(0x401002, "ret"),
            self.instruction(0x401003, "ret"),
        ]

        graph = context.build_control_flow(rows, target=0x401000, size=4)

        self.assertEqual(
            graph["unreachable_blocks"], ["0x00401001", "0x00401002"]
        )
        self.assertEqual(
            graph["dominators"],
            [
                {
                    "block": "0x00401000",
                    "dominators": ["0x00401000"],
                },
                {
                    "block": "0x00401003",
                    "dominators": ["0x00401000", "0x00401003"],
                },
            ],
        )
        fall_off = context.build_control_flow(
            [self.instruction(0x401000, "nop")], target=0x401000, size=1
        )
        tail_jump = context.build_control_flow(
            [self.instruction(0x401000, "jmp", target=0x500000)],
            target=0x401000,
            size=1,
        )
        self.assertEqual(fall_off["exits"][0]["kind"], "fall-off")
        self.assertEqual(tail_jump["exits"][0]["kind"], "external-tail-jump")

    def test_gaps_and_indirect_jumps_make_context_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [
                {"address": "00401000", "bytes": "90", "mnemonic": "NOP"},
                {"address": "00401002", "bytes": "ff", "mnemonic": "JMP", "operands": ["EAX"]},
            ]
            descriptor = self.artifact(root, rows)

            def decoder(code: bytes, address: int) -> context.DecodedInstruction:
                return context.DecodedInstruction(
                    address,
                    len(code),
                    "nop" if address == 0x401000 else "jmp",
                    "" if address == 0x401000 else "eax",
                    indirect=address == 0x401002,
                )

            result = context.normalize_artifact(
                root, "0x00401000", 3, descriptor, decoder=decoder
            )

        self.assertFalse(result["completeness"]["complete"])
        self.assertIn(
            "instruction-gap:00401001-00401002",
            result["completeness"]["unknown_reasons"],
        )
        self.assertIn(
            "indirect-control-transfer:00401002",
            result["completeness"]["unknown_reasons"],
        )

    def test_memory_operands_keep_stack_absolute_and_base_classes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [
                {"address": f"0040100{index}", "bytes": "90", "mnemonic": "MOV"}
                for index in range(3)
            ]
            descriptor = self.artifact(root, rows)
            memories = {
                0x401000: context.DecodedMemory(1, "read", base="esp", displacement=4, size=4),
                0x401001: context.DecodedMemory(0, "write", displacement=0x550000, size=4),
                0x401002: context.DecodedMemory(1, "unknown", base="eax", index="ecx", scale=4, size=2),
            }

            def decoder(code: bytes, address: int) -> context.DecodedInstruction:
                return context.DecodedInstruction(
                    address, len(code), "mov", "", memory=(memories[address],)
                )

            result = context.normalize_artifact(
                root, 0x401000, 3, descriptor, decoder=decoder
            )

        self.assertEqual(
            [row["class"] for row in result["memory_operands"]],
            ["stack", "absolute", "base-index"],
        )
        self.assertFalse(result["completeness"]["memory_access"])
        self.assertIn(
            "unknown-memory-access:00401002:1",
            result["completeness"]["unknown_reasons"],
        )

    def test_artifact_decode_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [{"address": "00401000", "bytes": "90", "mnemonic": "NOP"}]
            descriptor = self.artifact(root, rows)
            decoder = lambda _code, address: context.DecodedInstruction(
                address, 1, "mov", "eax, eax"
            )

            with self.assertRaisesRegex(context.ContextError, "artifact row"):
                context.normalize_artifact(
                    root, 0x401000, 1, descriptor, decoder=decoder
                )

    def test_missing_capstone_uses_rows_and_marks_context_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [{"address": "00401000", "bytes": "c3", "mnemonic": "RET", "operands": []}]
            descriptor = self.artifact(root, rows)
            with patch.object(
                context, "_capstone_decoder", side_effect=context.ContextError("capstone-unavailable")
            ):
                result = context.normalize_artifact(root, 0x401000, 1, descriptor)

        self.assertFalse(result["completeness"]["complete"])
        self.assertFalse(result["completeness"]["control_flow"])
        self.assertFalse(result["completeness"]["memory_access"])
        self.assertIn("capstone-unavailable", result["completeness"]["unknown_reasons"])

    def test_instruction_limit_marks_all_byte_derived_sections_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [
                {"address": f"0040100{index}", "bytes": "90", "mnemonic": "NOP"}
                for index in range(3)
            ]
            descriptor = self.artifact(root, rows)
            decoder = lambda _code, address: context.DecodedInstruction(
                address, 1, "nop", ""
            )
            with patch.object(context, "MAX_INSTRUCTIONS", 2):
                result = context.normalize_artifact(
                    root, 0x401000, 3, descriptor, decoder=decoder
                )

        self.assertEqual(len(result["instructions"]), 2)
        self.assertIn("instruction-limit", result["completeness"]["unknown_reasons"])
        self.assertFalse(result["completeness"]["instruction_bytes"])
        self.assertFalse(result["completeness"]["control_flow"])
        self.assertFalse(result["completeness"]["memory_access"])

    def test_call_neighbors_are_depth_one_and_capped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src/Calls.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// FUNCTION: TOY2 0x00402000 [MATCHED]\n",
                encoding="utf-8",
            )
            report = root / "build/decomp-current-report.json"
            report.parent.mkdir(parents=True)
            report.write_text(
                json.dumps(
                    {"data": [{"address": "0x00402000", "matching": 1.0}]}
                ),
                encoding="utf-8",
            )
            callees = frozenset(range(0x402000, 0x402200, 0x10))
            graph = context.DependencyGraph(
                callees={0x401000: callees, 0x402000: frozenset({0x499000})},
                callers={0x401000: frozenset({0x400000})},
                indirect_calls={},
                indirect_jumps={},
            )
            entries = [(address, f"Function{address:08X}") for address in callees]
            entries.extend([(0x400000, "Caller"), (0x401000, "Target")])

            neighbors = context.build_call_neighbors(
                root, 0x401000, graph=graph, entries=entries
            )

        self.assertEqual(len(neighbors), context.MAX_CALL_NODES)
        self.assertEqual(neighbors[0]["direction"], "caller")
        accepted = next(row for row in neighbors if row["address"] == "0x00402000")
        self.assertEqual(accepted["state"], "FUNCTION")
        self.assertEqual(accepted["status"], "exact")
        self.assertNotIn("0x00499000", {row["address"] for row in neighbors})

    def test_incoming_call_evidence_requires_the_target_and_caller_bounds(self):
        neighbor = {
            "address": "0x00400000",
            "direction": "caller",
            "name": "Caller",
        }
        artifact = {"path": "build/xrefs.json", "sha256": "a" * 64}
        entries = [(0x400000, "Ns::Caller"), (0x401000, "Ns::Target")]
        common = {
            "neighbors": [neighbor],
            "instructions": [],
            "target": 0x401000,
            "xref_artifact": artifact,
            "entries": entries,
            "function_sizes": {0x400000: 0x20, 0x401000: 0x10},
        }

        rejected, reasons = context.bind_call_neighbor_evidence(
            xrefs=[
                {
                    "from": "00400010",
                    "to": "00402000",
                    "from_function": "Caller",
                    "ref_type": "UNCONDITIONAL_CALL",
                }
            ],
            **common,
        )
        accepted, _ = context.bind_call_neighbor_evidence(
            xrefs=[
                {
                    "from": "00400010",
                    "to": "00401000",
                    "from_function": "Caller",
                    "ref_type": "UNCONDITIONAL_CALL",
                }
            ],
            **common,
        )

        self.assertEqual(rejected, [])
        self.assertEqual(reasons, ["unproven-call-neighbor:00400000:caller"])
        self.assertEqual(accepted[0]["call_site"], "0x00400010")
        self.assertIn("@.to=='00401000'", accepted[0]["evidence"]["json_locator"])

    def test_accepted_source_requires_current_terminal_debt_free_full_chain(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_retrieval_inputs(root)

            examples = context.build_accepted_sources(
                root,
                0x401000,
                graph=context.DependencyGraph({}, {}, {}, {}),
                debt_by_address={0x405000: ["unknown-symbol"]},
            )

        self.assertEqual([row["address"] for row in examples], ["0x00402000"])
        provenance = examples[0]["provenance"]
        self.assertEqual(provenance["campaign_id"], "accepted")
        self.assertEqual(provenance["commit"], "c" * 40)
        self.assertRegex(provenance["source"]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(len(provenance["ledger_locators"]["delivery"]), 5)
        self.assertEqual(len(provenance["delivery_ids"]), 5)
        self.assertFalse(Path(provenance["finalize_receipt"]["path"]).is_absolute())
        self.assertTrue(examples[0]["source_excerpt_complete"])

    def test_accepted_source_retrieval_can_be_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_retrieval_inputs(root)
            (root / "tools/Resources/campaign-ledger.jsonl").write_text("", encoding="utf-8")

            examples = context.build_accepted_sources(
                root,
                0x401000,
                graph=context.DependencyGraph({}, {}, {}, {}),
                debt_by_address={},
            )

        self.assertEqual(examples, [])

    def test_accepted_source_rejects_a_tampered_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_retrieval_inputs(root)
            campaign = json.loads(
                (root / "tools/Resources/campaign-ledger.jsonl")
                .read_text(encoding="utf-8")
                .splitlines()[0]
            )
            receipt = root / campaign["finalize_receipt"]["path"]
            receipt.write_text(receipt.read_text(encoding="utf-8") + " ", encoding="utf-8")

            examples = context.build_accepted_sources(
                root,
                0x401000,
                graph=context.DependencyGraph({}, {}, {}, {}),
                debt_by_address={0x405000: ["unknown-symbol"]},
            )

        self.assertEqual(examples, [])

    def test_accepted_source_rejects_source_changed_after_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_retrieval_inputs(root)
            source = root / "src/Examples.cpp"
            source.write_text(
                source.read_text(encoding="utf-8") + "// Later change.\n",
                encoding="utf-8",
            )

            examples = context.build_accepted_sources(
                root,
                0x401000,
                graph=context.DependencyGraph({}, {}, {}, {}),
                debt_by_address={0x405000: ["unknown-symbol"]},
            )

        self.assertEqual(examples, [])

    def test_accepted_source_rejects_reordered_or_malformed_delivery_rows(self):
        for fault in ("reordered", "malformed"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.write_retrieval_inputs(root)
                ledger = root / "tools/Resources/campaign-ledger.jsonl"
                rows = [json.loads(line) for line in ledger.read_text().splitlines()]
                if fault == "reordered":
                    rows[1], rows[2] = rows[2], rows[1]
                else:
                    rows.append(
                        {
                            "schema_version": 3,
                            "record_type": "delivery",
                            "status": "pushed",
                        }
                    )
                ledger.write_text(
                    "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
                    encoding="utf-8",
                )

                examples = context.build_accepted_sources(
                    root,
                    0x401000,
                    graph=context.DependencyGraph({}, {}, {}, {}),
                    debt_by_address={0x405000: ["unknown-symbol"]},
                )

                self.assertEqual(examples, [])

    def test_accepted_source_rejects_nonfinite_or_duplicate_report_rows(self):
        for fault in ("nonfinite", "duplicate"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.write_retrieval_inputs(root)
                report = root / "build/decomp-current-report.json"
                document = json.loads(report.read_text(encoding="utf-8"))
                accepted = next(
                    row
                    for row in document["data"]
                    if row["address"] == "0x00402000"
                )
                if fault == "nonfinite":
                    accepted["matching"] = float("nan")
                else:
                    document["data"].append(dict(accepted))
                report.write_text(json.dumps(document), encoding="utf-8")

                examples = context.build_accepted_sources(
                    root,
                    0x401000,
                    graph=context.DependencyGraph({}, {}, {}, {}),
                    debt_by_address={0x405000: ["unknown-symbol"]},
                )

                self.assertEqual(examples, [])

    def test_context_brief_loader_validates_pack_target_size_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, expected = self.write_context_brief(root)

            actual = context.load_context_from_brief(
                path, root=root, validate_repository=False
            )

        self.assertEqual(actual, expected)
        self.assertLessEqual(context._encoded_size(actual), context.MAX_CONTEXT_BYTES)

    def test_context_pack_validator_rejects_oversize_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _path, pack = self.write_context_brief(root)
            pack["abi"] = "x" * context.MAX_CONTEXT_BYTES
            pack["content_sha256"] = context.context_pack_hash(pack)

            with self.assertRaisesRegex(context.ContextError, "byte limit"):
                context.validate_context_pack(pack, target=0x401000, size=2)

    def test_context_brief_loader_rejects_pack_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, _pack = self.write_context_brief(root)
            document = json.loads(path.read_text(encoding="utf-8"))
            document["evidence"]["context_pack"]["target"] = "0x00402000"
            document["content_sha256"] = context._snapshot_hash(
                {key: value for key, value in document.items() if key != "content_sha256"}
            )
            path.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")

            with self.assertRaisesRegex(context.ContextError, "target"):
                context.load_context_from_brief(
                    path, root=root, validate_repository=False
                )

    def test_context_brief_loader_rejects_noncache_and_symlink_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, _pack = self.write_context_brief(root)
            outside = root / "brief.json"
            outside.write_bytes(path.read_bytes())
            with self.assertRaisesRegex(context.ContextError, "outside"):
                context.load_context_from_brief(
                    outside, root=root, validate_repository=False
                )
            link = path.with_name("link.json")
            link.symlink_to(path)
            with self.assertRaisesRegex(context.ContextError, "symbolic link"):
                context.load_context_from_brief(
                    link, root=root, validate_repository=False
                )


if __name__ == "__main__":
    unittest.main()
