import copy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools import decomp_binary
from tools import decomp_oracle as oracle


ROOT = Path(__file__).resolve().parents[2]
RETAIL_CODE = bytes.fromhex(
    "8b4c2404b8100000003bc87e06d1e03bc17cfac3"
)
ZERO_SHA = "0" * 64


def _descriptor(path: str, *, size: int = 1) -> dict[str, object]:
    return {"path": path, "sha256": ZERO_SHA, "size": size}


def synthetic_receipt(
    *,
    selected: bool = True,
    current_code: bytes = RETAIL_CODE,
) -> dict[str, object]:
    """Return a deeply valid receipt that has no external file dependency."""

    policy = oracle.load_policy()
    target = policy["targets"][0]
    scope = ["0x004B0740"] if selected else []
    targets = []
    status = "not-applicable"
    if selected:
        result = oracle._evaluate_target(
            target,
            map_name=target["function_map_name"],
            current_address=0x401E50,
            retail_code=RETAIL_CODE,
            current_code=current_code,
        )
        targets = [result]
        status = result["status"]
    receipt: dict[str, object] = {
        "schema_version": oracle.SCHEMA_VERSION,
        "kind": oracle.RECEIPT_KIND,
        "status": status,
        "semantic_equivalence_claim": False,
        "native_execution": False,
        "coverage": oracle.COVERAGE,
        "inputs": {
            "scope": scope,
            "selected_targets": scope,
            "repository": {
                "head": "0" * 40,
                "source_worktree_sha256": ZERO_SHA,
                "source_index_sha256": ZERO_SHA,
            },
            "policy": _descriptor("tools/Resources/leaf-oracles.json"),
            "tool": {
                **_descriptor("tools/decomp_oracle.py"),
                "backend": oracle.BACKEND,
            },
            "decoder": {
                "engine": oracle.BACKEND,
                "architecture": "ia32",
                "mode": 32,
                "dependency_free": True,
                "binary_reader": _descriptor("tools/decomp_binary.py"),
            },
            "support_tools": {
                "snapshot": _descriptor("tools/decomp_campaigns.py"),
                "provenance": _descriptor("tools/decomp_provenance.py"),
            },
            "python_runtime": {
                "implementation": "cpython",
                "version": [3, 11, 0],
                "executable": {"path": "/usr/bin/python3", "sha256": ZERO_SHA},
            },
            "function_map": _descriptor("tools/Resources/functions_map.txt"),
            "comparison_report": _descriptor(
                "build/decomp-current-report.json"
            ),
            "comparison_provenance": _descriptor(
                "build/decomp-current-report.json.provenance.json"
            ),
            "retail_image": _descriptor("original/toy2.exe"),
            "current_image": _descriptor("build/toy2.exe"),
            "current_symbols": _descriptor("build/toy2.pdb"),
        },
        "targets": targets,
    }
    receipt["content_sha256"] = oracle._document_hash(receipt)
    oracle.validate_document(receipt)
    return receipt


class OracleInterpreterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = oracle.load_policy()
        cls.target = cls.policy["targets"][0]

    def test_allowlisted_policy_has_exact_target_and_vectors(self):
        self.assertEqual(self.target["address"], "0x004B0740")
        self.assertEqual(self.target["name"], "Numerics::RoundUpToPowerOf2")
        self.assertEqual(
            self.target["function_map_name"],
            "Nu3D::Math::RoundUpToPowerOf2",
        )
        self.assertEqual(
            [
                (case["id"], case["input"], case["expected"])
                for case in self.target["cases"]
            ],
            list(oracle.REQUIRED_CASES),
        )
        self.assertFalse(
            any(case["input"] > 0x40000000 for case in self.target["cases"])
        )

    def test_retail_body_passes_each_expected_vector(self):
        for case in self.target["cases"]:
            with self.subTest(case=case["id"]):
                result = oracle.execute(RETAIL_CODE, self.target, case["input"])
                self.assertEqual(result["return"], case["expected"])
                self.assertEqual(result["stack_delta"], 4)
                self.assertEqual(result["returned_to"], "0x0BADF00D")
                self.assertTrue(result["callee_saved_preserved"])
                self.assertEqual(result["memory_writes"], [])

    def test_longest_vectors_fit_the_committed_limits(self):
        for input_value in (536870913, 1073741824):
            result = oracle.execute(RETAIL_CODE, self.target, input_value)
            self.assertEqual(result["instructions"], 83)
            self.assertEqual(result["branches"], 27)
        self.assertEqual(self.target["limits"]["max_instructions"], 128)
        self.assertEqual(self.target["limits"]["max_branches"], 64)

    def test_runtime_limits_fail_closed(self):
        short_steps = copy.deepcopy(self.target)
        short_steps["limits"]["max_instructions"] = 82
        with self.assertRaisesRegex(oracle.ExecutionError, "instruction limit"):
            oracle.execute(RETAIL_CODE, short_steps, 1073741824)
        short_branches = copy.deepcopy(self.target)
        short_branches["limits"]["max_branches"] = 26
        with self.assertRaisesRegex(oracle.ExecutionError, "branch limit"):
            oracle.execute(RETAIL_CODE, short_branches, 1073741824)

    def test_signed_cmp_flags_cover_overflow_boundaries(self):
        negative_less = oracle._cmp_flags(0x80000000, 0x10)
        positive_greater = oracle._cmp_flags(0x7FFFFFFF, 0xFFFFFFFF)
        self.assertNotEqual(negative_less["sf"], negative_less["of"])
        self.assertEqual(positive_greater["sf"], positive_greater["of"])
        self.assertFalse(positive_greater["zf"])
        self.assertFalse(negative_less["cf"])

    def test_decoder_accepts_the_full_cyclic_body(self):
        decoded = oracle.decode_body(RETAIL_CODE, self.target["limits"])
        self.assertEqual(sorted(decoded), [0, 4, 9, 11, 13, 15, 17, 19])
        self.assertEqual(decoded[17].successors, (19, 13))
        self.assertEqual(decoded[19].operation, "ret")

    def test_decoder_rejects_unsupported_or_unsafe_forms(self):
        mutations = {
            "call": bytes.fromhex("e800000000") + RETAIL_CODE[5:],
            "indirect": bytes.fromhex("ff20") + RETAIL_CODE[2:],
            "wrong-stack-offset": RETAIL_CODE[:3] + b"\x08" + RETAIL_CODE[4:],
            "memory-cmp": RETAIL_CODE[:10] + b"\x08" + RETAIL_CODE[11:],
            "out-of-window-branch": RETAIL_CODE[:12] + b"\x7e\x7f" + RETAIL_CODE[14:],
            "interior-branch": RETAIL_CODE[:12] + b"\x7e\xfc" + RETAIL_CODE[14:],
            "unsupported": b"\xcc" + RETAIL_CODE[1:],
        }
        for name, code in mutations.items():
            with self.subTest(name=name), self.assertRaises(oracle.ExecutionError):
                oracle.decode_body(code, self.target["limits"])

    def test_decoder_rejects_unreachable_padding(self):
        with self.assertRaisesRegex(oracle.ExecutionError, "unreachable"):
            oracle.decode_body(RETAIL_CODE + b"\x90", self.target["limits"])

    def test_definite_initialization_and_abi_register_rules(self):
        protected_immediates = {
            "ebx": "bb73635343b810000000c3",
            "esp": "bc00ff1200b810000000c3",
            "ebp": "bd84746454b810000000c3",
            "esi": "be95857565b810000000c3",
            "edi": "bfa6968676b810000000c3",
            "stack-to-ebx": "8b5c2404b810000000c3",
        }
        for name, code_hex in protected_immediates.items():
            with self.subTest(name=name), self.assertRaisesRegex(
                oracle.ExecutionError, "protected ABI register"
            ):
                decoded = oracle.decode_body(
                    bytes.fromhex(code_hex), self.target["limits"]
                )
                oracle.validate_definite_initialization(decoded)
        invalid_reads = {
            "cmp-before-mov": "3bc1b810000000c3",
            "branch-before-cmp": "b8100000007e00c3",
            "eax-before-ret": "c3",
            "cmp-esp-left": "b810000000b900ff12003be17e00c3",
            "cmp-esp-right": "b810000000b900ff12003bcc7e00c3",
        }
        for name, code_hex in invalid_reads.items():
            with self.subTest(name=name), self.assertRaises(oracle.ExecutionError):
                decoded = oracle.decode_body(
                    bytes.fromhex(code_hex), self.target["limits"]
                )
                oracle.validate_definite_initialization(decoded)

    def test_memory_rejects_out_of_range_and_uninitialized_reads(self):
        with self.assertRaisesRegex(oracle.ExecutionError, "outside"):
            oracle._read_u32({}, 0, 0x1000, 32)
        with self.assertRaisesRegex(oracle.ExecutionError, "uninitialized"):
            oracle._read_u32({0x1000: 0}, 0x1000, 0x1000, 32)

    def test_dynamic_corpus_covers_each_instruction_and_branch_outcome(self):
        result = oracle._evaluate_target(
            self.target,
            map_name=self.target["function_map_name"],
            current_address=0x401E50,
            retail_code=RETAIL_CODE,
            current_code=RETAIL_CODE,
        )
        expected_offsets = [0, 4, 9, 11, 13, 15, 17, 19]
        expected_edges = [
            "11:not-taken",
            "11:taken",
            "17:not-taken",
            "17:taken",
        ]
        for image in ("retail", "current"):
            coverage = result["dynamic_coverage"][image]
            self.assertEqual(coverage["required_instruction_offsets"], expected_offsets)
            self.assertEqual(coverage["executed_instruction_offsets"], expected_offsets)
            self.assertEqual(coverage["required_conditional_edges"], expected_edges)
            self.assertEqual(coverage["executed_conditional_edges"], expected_edges)
            self.assertTrue(coverage["complete"])

    def test_incomplete_vector_corpus_fails_coverage(self):
        incomplete = copy.deepcopy(self.target)
        incomplete["cases"] = [copy.deepcopy(self.target["cases"][5])]
        result = oracle._evaluate_target(
            incomplete,
            map_name=self.target["function_map_name"],
            current_address=0x401E50,
            retail_code=RETAIL_CODE,
            current_code=RETAIL_CODE,
        )
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["dynamic_coverage"]["retail"]["complete"])
        self.assertFalse(result["dynamic_coverage"]["current"]["complete"])

    def test_current_execution_fault_is_a_failed_result(self):
        bad_current = b"\xcc" + RETAIL_CODE[1:]
        result = oracle._evaluate_target(
            self.target,
            map_name=self.target["function_map_name"],
            current_address=0x401E50,
            retail_code=RETAIL_CODE,
            current_code=bad_current,
        )
        self.assertEqual(result["status"], "failed")
        self.assertEqual(len(result["cases"]), 18)
        self.assertFalse(any(case["passed"] for case in result["cases"]))

    def test_symmetric_fault_cannot_pass(self):
        with self.assertRaisesRegex(oracle.OracleError, "retail function body"):
            oracle._evaluate_target(
                self.target,
                map_name=self.target["function_map_name"],
                current_address=0x401E50,
                retail_code=b"\xcc" + RETAIL_CODE[1:],
                current_code=b"\xcc" + RETAIL_CODE[1:],
            )

    def test_policy_tamper_is_rejected(self):
        variants = []
        changed_case = copy.deepcopy(self.policy)
        changed_case["targets"][0]["cases"][0]["expected"] = 17
        variants.append(changed_case)
        changed_opcode = copy.deepcopy(self.policy)
        code = bytearray.fromhex(changed_opcode["targets"][0]["retail"]["bytes"])
        code[0] = 0xCC
        changed_opcode["targets"][0]["retail"]["bytes"] = code.hex()
        changed_opcode["targets"][0]["retail"]["sha256"] = hashlib.sha256(code).hexdigest()
        variants.append(changed_opcode)
        changed_limit = copy.deepcopy(self.policy)
        changed_limit["targets"][0]["limits"]["max_instructions"] = 10000
        variants.append(changed_limit)
        changed_abi = copy.deepcopy(self.policy)
        changed_abi["targets"][0]["abi"]["stack_delta"] = 8
        variants.append(changed_abi)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "policy.json"
            for index, variant in enumerate(variants):
                with self.subTest(index=index):
                    path.write_text(json.dumps(variant), encoding="utf-8")
                    with self.assertRaises(oracle.OracleError):
                        oracle.load_policy(path)

    def test_strict_json_rejects_duplicate_fields_and_nonfinite_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.json"
            for text in ('{"a":1,"a":2}', '{"a":NaN}'):
                with self.subTest(text=text):
                    path.write_text(text, encoding="utf-8")
                    with self.assertRaises(oracle.OracleError):
                        oracle._read_json(path, "test input", limit=1024)

    def test_policy_symbolic_link_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "policy.json"
            destination.write_text(json.dumps(self.policy), encoding="utf-8")
            link = Path(directory) / "link.json"
            link.symlink_to(destination)
            with self.assertRaisesRegex(oracle.OracleError, "symbolic link"):
                oracle.load_policy(link)

    def test_production_policy_and_report_paths_are_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            alternate = Path(directory) / "copy.json"
            alternate.write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(oracle.OracleError, "production path"):
                oracle.build_receipt([], root=ROOT, policy_path=alternate)
            with self.assertRaisesRegex(oracle.OracleError, "production path"):
                oracle.build_receipt([], root=ROOT, report_path=alternate)

    def test_report_requires_one_exact_address_and_name(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "report.json"
            good = {
                "data": [
                    {
                        "address": "0x4b0740",
                        "name": "Numerics::RoundUpToPowerOf2",
                        "recomp": "0x401e50",
                        "type": 1,
                    }
                ]
            }
            report.write_text(json.dumps(good), encoding="utf-8")
            self.assertEqual(
                oracle._comparison_row(report, 0x4B0740)["recomp"], "0x401e50"
            )
            duplicate = copy.deepcopy(good)
            duplicate["data"].append(copy.deepcopy(duplicate["data"][0]))
            report.write_text(json.dumps(duplicate), encoding="utf-8")
            with self.assertRaisesRegex(oracle.OracleError, "one allowlisted"):
                oracle._comparison_row(report, 0x4B0740)

    def test_stale_report_provenance_is_rejected(self):
        from tools import decomp_provenance

        with patch.object(
            decomp_provenance,
            "validate_report",
            side_effect=ValueError("the comparison report source or build identity is stale"),
        ), self.assertRaisesRegex(oracle.OracleError, "identity is stale"):
            oracle._report_binding(ROOT, ROOT / "build/decomp-current-report.json")

    def test_pe_body_requires_executable_raw_and_virtual_bounds(self):
        image = bytes(range(64))
        path = Path("image.exe")

        def metadata(raw_size=20, virtual_size=20, characteristics=0x20000000):
            return decomp_binary.ImageMetadata(
                file_size=len(image),
                image_base=0,
                header_size=0,
                sections=(
                    decomp_binary.Section(
                        ".text", 0x1000, virtual_size, 8, raw_size, characteristics
                    ),
                ),
            )

        with patch.object(oracle, "_read_bounded_bytes", return_value=image), patch.object(
            decomp_binary, "address_to_file_offset", return_value=8
        ):
            with patch.object(decomp_binary, "parse_image_metadata", return_value=metadata()):
                self.assertEqual(oracle._binary_code(path, 0x1000, 20), image[8:28])
            for invalid in (
                metadata(characteristics=0),
                metadata(virtual_size=19),
                metadata(raw_size=19),
            ):
                with self.subTest(section=invalid.sections[0]), patch.object(
                    decomp_binary, "parse_image_metadata", return_value=invalid
                ), self.assertRaises(oracle.OracleError):
                    oracle._binary_code(path, 0x1000, 20)

    def test_live_file_reads_enforce_size_caps_before_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oversized.bin"
            path.write_bytes(b"12345")
            with self.assertRaises(oracle.OracleError):
                oracle._file_hash(path, max_size=4)
            with patch.object(oracle, "MAX_MAP_BYTES", 4), self.assertRaises(
                oracle.OracleError
            ):
                oracle._function_map_name(path, 0x004B0740)
            with patch.object(
                oracle, "MAX_EXECUTABLE_BYTES", 4
            ), self.assertRaises(oracle.OracleError):
                oracle._binary_code(path, 0x1000, 1)

    def test_scope_count_is_bounded(self):
        with self.assertRaisesRegex(oracle.OracleError, "too many"):
            oracle._normalize_scope(list(range(oracle.MAX_SCOPE_ADDRESSES + 1)))


class OracleReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.cache = self.root / "build" / "decomp-cache" / "oracle"

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, receipt: dict[str, object]) -> Path:
        return oracle.write_receipt(receipt, root=self.root)

    def write_unchecked(self, receipt: dict[str, object]) -> Path:
        self.cache.mkdir(parents=True, exist_ok=True)
        path = self.cache / f"{receipt['content_sha256']}.json"
        path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
        return path

    def test_pass_and_not_applicable_documents_are_explicit(self):
        passed = synthetic_receipt()
        not_applicable = synthetic_receipt(selected=False)
        self.assertEqual(passed["status"], "passed")
        self.assertEqual(not_applicable["status"], "not-applicable")
        self.assertEqual(not_applicable["inputs"]["selected_targets"], [])
        oracle.validate_document(passed, require_pass=True)
        oracle.validate_document(not_applicable, require_pass=True)

    def test_result_code_vector_and_opcode_tamper_fail_after_rehash(self):
        variants = []
        changed_result = synthetic_receipt()
        changed_result["targets"][0]["cases"][0]["current"]["return"] = 17
        variants.append(changed_result)
        changed_code = synthetic_receipt()
        current_code = changed_code["targets"][0]["current_code"]
        raw = bytearray.fromhex(current_code["bytes"])
        raw[5] = 17
        current_code["bytes"] = raw.hex()
        current_code["sha256"] = hashlib.sha256(raw).hexdigest()
        variants.append(changed_code)
        changed_vector = synthetic_receipt()
        changed_vector["targets"][0]["cases"][0]["expected"] = 17
        variants.append(changed_vector)
        changed_opcode = synthetic_receipt()
        changed_opcode["targets"][0]["current_code"]["bytes"] = (
            "cc" + RETAIL_CODE.hex()[2:]
        )
        changed_opcode["targets"][0]["current_code"]["sha256"] = hashlib.sha256(
            b"\xcc" + RETAIL_CODE[1:]
        ).hexdigest()
        variants.append(changed_opcode)
        changed_stack_base = synthetic_receipt()
        changed_stack_base["targets"][0]["limits"]["stack_base"] = "0x00130000"
        variants.append(changed_stack_base)
        changed_entry_esp = synthetic_receipt()
        changed_entry_esp["targets"][0]["limits"]["entry_esp"] = "0x00130100"
        variants.append(changed_entry_esp)
        for index, receipt in enumerate(variants):
            with self.subTest(index=index):
                receipt["content_sha256"] = oracle._document_hash(receipt)
                path = self.write_unchecked(receipt)
                with self.assertRaises(oracle.OracleError):
                    oracle.validate_receipt(path, root=self.root)

    def test_descriptor_paths_and_types_are_deeply_validated(self):
        mutations = (
            ("policy", "path", "copied-policy.json"),
            ("comparison_report", "size", 0),
            ("retail_image", "sha256", "bad"),
        )
        for name, field, value in mutations:
            with self.subTest(name=name, field=field):
                receipt = synthetic_receipt()
                receipt["inputs"][name][field] = value
                receipt["content_sha256"] = oracle._document_hash(receipt)
                with self.assertRaises(oracle.OracleError):
                    oracle.validate_document(receipt)

    def test_receipt_embedded_policy_groups_are_exact(self):
        def retail_code(value):
            raw = bytearray.fromhex(value["targets"][0]["retail_code"]["bytes"])
            raw[0] = 0xCC
            value["targets"][0]["retail_code"]["bytes"] = raw.hex()
            value["targets"][0]["retail_code"]["sha256"] = hashlib.sha256(
                raw
            ).hexdigest()

        mutations = {
            "address": lambda value: value["targets"][0].__setitem__(
                "address", "0x004B0750"
            ),
            "name": lambda value: value["targets"][0].__setitem__(
                "name", "Changed::Name"
            ),
            "map-name": lambda value: value["targets"][0].__setitem__(
                "function_map_name", "Changed::MapName"
            ),
            "backend": lambda value: value["targets"][0].__setitem__(
                "backend", "changed-backend"
            ),
            "abi": lambda value: value["targets"][0]["abi"].__setitem__(
                "stack_delta", 8
            ),
            "retail-code": retail_code,
            "instruction-limit": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("max_instructions", 129),
            "branch-limit": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("max_branches", 65),
            "stack-base": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("stack_base", "0x00130000"),
            "stack-size": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("stack_size", 1024),
            "entry-esp": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("entry_esp", "0x00130100"),
            "coverage-policy": lambda value: value["targets"][0][
                "limits"
            ].__setitem__("require_full_code_coverage", False),
            "case-id": lambda value: value["targets"][0]["cases"][0].__setitem__(
                "id", "changed-case"
            ),
            "case-input": lambda value: value["targets"][0]["cases"][0].__setitem__(
                "input", -2147483647
            ),
            "case-expected": lambda value: value["targets"][0]["cases"][
                0
            ].__setitem__("expected", 17),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                receipt = synthetic_receipt()
                mutate(receipt)
                receipt["content_sha256"] = oracle._document_hash(receipt)
                with self.assertRaises(oracle.OracleError):
                    oracle.validate_document(receipt, require_pass=True)

    def test_integrity_only_validation_and_required_current_replay(self):
        original = synthetic_receipt()
        mutations = {
            "head": lambda value: value["inputs"]["repository"].__setitem__(
                "head", "1" * 40
            ),
            "worktree": lambda value: value["inputs"]["repository"].__setitem__(
                "source_worktree_sha256", "1" * 64
            ),
            "index": lambda value: value["inputs"]["repository"].__setitem__(
                "source_index_sha256", "1" * 64
            ),
            "policy": lambda value: value["inputs"]["policy"].__setitem__(
                "sha256", "1" * 64
            ),
            "tool": lambda value: value["inputs"]["tool"].__setitem__(
                "sha256", "1" * 64
            ),
            "map": lambda value: value["inputs"]["function_map"].__setitem__(
                "sha256", "1" * 64
            ),
            "report": lambda value: value["inputs"]["comparison_report"].__setitem__(
                "sha256", "1" * 64
            ),
            "report-sidecar": lambda value: value["inputs"][
                "comparison_provenance"
            ].__setitem__("sha256", "1" * 64),
            "retail-image": lambda value: value["inputs"]["retail_image"].__setitem__(
                "sha256", "1" * 64
            ),
            "current-image": lambda value: value["inputs"]["current_image"].__setitem__(
                "sha256", "1" * 64
            ),
            "symbols": lambda value: value["inputs"]["current_symbols"].__setitem__(
                "sha256", "1" * 64
            ),
            "snapshot": lambda value: value["inputs"]["support_tools"][
                "snapshot"
            ].__setitem__("sha256", "1" * 64),
            "provenance": lambda value: value["inputs"]["support_tools"][
                "provenance"
            ].__setitem__("sha256", "1" * 64),
            "runtime-version": lambda value: value["inputs"][
                "python_runtime"
            ].__setitem__("version", [9, 9, 9]),
            "runtime-executable": lambda value: value["inputs"]["python_runtime"][
                "executable"
            ].__setitem__("sha256", "1" * 64),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                tampered = copy.deepcopy(original)
                mutate(tampered)
                tampered["content_sha256"] = oracle._document_hash(tampered)
                path = self.write(tampered)
                self.assertEqual(
                    oracle.validate_receipt(path, root=self.root), tampered
                )
                with patch.object(oracle, "build_receipt", return_value=original):
                    with self.assertRaisesRegex(oracle.OracleError, "current inputs"):
                        oracle.validate_receipt(
                            path, root=self.root, require_pass=True
                        )

    def test_current_replay_accepts_only_an_exact_rebuild(self):
        receipt = synthetic_receipt()
        path = self.write(receipt)
        with patch.object(oracle, "build_receipt", return_value=receipt) as rebuild:
            validated = oracle.validate_receipt(
                path,
                root=self.root,
                current=True,
                require_pass=True,
                expected_scope=["0x004B0740"],
            )
        self.assertEqual(validated, receipt)
        rebuild.assert_called_once_with(["0x004B0740"], root=self.root)

    def test_require_pass_rejects_a_deeply_valid_failed_receipt(self):
        receipt = synthetic_receipt(current_code=b"\xcc" + RETAIL_CODE[1:])
        self.assertEqual(receipt["status"], "failed")
        path = self.write(receipt)
        oracle.validate_receipt(path, root=self.root)
        with self.assertRaisesRegex(oracle.OracleError, "did not pass"):
            oracle.validate_receipt(path, root=self.root, require_pass=True)

    def test_receipt_requires_exact_cache_path_and_rejects_symlinks(self):
        receipt = synthetic_receipt(selected=False)
        path = self.write(receipt)
        outside = self.root / "build" / "decomp-cache" / "oracle-copy.json"
        outside.write_bytes(path.read_bytes())
        with (
            patch.object(
                oracle,
                "_read_json",
                side_effect=AssertionError("noncanonical receipt read"),
            ),
            self.assertRaisesRegex(oracle.OracleError, "not canonical"),
        ):
            oracle.validate_receipt(outside, root=self.root)
        link = self.cache / "link.json"
        link.symlink_to(path)
        with self.assertRaisesRegex(oracle.OracleError, "symbolic link"):
            oracle.validate_receipt(link, root=self.root)

        alias = self.root / "oracle-alias"
        alias.symlink_to(self.cache, target_is_directory=True)
        alias_path = alias / path.name
        with (
            patch.object(
                oracle,
                "_read_json",
                side_effect=AssertionError("symlinked receipt read"),
            ),
            self.assertRaisesRegex(oracle.OracleError, "symbolic link"),
        ):
            oracle.validate_receipt(alias_path, root=self.root)

    def test_cache_root_is_exact_and_rejects_a_symlink_component(self):
        receipt = synthetic_receipt(selected=False)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(oracle.OracleError, "decomp-cache/oracle"):
                oracle.write_receipt(
                    receipt,
                    root=self.root,
                    cache_root=Path(directory),
                )
        linked_root = self.root / "linked-repository"
        target = self.root / "real-repository"
        target.mkdir()
        linked_root.symlink_to(target, target_is_directory=True)
        with self.assertRaisesRegex(oracle.OracleError, "symbolic link"):
            oracle.write_receipt(receipt, root=linked_root)

    def test_required_input_descriptor_rejects_a_symlink_component(self):
        real = self.root / "real"
        real.mkdir()
        (real / "input.bin").write_bytes(b"input")
        alias = self.root / "alias"
        alias.symlink_to(real, target_is_directory=True)
        with self.assertRaisesRegex(oracle.OracleError, "symbolic link"):
            oracle._descriptor(self.root, alias / "input.bin", max_size=16)

    def test_cache_collision_is_rejected(self):
        receipt = synthetic_receipt(selected=False)
        self.cache.mkdir(parents=True)
        collision = self.cache / f"{receipt['content_sha256']}.json"
        collision.write_text("{}\n", encoding="utf-8")
        with self.assertRaisesRegex(oracle.OracleError, "different content"):
            self.write(receipt)

    def test_oversized_existing_cache_collision_is_bounded(self):
        receipt = synthetic_receipt(selected=False)
        content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
        limit = len(content.encode("utf-8")) + 1
        self.cache.mkdir(parents=True)
        collision = self.cache / f"{receipt['content_sha256']}.json"
        with collision.open("wb") as stream:
            stream.truncate(limit + 1)
        with (
            patch.object(oracle, "MAX_RECEIPT_BYTES", limit),
            patch.object(
                Path,
                "read_text",
                side_effect=AssertionError("unbounded cache read"),
            ),
        ):
            with self.assertRaisesRegex(oracle.OracleError, "different content"):
                self.write(receipt)

    def test_oversized_racing_cache_collision_is_bounded(self):
        receipt = synthetic_receipt(selected=False)
        content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
        limit = len(content.encode("utf-8")) + 1
        self.cache.mkdir(parents=True)
        collision = self.cache / f"{receipt['content_sha256']}.json"

        def collide(_source: Path, destination: Path) -> None:
            with Path(destination).open("wb") as stream:
                stream.truncate(limit + 1)
            raise FileExistsError

        with (
            patch.object(oracle, "MAX_RECEIPT_BYTES", limit),
            patch.object(oracle.os, "link", side_effect=collide),
            patch.object(
                Path,
                "read_text",
                side_effect=AssertionError("unbounded cache read"),
            ),
        ):
            with self.assertRaisesRegex(oracle.OracleError, "different content"):
                self.write(receipt)

    def test_root_relative_default_cache_is_used(self):
        receipt = synthetic_receipt(selected=False)
        path = oracle.write_receipt(receipt, root=self.root)
        self.assertEqual(path.parent, self.cache)

    def test_serialized_write_and_read_sizes_are_bounded(self):
        receipt = synthetic_receipt()
        pretty_size = len(
            (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")
        )
        with patch.object(oracle, "MAX_RECEIPT_BYTES", pretty_size - 1):
            with self.assertRaisesRegex(oracle.OracleError, "too large"):
                self.write(receipt)
        path = self.write(receipt)
        with patch.object(oracle, "MAX_RECEIPT_BYTES", path.stat().st_size - 1):
            with self.assertRaisesRegex(oracle.OracleError, "size"):
                oracle.validate_receipt(path, root=self.root)


class OracleWrapperTests(unittest.TestCase):
    def test_bash_wrapper_dispatches_list_and_help_without_build_setup(self):
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        with tempfile.TemporaryDirectory() as directory:
            listed = subprocess.run(
                [str(ROOT / "tools/decomp"), "oracle", "list", "--json"],
                cwd=directory,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(listed.returncode, 0, listed.stderr)
        payload = json.loads(listed.stdout)
        self.assertEqual(payload[0]["address"], "0x004B0740")
        with tempfile.TemporaryDirectory() as directory:
            helped = subprocess.run(
                [str(ROOT / "tools/decomp"), "oracle", "--help"],
                cwd=directory,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(helped.returncode, 0, helped.stderr)
        self.assertIn("run", helped.stdout)
        self.assertIn("verify", helped.stdout)

    def test_module_and_windows_wrapper_expose_the_same_command(self):
        helped = subprocess.run(
            [sys.executable, "-m", "tools.decomp_oracle", "--help"],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(helped.returncode, 0, helped.stderr)
        windows = (ROOT / "tools/decomp.ps1").read_text(encoding="utf-8")
        match = re.search(
            r'if \(\$Command -eq "oracle"\) \{(?P<body>.*?)\n\}',
            windows,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(match)
        block = match.group("body")
        self.assertIn("Set-Location $Root", block)
        self.assertIn("& $VenvPython -m tools.decomp_oracle @CommandArgs", block)
        self.assertIn("exit $LASTEXITCODE", block)
        self.assertNotIn("New-Item", block)
        self.assertNotIn("VC6", block)
        self.assertNotIn("Ghidra", block)

    def test_report_runtime_identity_does_not_depend_on_path_setup(self):
        from tools import decomp_provenance

        with patch.object(decomp_provenance.shutil, "which", return_value=None):
            identity = decomp_provenance._reccmp_runtime_identity(ROOT)
        executable_root = (
            ROOT
            / ".tooling"
            / "venv"
            / ("Scripts" if os.name == "nt" else "bin")
        )
        for name in ("reccmp-project", "reccmp-reccmp"):
            candidates = (
                (executable_root / f"{name}.exe", executable_root / name)
                if os.name == "nt"
                else (executable_root / name,)
            )
            expected = next(path for path in candidates if path.is_file())
            self.assertEqual(
                identity["executables"][name]["path"], str(expected.resolve())
            )


@unittest.skipUnless(
    (ROOT / "original/toy2.exe").is_file()
    and (ROOT / "build/toy2.exe").is_file()
    and (ROOT / "build/decomp-current-report.json").is_file(),
    "The checked executable artifacts are not available.",
)
class OracleLiveArtifactTests(unittest.TestCase):
    def test_real_artifacts_pass_and_bind_exact_addresses(self):
        try:
            receipt = oracle.build_receipt(["0x004B0740"])
        except oracle.OracleError as error:
            if "provenance" in str(error) and "stale" in str(error):
                self.skipTest("The current comparison report is stale.")
            raise
        self.assertEqual(receipt["status"], "passed")
        target = receipt["targets"][0]
        self.assertEqual(target["retail_code"]["address"], "0x004B0740")
        self.assertEqual(target["current_code"]["address"], "0x00401E50")
        self.assertEqual(target["retail_code"]["bytes"], RETAIL_CODE.hex())
        self.assertEqual(target["current_code"]["bytes"], RETAIL_CODE.hex())


if __name__ == "__main__":
    unittest.main()
