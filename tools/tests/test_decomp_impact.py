import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

from tools import decomp_impact as impact
from tools.decomp_dependencies import DependencyGraph


def status(
    score: float,
    *,
    effective: bool = False,
    diff: object = None,
) -> dict[str, object]:
    return {
        "matching": score,
        "effective": effective,
        "exact": score == 1.0,
        "terminal": effective or score == 1.0,
        "stub": False,
        "name": "Function",
        "diff_sha256": impact._diff_hash(diff),
    }


class ImpactComparisonTests(unittest.TestCase):
    def test_report_rejects_a_row_without_an_address_or_score(self):
        for row in ({"matching": 0.5}, {"address": "0x00401000"}):
            with self.subTest(row=row), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "report.json"
                path.write_text(json_text({"data": [row]}), encoding="utf-8")

                with self.assertRaisesRegex(impact.ImpactError, "no address or score"):
                    impact.read_report(path)

    def test_report_does_not_classify_a_near_one_score_as_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(
                json_text(
                    {
                        "data": [
                            {
                                "address": "0x00401000",
                                "matching": 0.9999999999,
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            row = impact.read_report(path)[0x401000]

            self.assertFalse(row["exact"])
            self.assertFalse(row["terminal"])

    def test_oracle_reports_regression_and_equal_score_diff_change(self):
        baseline = {
            0x401000: status(0.75, diff=["old"]),
            0x402000: status(0.80, diff=["before"]),
            0x403000: status(1.0),
        }
        current = {
            0x401000: status(0.70, diff=["worse"]),
            0x402000: status(0.80, diff=["after"]),
            0x403000: status(1.0),
        }

        changes, oracle = impact.compare_reports(
            baseline, current, {0x402000}
        )

        self.assertFalse(oracle["passed"])
        self.assertEqual(oracle["regressions"], ["0x00401000"])
        self.assertEqual(
            oracle["equal_score_diff_changes"], ["0x00402000"]
        )
        self.assertEqual(
            [row["classification"] for row in changes],
            ["regressed", "equal-score-diff-changed"],
        )

    def test_terminal_loss_is_a_regression_at_an_equal_effective_score(self):
        baseline = {0x401000: status(0.90, effective=True)}
        current = {0x401000: status(1.0 - 1e-13)}

        _changes, oracle = impact.compare_reports(baseline, current, set())

        self.assertEqual(oracle["regressions"], ["0x00401000"])

    def test_function_to_stub_is_a_regression_at_the_same_score(self):
        before = status(0.75, diff=["same"])
        after = status(0.75, diff=["same"])
        after["stub"] = True

        changes, oracle = impact.compare_reports(
            {0x401000: before}, {0x401000: after}, set()
        )

        self.assertEqual(changes[0]["classification"], "regressed")
        self.assertEqual(oracle["regressions"], ["0x00401000"])
        self.assertFalse(oracle["passed"])


class ImpactScopeTests(unittest.TestCase):
    def setUp(self):
        self.graph = DependencyGraph(
            callees={
                0x402000: frozenset({0x401000}),
                0x403000: frozenset({0x402000}),
            },
            callers={
                0x401000: frozenset({0x402000}),
                0x402000: frozenset({0x403000}),
            },
            indirect_calls={},
            indirect_jumps={},
        )
        self.sources = {
            "src/Game/A.cpp": (
                '#include "Shared.h"\n'
                "// FUNCTION: TOY2 0x00401000\n"
            ),
            "src/Game/B.cpp": "// FUNCTION: TOY2 0x00402000\n",
            "src/Game/Shared.h": "struct Shared {};\n",
        }

    def test_translation_unit_scope_adds_transitive_callers_and_callees(self):
        scope = impact.compute_impact_scope(
            changed_paths=["src/Game/A.cpp"],
            source_texts=self.sources,
            baseline_source_texts=self.sources,
            all_functions={0x401000, 0x402000, 0x403000, 0x404000},
            targets={0x401000},
            graph=self.graph,
        )

        self.assertEqual(
            scope["functions"],
            [
                "0x00401000",
                "0x00402000",
                "0x00403000",
                "0x00404000",
            ],
        )
        self.assertEqual(scope["strategy"], "all-functions-fallback")
        self.assertEqual(
            [row["depth"] for row in scope["caller_closure"]], [1, 2]
        )
        self.assertEqual(
            scope["callee_context"],
            [
                {
                    "address": "0x00401000",
                    "direct_callees": [],
                    "indirect_calls": 0,
                    "indirect_jumps": 0,
                }
            ],
        )
        self.assertTrue(scope["context_complete"])

    def test_header_scope_finds_include_users(self):
        scope = impact.compute_impact_scope(
            changed_paths=["src/Game/Shared.h"],
            source_texts=self.sources,
            baseline_source_texts=self.sources,
            all_functions={0x401000, 0x402000, 0x403000},
            targets={0x401000},
            graph=self.graph,
        )

        self.assertEqual(
            scope["header_translation_units"], ["src/Game/A.cpp"]
        )
        self.assertEqual(scope["strategy"], "all-functions-fallback")

    def test_missing_graph_uses_all_function_fallback_and_records_gap(self):
        scope = impact.compute_impact_scope(
            changed_paths=["src/Game/A.cpp"],
            source_texts=self.sources,
            baseline_source_texts=self.sources,
            all_functions={0x401000, 0x402000, 0x403000, 0x404000},
            targets={0x401000},
            graph=None,
            graph_error="Capstone is unavailable.",
        )

        self.assertEqual(scope["strategy"], "all-functions-fallback")
        self.assertEqual(len(scope["functions"]), 4)
        self.assertFalse(scope["context_complete"])
        self.assertEqual(
            scope["gaps"][0]["code"], "retail-call-graph-unavailable"
        )

    def test_indirect_control_flow_uses_all_function_fallback(self):
        graph = DependencyGraph(
            callees={},
            callers={},
            indirect_calls={0x401000: 1},
            indirect_jumps={},
        )
        scope = impact.compute_impact_scope(
            changed_paths=["src/Game/A.cpp"],
            source_texts=self.sources,
            baseline_source_texts=self.sources,
            all_functions={0x401000, 0x402000},
            targets={0x401000},
            graph=graph,
        )

        self.assertEqual(scope["strategy"], "all-functions-fallback")
        self.assertEqual(
            scope["gaps"][0]["code"],
            "indirect-control-flow-in-impact-scope",
        )


class ImpactValidationTests(unittest.TestCase):
    def test_pivot_selects_active_briefs_in_active_target_order(self):
        retired = {"target": "0x00401000"}
        second = {"target": "0x00402000"}
        pivot = {"target": "0x00403000"}
        latest_pivot = {"target": "0x00403000", "generation": 2}

        self.assertEqual(
            impact._active_briefs(
                [retired, pivot, second, latest_pivot], [0x403000, 0x402000]
            ),
            [latest_pivot, second],
        )

    def test_direct_script_and_wrappers_expose_the_read_only_cli(self):
        root = Path(__file__).resolve().parents[2]
        environment = dict(os.environ)
        environment.pop("PYTHONPATH", None)
        direct = subprocess.run(
            [sys.executable, "tools/decomp_impact.py", "--help"],
            cwd=root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        linux = subprocess.run(
            ["tools/decomp", "impact", "--help"],
            cwd=root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(direct.returncode, 0, direct.stderr)
        self.assertEqual(linux.returncode, 0, linux.stderr)
        for output in (direct.stdout, linux.stdout):
            self.assertIn("template", output)
            self.assertIn("seal-review", output)
            self.assertIn("verify-review", output)
        windows = (root / "tools/decomp.ps1").read_text(encoding="utf-8")
        self.assertIn('"impact"', windows)
        self.assertIn("-m tools.decomp_impact", windows)

    def make_pack(self) -> dict[str, object]:
        zero_hash = "0" * 64
        source_text = "int target;\n"
        prior_source_text = "int prior_target;\n"
        source_hash = hashlib.sha256(source_text.encode()).hexdigest()
        prior_source_hash = hashlib.sha256(prior_source_text.encode()).hexdigest()
        prior_oid = impact._git_blob_oid(prior_source_text.encode(), 40)
        source_oid = impact._git_blob_oid(source_text.encode(), 40)
        before_index = {"src/Game.cpp": [f"100644 {prior_oid} 0"]}
        current_index = {"src/Game.cpp": [f"100644 {source_oid} 0"]}
        brief_document = {
            "roles": {
                "writer": {"id": "source-writer"},
                "scouts": [
                    {"id": "context-scout"},
                    {"id": "retail-scout"},
                ],
            }
        }
        brief_content_hash = impact._json_hash(brief_document)
        brief_document["content_sha256"] = brief_content_hash
        brief_text = json.dumps(brief_document)
        brief_hash = hashlib.sha256(brief_text.encode()).hexdigest()
        target_state = status(0.80, diff=["target"])
        changed_before = status(0.70, diff=["before"])
        changed_after = status(0.70, diff=["after"])
        pack: dict[str, object] = {
            "schema": 1,
            "kind": impact.IMPACT_KIND,
            "campaign_id": "campaign-1",
            "mode": "refinement",
            "lane": "production",
            "targets": ["0x00401000"],
            "writer_id": "source-writer",
            "scout_ids": ["context-scout", "retail-scout"],
            "bindings": {
                "campaign_head": "c" * 40,
                "baseline_report": {
                    "path": "/tmp/impact-baseline.json",
                    "sha256": zero_hash,
                    "provenance_sha256": zero_hash,
                },
                "current_report": {
                    "path": "/tmp/impact-current.json",
                    "sha256": zero_hash,
                    "provenance_sha256": zero_hash,
                },
                "baseline_data_report": {
                    "path": "/tmp/impact-baseline-data.json",
                    "sha256": zero_hash,
                    "provenance_sha256": zero_hash,
                },
                "current_data_report": {
                    "path": "/tmp/impact-current-data.json",
                    "sha256": zero_hash,
                    "provenance_sha256": zero_hash,
                },
                "function_map": {
                    "path": "/tmp/functions-map.txt",
                    "sha256": zero_hash,
                },
                "retail_image": {
                    "path": "/tmp/toy2.exe",
                    "sha256": zero_hash,
                },
                "campaign_repository_index_sha256": impact._json_hash(before_index),
                "repository_index_sha256": impact._json_hash(current_index),
                "changed_inputs": [
                    {
                        "path": "src/Game.cpp",
                        "before": {
                            "git_mode": "100644",
                            "git_oid": prior_oid,
                            "sha256": prior_source_hash,
                            "byte_count": len(prior_source_text.encode()),
                            "line_count": 1,
                        },
                        "current": {
                            "git_mode": "100644",
                            "git_oid": source_oid,
                            "sha256": source_hash,
                            "byte_count": len(source_text.encode()),
                            "line_count": 1,
                        },
                    }
                ],
                "tool": {
                    "path": str(Path(impact.__file__).resolve()),
                    "sha256": impact._file_hash(Path(impact.__file__).resolve()),
                },
                "dependencies_tool": {
                    "path": str(
                        Path(impact.decomp_dependencies.__file__).resolve()
                    ),
                    "sha256": impact._file_hash(
                        Path(impact.decomp_dependencies.__file__).resolve()
                    ),
                },
                "binary_tool": {
                    "path": str(Path(impact.decomp_binary.__file__).resolve()),
                    "sha256": impact._file_hash(
                        Path(impact.decomp_binary.__file__).resolve()
                    ),
                },
                "annotations_tool": {
                    "path": str(
                        Path(impact.__file__).with_name("decomp_annotations.py").resolve()
                    ),
                    "sha256": impact._file_hash(
                        Path(impact.__file__).with_name("decomp_annotations.py").resolve()
                    ),
                },
                "verify_tool": {
                    "path": str(
                        Path(impact.__file__).with_name("decomp_verify.py").resolve()
                    ),
                    "sha256": impact._file_hash(
                        Path(impact.__file__).with_name("decomp_verify.py").resolve()
                    ),
                },
                "decoder": impact.decoder_identity(),
                "briefs": [
                    {
                        "target": "0x00401000",
                        "path": "/tmp/brief.json",
                        "sha256": brief_hash,
                        "content_sha256": brief_content_hash,
                        "doctor_receipt": {
                            "path": "/tmp/doctor.json",
                            "sha256": zero_hash,
                        },
                        "scout_reports": [
                            {
                                "path": "/tmp/context-scout.json",
                                "sha256": zero_hash,
                            },
                            {
                                "path": "/tmp/retail-scout.json",
                                "sha256": zero_hash,
                            },
                        ],
                        "dwarf_input": {
                            "path": "/tmp/woc-dwarf.txt",
                            "sha256": zero_hash,
                        },
                    }
                ],
            },
            "scope": {
                "strategy": "all-functions-fallback",
                "changed_paths": ["src/Game.cpp"],
                "changed_translation_units": ["src/Game.cpp"],
                "changed_headers": [],
                "header_translation_units": [],
                "seed_functions": ["0x00401000"],
                "caller_closure": [],
                "callee_context": [],
                "functions": ["0x00401000", "0x00402000"],
                "all_function_count": 2,
                "coverage_complete": True,
                "context_complete": False,
                "gaps": [
                    {
                        "code": "retail-call-graph-unavailable",
                        "detail": "The retail call graph is unavailable.",
                    }
                ],
            },
            "comparison_changes": [
                {
                    "address": "0x00401000",
                    "target": True,
                    "classification": "unchanged",
                    "regression": False,
                    "improvement": False,
                    "equal_score_diff_changed": False,
                    "before": target_state,
                    "after": target_state,
                },
                {
                    "address": "0x00402000",
                    "target": False,
                    "classification": "equal-score-diff-changed",
                    "regression": False,
                    "improvement": False,
                    "equal_score_diff_changed": True,
                    "before": changed_before,
                    "after": changed_after,
                },
            ],
            "regression_oracle": {
                "complete": True,
                "baseline_function_count": 2,
                "current_function_count": 2,
                "checked_function_count": 2,
                "regressions": [],
                "passed": True,
                "equal_score_diff_changes": ["0x00402000"],
                "added_functions": [],
            },
            "typed_data_oracle": {
                "complete": True,
                "checked_groups": list(impact.TYPED_DATA_GROUPS),
                "problems": [],
                "passed": True,
            },
            "review_evidence": {
                "changed_inputs": [
                    {
                        "path": "src/Game.cpp",
                        "before": {
                            "git_mode": "100644",
                            "git_oid": prior_oid,
                            "sha256": prior_source_hash,
                            "byte_count": len(prior_source_text.encode()),
                            "line_count": 1,
                            "text": prior_source_text,
                        },
                        "current": {
                            "git_mode": "100644",
                            "git_oid": source_oid,
                            "sha256": source_hash,
                            "byte_count": len(source_text.encode()),
                            "line_count": 1,
                            "text": source_text,
                        },
                    }
                ],
                "briefs": [
                    {
                        "target": "0x00401000",
                        "path": "/tmp/brief.json",
                        "sha256": brief_hash,
                        "text": brief_text,
                    }
                ],
                "total_bytes": (
                    len(prior_source_text.encode())
                    + len(source_text.encode())
                    + len(brief_text.encode())
                ),
            },
            "promotion_checks": [
                {
                    "id": "target-output-contract",
                    "status": "pass",
                    "subjects": [],
                },
                {
                    "id": "impact-scope-coverage",
                    "status": "pass",
                    "subjects": [],
                },
                {
                    "id": "all-function-regression-oracle",
                    "status": "pass",
                    "subjects": [],
                },
                {
                    "id": "typed-data-regression-oracle",
                    "status": "pass",
                    "subjects": [],
                },
            ],
            "required_claims": [
                {"id": claim_id, "claim": claim}
                for claim_id, claim in impact.PROMOTION_CLAIMS.items()
            ],
            "required_refuter_checks": list(impact.REFUTER_CHECKS),
        }
        pack["content_sha256"] = impact._document_hash(pack)
        return pack

    def make_finalize(
        self, root: Path
    ) -> tuple[dict[str, object], dict[str, object], Path]:
        pack = self.make_pack()
        from tools.decomp_provenance import provenance_path

        tool_inputs = {
            "function_map": root / "tools/Resources/functions_map.txt",
            "retail_image": root / "original/toy2.exe",
        }
        for path in tool_inputs.values():
            path.parent.mkdir(parents=True, exist_ok=True)
        tool_inputs["function_map"].write_text(
            "0x00401000 Function\n0x00402000 Other\n", encoding="utf-8"
        )
        tool_inputs["retail_image"].write_bytes(b"retail")
        for field, path in tool_inputs.items():
            pack["bindings"][field] = {
                "path": str(path.resolve()),
                "sha256": impact._file_hash(path),
            }

        report_sources: dict[str, Path] = {}
        code_payloads = {
            "baseline": {
                "data": [
                    {
                        "address": "0x00401000",
                        "matching": 0.80,
                        "effective": False,
                        "stub": False,
                        "name": "Function",
                        "diff": ["target"],
                    },
                    {
                        "address": "0x00402000",
                        "matching": 0.70,
                        "effective": False,
                        "stub": False,
                        "name": "Function",
                        "diff": ["before"],
                    },
                ]
            },
            "current": {
                "data": [
                    {
                        "address": "0x00401000",
                        "matching": 0.80,
                        "effective": False,
                        "stub": False,
                        "name": "Function",
                        "diff": ["target"],
                    },
                    {
                        "address": "0x00402000",
                        "matching": 0.70,
                        "effective": False,
                        "stub": False,
                        "name": "Function",
                        "diff": ["after"],
                    },
                ]
            },
        }
        data_payload = {
            "variables": {
                "variable_count": 0,
                "scored_bytes": 0,
                "explained_bytes": 0,
                "variables": [],
            },
            "sections": {
                "scored_bytes": 0,
                "explained_bytes": 0,
                "sections": [],
            },
            "vtables": {
                "table_count": 0,
                "scored_bytes": 0,
                "explained_bytes": 0,
                "tables": [],
            },
            "imports": {
                "entry_count": 0,
                "matched_entries": 0,
                "score": 1.0,
                "entries": [],
            },
            "relocations": {
                "entry_count": 0,
                "mapped_entries": 0,
                "matched_entries": 0,
                "score": 1.0,
            },
            "debug": {
                "original_pdb": "C:/retail/toy2.pdb",
                "recompiled_pdb": "C:/build/toy2.pdb",
            },
        }
        for name in ("baseline", "current", "baseline-data", "current-data"):
            report = root / "build" / f"decomp-{name}-report.json"
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(
                json_text(
                    data_payload
                    if name.endswith("data")
                    else code_payloads[name]
                ),
                encoding="utf-8",
            )
            sidecar = provenance_path(report)
            sidecar.write_text(json_text({"name": name}), encoding="utf-8")
            report_sources[name] = report
            report_sources[f"{name}-provenance"] = sidecar
        for field, name in (
            ("baseline_report", "baseline"),
            ("current_report", "current"),
            ("baseline_data_report", "baseline-data"),
            ("current_data_report", "current-data"),
        ):
            pack["bindings"][field] = {
                "path": str(report_sources[name].resolve()),
                "sha256": impact._file_hash(report_sources[name]),
                "provenance_sha256": impact._file_hash(
                    report_sources[f"{name}-provenance"]
                ),
            }
        changed_input = pack["bindings"]["changed_inputs"][0]
        before_index = {
            "src/Game.cpp": [
                f"{changed_input['before']['git_mode']} "
                f"{changed_input['before']['git_oid']} 0"
            ]
        }
        current_index = {
            "src/Game.cpp": [
                f"{changed_input['current']['git_mode']} "
                f"{changed_input['current']['git_oid']} 0"
            ]
        }
        pack["scope"] = impact._canonical_receipt_scope(
            pack=pack,
            bindings=pack["bindings"],
            baseline_index=before_index,
            current_index=current_index,
            changed_paths=["src/Game.cpp"],
            all_functions={0x401000, 0x402000},
            targets={0x401000},
        )
        pack["content_sha256"] = impact._document_hash(pack)
        source = root / "build/decomp-cache/impact/campaign-1/pack.json"
        source.parent.mkdir(parents=True)
        source.write_text(json_text(pack), encoding="utf-8")
        digest = impact._file_hash(source)
        all_sources = [source, *report_sources.values()]
        immutable = []
        frozen = root / "cache/artifacts/sha256" / digest
        for artifact_source in all_sources:
            artifact_digest = impact._file_hash(artifact_source)
            artifact_frozen = root / "cache/artifacts/sha256" / artifact_digest
            artifact_frozen.parent.mkdir(parents=True, exist_ok=True)
            artifact_frozen.write_bytes(artifact_source.read_bytes())
            immutable.append(
                {
                    "path": str(artifact_frozen.resolve()),
                    "sha256": artifact_digest,
                    "source_path": str(artifact_source.resolve()),
                }
            )
        receipt = {
            "schema_version": 3,
            "receipt_version": 2,
            "record_type": "finalize-receipt",
            "status": "passed",
            "campaign_id": "campaign-1",
            "mode": "refinement",
            "lane": "production",
            "receipt_key": "0" * 64,
            "key_payload": {
                "impact_review_required": True,
                "mode": "refinement",
                "lane": "production",
                "active_addresses": ["0x00401000"],
                "campaign_head": "c" * 40,
                "briefs": copy.deepcopy(pack["bindings"]["briefs"]),
                "baseline_report_sha256": pack["bindings"]["baseline_report"]["sha256"],
                "baseline_report_provenance_sha256": pack["bindings"]["baseline_report"]["provenance_sha256"],
                "baseline_data_report_sha256": pack["bindings"]["baseline_data_report"]["sha256"],
                "baseline_data_report_provenance_sha256": pack["bindings"]["baseline_data_report"]["provenance_sha256"],
                "campaign_repository_index_snapshot": before_index,
                "repository_index_snapshot": current_index,
                "repository_index_sha256": impact._json_hash(current_index),
                "inputs": {
                    "decoder": copy.deepcopy(pack["bindings"]["decoder"]),
                    "files": {
                        pack["bindings"][field]["path"]: pack["bindings"][field]["sha256"]
                        for field in (
                            "function_map",
                            "retail_image",
                            "tool",
                            "dependencies_tool",
                            "binary_tool",
                            "annotations_tool",
                            "verify_tool",
                        )
                    }
                },
            },
            "step_results": {
                "code_report": {
                    "artifacts": [
                        {
                            "path": str(report_sources["current"].resolve()),
                            "sha256": impact._file_hash(report_sources["current"]),
                        },
                        {
                            "path": str(report_sources["current-provenance"].resolve()),
                            "sha256": impact._file_hash(
                                report_sources["current-provenance"]
                            ),
                        },
                    ]
                },
                "data_report": {
                    "artifacts": [
                        {
                            "path": str(report_sources["current-data"].resolve()),
                            "sha256": impact._file_hash(report_sources["current-data"]),
                        },
                        {
                            "path": str(report_sources["current-data-provenance"].resolve()),
                            "sha256": impact._file_hash(
                                report_sources["current-data-provenance"]
                            ),
                        },
                    ]
                },
                "source_scan": {
                    "artifacts": [
                        {"path": str(source.resolve()), "sha256": digest},
                        *[
                            {
                                "path": str(report_sources[name].resolve()),
                                "sha256": impact._file_hash(report_sources[name]),
                            }
                            for name in ("baseline", "baseline-provenance", "baseline-data", "baseline-data-provenance")
                        ],
                    ]
                }
            },
            "immutable_artifacts": immutable,
        }
        receipt["receipt_key"] = impact._json_hash(receipt["key_payload"])
        receipt["receipt_sha256"] = impact._json_hash(receipt)
        return receipt, pack, frozen

    def replace_impact_pack(
        self,
        receipt: dict[str, object],
        pack: dict[str, object],
    ) -> Path:
        pack["content_sha256"] = impact._document_hash(pack)
        source_scan = receipt["step_results"]["source_scan"]
        source_descriptor = source_scan["artifacts"][0]
        source = Path(source_descriptor["path"])
        source.write_text(json_text(pack), encoding="utf-8")
        digest = impact._file_hash(source)
        frozen = Path(receipt["immutable_artifacts"][0]["path"]).parent / digest
        frozen.write_bytes(source.read_bytes())
        source_descriptor["sha256"] = digest
        immutable = next(
            row
            for row in receipt["immutable_artifacts"]
            if row["source_path"] == str(source.resolve())
        )
        immutable["path"] = str(frozen.resolve())
        immutable["sha256"] = digest
        receipt["receipt_sha256"] = impact._json_hash(receipt)
        return frozen

    def test_pack_hash_tamper_is_rejected(self):
        pack = self.make_pack()
        impact.validate_impact_pack(pack)
        changed = copy.deepcopy(pack)
        changed["targets"] = ["0x00402000"]

        with self.assertRaisesRegex(impact.ImpactError, "content hash"):
            impact.validate_impact_pack(changed)

    def test_atomic_cache_publish_does_not_overwrite_a_racing_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cache/value.json"

            def racing_link(_temporary: Path, destination: Path) -> None:
                destination.write_text("other\n", encoding="utf-8")
                raise FileExistsError

            with mock.patch.object(impact.os, "link", side_effect=racing_link):
                with self.assertRaisesRegex(impact.ImpactError, "collision"):
                    impact._atomic_cache_write(
                        path,
                        "expected\n",
                        "cache collision",
                    )

            self.assertEqual(path.read_text(encoding="utf-8"), "other\n")

    def test_impact_cache_enforces_the_serialized_document_bound(self):
        def escaped_pack(blob_size: int) -> dict[str, object]:
            pack = self.make_pack()
            bindings = []
            evidence = []
            paths = []
            for index in range(8):
                path = f"src/{index}.cpp"
                paths.append(path)
                sides = []
                for text_value in ("\\" * blob_size, '"' * blob_size):
                    raw = text_value.encode("utf-8")
                    sides.append(
                        {
                            "git_mode": "100644",
                            "git_oid": impact._git_blob_oid(raw, 40),
                            "sha256": hashlib.sha256(raw).hexdigest(),
                            "byte_count": len(raw),
                            "line_count": len(raw.splitlines()),
                            "text": text_value,
                        }
                    )
                before, current = sides
                bindings.append(
                    {
                        "path": path,
                        "before": {
                            key: before[key]
                            for key in (
                                "git_mode",
                                "git_oid",
                                "sha256",
                                "byte_count",
                                "line_count",
                            )
                        },
                        "current": {
                            key: current[key]
                            for key in (
                                "git_mode",
                                "git_oid",
                                "sha256",
                                "byte_count",
                                "line_count",
                            )
                        },
                    }
                )
                evidence.append(
                    {"path": path, "before": before, "current": current}
                )
            pack["bindings"]["changed_inputs"] = bindings
            pack["review_evidence"]["changed_inputs"] = evidence
            brief_bytes = len(
                pack["review_evidence"]["briefs"][0]["text"].encode("utf-8")
            )
            pack["review_evidence"]["total_bytes"] = (
                brief_bytes + blob_size * 16
            )
            pack["scope"]["changed_paths"] = paths
            pack["scope"]["changed_translation_units"] = paths
            pack["content_sha256"] = impact._document_hash(pack)
            return pack

        within = escaped_pack(220_000)
        self.assertLess(
            len(impact._document_text(within).encode("utf-8")),
            impact.MAX_DOCUMENT_BYTES,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = impact.write_impact_pack(within, Path(directory))
            self.assertEqual(
                impact._read_json(path, "impact pack"),
                impact.validate_impact_pack(within),
            )

        brief_bytes = len(
            self.make_pack()["review_evidence"]["briefs"][0]["text"].encode(
                "utf-8"
            )
        )
        over = escaped_pack(
            (impact.MAX_EVIDENCE_BYTES - brief_bytes - 16) // 16
        )
        self.assertGreater(
            len(impact._document_text(over).encode("utf-8")),
            impact.MAX_DOCUMENT_BYTES,
        )
        with self.assertRaisesRegex(impact.ImpactError, "serialized impact pack"):
            impact.validate_impact_pack(over)

    def test_rehashed_pack_cannot_relabel_a_regression_as_an_improvement(self):
        pack = self.make_pack()
        change = pack["comparison_changes"][0]
        change["before"] = status(0.90, diff=["before"])
        change["after"] = status(0.80, diff=["after"])
        change["classification"] = "improved"
        change["improvement"] = True
        pack["content_sha256"] = impact._document_hash(pack)

        with self.assertRaisesRegex(impact.ImpactError, "incorrect classification"):
            impact.validate_impact_pack(pack)

    def test_rehashed_pack_cannot_substitute_changed_input_text(self):
        pack = self.make_pack()
        content = pack["review_evidence"]["changed_inputs"][0]["current"]
        binding = pack["bindings"]["changed_inputs"][0]["current"]
        content["text"] = "int substituted;\n"
        raw = content["text"].encode()
        for row in (content, binding):
            row["sha256"] = hashlib.sha256(raw).hexdigest()
            row["byte_count"] = len(raw)
            row["line_count"] = len(raw.splitlines())
        pack["review_evidence"]["total_bytes"] += (
            len(raw) - len("int target;\n".encode())
        )
        pack["content_sha256"] = impact._document_hash(pack)

        with self.assertRaisesRegex(impact.ImpactError, "Git object"):
            impact.validate_impact_pack(pack)

    def test_receipt_binding_rejects_forged_oracles_and_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, pack, _frozen = self.make_finalize(root)
            changed = pack["comparison_changes"][1]
            changed["after"] = copy.deepcopy(changed["before"])
            changed["classification"] = "unchanged"
            changed["equal_score_diff_changed"] = False
            pack["regression_oracle"]["equal_score_diff_changes"] = []
            self.replace_impact_pack(finalize, pack)

            with self.assertRaisesRegex(impact.ImpactError, "immutable reports"):
                impact.impact_artifact(finalize)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, pack, _frozen = self.make_finalize(root)
            pack["bindings"]["tool"]["sha256"] = "f" * 64
            self.replace_impact_pack(finalize, pack)

            with self.assertRaisesRegex(impact.ImpactError, "tool binding"):
                impact.impact_artifact(finalize)

    def test_receipt_binding_rejects_changed_dependency_and_decoder(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            dependency_path = Path(impact.decomp_dependencies.__file__).resolve()
            real_hash = impact._file_hash

            def changed_dependency(path):
                if Path(path).resolve() == dependency_path:
                    return "f" * 64
                return real_hash(Path(path))

            with (
                mock.patch.object(
                    impact, "_file_hash", side_effect=changed_dependency
                ),
                self.assertRaisesRegex(
                    impact.ImpactError, "dependencies tool changed"
                ),
            ):
                impact.impact_artifact(finalize)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, pack, _frozen = self.make_finalize(root)
            decoder = copy.deepcopy(pack["bindings"]["decoder"])
            decoder["version"] = "changed-decoder"
            pack["bindings"]["decoder"] = decoder
            finalize["key_payload"]["inputs"]["decoder"] = copy.deepcopy(decoder)
            finalize["receipt_key"] = impact._json_hash(finalize["key_payload"])
            self.replace_impact_pack(finalize, pack)

            with self.assertRaisesRegex(impact.ImpactError, "decoder identity changed"):
                impact.impact_artifact(finalize)

    def test_receipt_binding_rejects_wrong_target_index_and_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            finalize["key_payload"]["active_addresses"] = ["0x00402000"]
            finalize["receipt_key"] = impact._json_hash(finalize["key_payload"])
            finalize["receipt_sha256"] = impact._json_hash(finalize)
            with self.assertRaisesRegex(impact.ImpactError, "finalization scope"):
                impact.impact_artifact(finalize)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            current_index = finalize["key_payload"]["repository_index_snapshot"]
            current_index["src/Other.cpp"] = [f"100644 {'3' * 40} 0"]
            finalize["key_payload"]["repository_index_sha256"] = impact._json_hash(
                current_index
            )
            finalize["receipt_key"] = impact._json_hash(finalize["key_payload"])
            finalize["receipt_sha256"] = impact._json_hash(finalize)
            with self.assertRaisesRegex(impact.ImpactError, "repository index"):
                impact.impact_artifact(finalize)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, pack, _frozen = self.make_finalize(root)
            pack["bindings"]["current_report"]["sha256"] = "f" * 64
            self.replace_impact_pack(finalize, pack)
            with self.assertRaisesRegex(impact.ImpactError, "report binding"):
                impact.impact_artifact(finalize)

    def test_receipt_binding_rejects_rehashed_scope_omissions(self):
        def erase_gaps(scope):
            scope.update(
                gaps=[],
                context_complete=True,
                callee_context=[
                    {
                        "address": "0x00401000",
                        "direct_callees": [],
                        "indirect_calls": 0,
                        "indirect_jumps": 0,
                    }
                ],
            )

        def claim_graph_available(scope):
            scope.update(
                gaps=[
                    gap
                    for gap in scope["gaps"]
                    if gap["code"] != "retail-call-graph-unavailable"
                ],
                callee_context=[
                    {
                        "address": "0x00401000",
                        "direct_callees": [],
                        "indirect_calls": 0,
                        "indirect_jumps": 0,
                    }
                ],
            )

        mutations = (
            (
                "function subset",
                lambda scope: scope.update(
                    functions=["0x00401000"], all_function_count=1
                ),
            ),
            (
                "strategy",
                lambda scope: scope.update(
                    strategy="translation-unit-and-caller-closure"
                ),
            ),
            ("omitted path", lambda scope: scope.update(changed_paths=[])),
            (
                "extra path",
                lambda scope: scope.update(
                    changed_paths=["src/Game.cpp", "src/Other.cpp"]
                ),
            ),
            ("erased gaps", erase_gaps),
            (
                "context state",
                lambda scope: scope.update(context_complete=True),
            ),
            (
                "seed functions",
                lambda scope: scope.update(
                    seed_functions=["0x00401000", "0x00402000"]
                ),
            ),
            (
                "caller closure",
                lambda scope: scope.update(
                    caller_closure=[
                        {
                            "address": "0x00402000",
                            "calls": "0x00401000",
                            "depth": 1,
                        }
                    ]
                ),
            ),
            ("graph availability", claim_graph_available),
            (
                "translation units",
                lambda scope: scope.update(changed_translation_units=[]),
            ),
            (
                "headers and include users",
                lambda scope: scope.update(
                    changed_headers=["src/Game.cpp"],
                    header_translation_units=["src/Game.cpp"],
                ),
            ),
            (
                "coverage state",
                lambda scope: scope.update(coverage_complete=False),
            ),
        )
        for label, mutate in mutations:
            with self.subTest(mutation=label), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                finalize, pack, _frozen = self.make_finalize(root)
                mutate(pack["scope"])
                self.replace_impact_pack(finalize, pack)

                with self.assertRaisesRegex(
                    impact.ImpactError, "impact (scope|context)"
                ):
                    impact.impact_artifact(finalize)

    def test_receipt_binding_rejects_a_substituted_brief(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, pack, _frozen = self.make_finalize(root)
            replacement = str((root / "other-brief.json").resolve())
            pack["bindings"]["briefs"][0]["path"] = replacement
            pack["review_evidence"]["briefs"][0]["path"] = replacement
            self.replace_impact_pack(finalize, pack)

            with self.assertRaisesRegex(impact.ImpactError, "brief binding"):
                impact.impact_artifact(finalize)

    def test_review_rejects_a_changed_frozen_baseline_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            baseline_source = str(
                (root / "build/decomp-baseline-report.json").resolve()
            )
            frozen = next(
                Path(row["path"])
                for row in finalize["immutable_artifacts"]
                if row["source_path"] == baseline_source
            )
            frozen.write_text("{}\n", encoding="utf-8")

            with self.assertRaisesRegex(impact.ImpactError, "changed"):
                impact.make_review_template(finalize, "acceptance-reviewer")

    def test_staged_and_head_source_readers_use_distinct_git_trees(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(
                ["git", "config", "user.name", "Impact Tests"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.email", "impact@example.invalid"],
                cwd=root,
                check=True,
            )
            source = root / "src/Game.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("int old_value;\n", encoding="utf-8")
            subprocess.run(["git", "add", "src/Game.cpp"], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "-qm", "Baseline"], cwd=root, check=True
            )
            source.write_text("int staged_value;\n", encoding="utf-8")
            subprocess.run(["git", "add", "src/Game.cpp"], cwd=root, check=True)

            self.assertEqual(
                impact.baseline_source_texts(root)["src/Game.cpp"],
                "int old_value;\n",
            )
            self.assertEqual(
                impact.staged_source_texts(root)["src/Game.cpp"],
                "int staged_value;\n",
            )

    def test_changed_input_evidence_covers_add_delete_and_build_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            subprocess.run(
                ["git", "config", "user.name", "Impact Tests"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.email", "impact@example.invalid"],
                cwd=root,
                check=True,
            )
            (root / "src").mkdir()
            (root / "src/Old.cpp").write_text("int old;\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "add_executable(game src/Old.cpp)\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "-A"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-qm", "Baseline"], cwd=root, check=True)
            baseline_index = impact._repository_index_snapshot(root)
            (root / "src/Old.cpp").unlink()
            (root / "src/New.asm").write_text("ret\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "add_executable(game src/New.asm)\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", "-A"], cwd=root, check=True)
            current_index = impact._repository_index_snapshot(root)

            bindings, evidence, _total = impact._changed_input_evidence(
                root,
                impact.staged_paths(root),
                baseline_index,
                current_index,
            )

            self.assertEqual(
                [row["path"] for row in bindings],
                ["CMakeLists.txt", "src/New.asm", "src/Old.cpp"],
            )
            self.assertIsNone(evidence[1]["before"])
            self.assertIsNone(evidence[2]["current"])

    def test_review_receipt_binds_all_claims_and_refuter_subjects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            report = impact.make_review_template(finalize, "acceptance-reviewer")
            accept_template(report)
            report["content_sha256"] = impact._document_hash(report)
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")

            receipt, identity = impact.create_review_receipt(
                finalize_receipt=finalize,
                review_report_path=report_path,
                cache_root=root / "reviews",
            )

            self.assertEqual(
                receipt["accepted_claims"], list(impact.PROMOTION_CLAIMS)
            )
            self.assertEqual(
                receipt["review_report"]["refuter_checks"][1]["subjects"],
                ["0x00402000"],
            )
            self.assertEqual(
                impact.validate_review_identity(
                    identity,
                    finalize_receipt=finalize,
                    cache_root=root / "reviews",
                ),
                receipt,
            )

    def test_review_rejects_writer_or_scout_as_reviewer(self):
        for reviewer_id in ("source-writer", "retail-scout"):
            with self.subTest(reviewer_id=reviewer_id), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                finalize, _pack, _frozen = self.make_finalize(root)
                with self.assertRaisesRegex(
                    impact.ImpactError, "matches a writer or scout"
                ):
                    impact.make_review_template(finalize, reviewer_id)

    def test_pending_review_template_cannot_be_promoted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            report = impact.make_review_template(finalize, "acceptance-reviewer")
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")

            with self.assertRaisesRegex(impact.ImpactError, "did not accept"):
                impact.create_review_receipt(
                    finalize_receipt=finalize,
                    review_report_path=report_path,
                    cache_root=root / "reviews",
                )

    def test_review_rejects_an_unbound_evidence_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            report = impact.make_review_template(finalize, "acceptance-reviewer")
            accept_template(report)
            report["claims"][0]["evidence"][0]["source"] = str(
                (root / "invented.txt").resolve()
            )
            report["content_sha256"] = impact._document_hash(report)

            with self.assertRaisesRegex(impact.ImpactError, "unbound evidence"):
                impact.validate_review_report(
                    report,
                    finalize_receipt=finalize,
                    pack=impact.impact_artifact(finalize)[0],
                    artifact=impact.impact_artifact(finalize)[1],
                )

            report = impact.make_review_template(
                finalize, "acceptance-reviewer"
            )
            accept_template(report)
            report["claims"][0]["evidence"][0]["locator"] = "invented.row"
            report["content_sha256"] = impact._document_hash(report)
            pack, artifact = impact.impact_artifact(finalize)
            with self.assertRaisesRegex(impact.ImpactError, "unbound evidence locator"):
                impact.validate_review_report(
                    report,
                    finalize_receipt=finalize,
                    pack=pack,
                    artifact=artifact,
                )

    def test_direct_loader_rejects_a_receipt_key_not_bound_to_its_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            finalize["receipt_key"] = "f" * 64
            finalize["receipt_sha256"] = impact._json_hash(finalize)
            path = root / "finalize.json"
            path.write_text(json_text(finalize), encoding="utf-8")

            with self.assertRaisesRegex(impact.ImpactError, "invalid"):
                impact._load_finalize_receipt(path)

    def test_review_identity_rejects_a_noncanonical_cache_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            report = impact.make_review_template(finalize, "acceptance-reviewer")
            accept_template(report)
            report["content_sha256"] = impact._document_hash(report)
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")
            _receipt, identity = impact.create_review_receipt(
                finalize_receipt=finalize,
                review_report_path=report_path,
                cache_root=root / "reviews",
            )
            copied = root / "copied-review.json"
            copied.write_bytes(Path(identity["path"]).read_bytes())
            identity["path"] = str(copied.resolve())
            identity["file_sha256"] = impact._file_hash(copied)

            with self.assertRaisesRegex(impact.ImpactError, "not canonical"):
                impact.validate_review_identity(
                    identity,
                    finalize_receipt=finalize,
                    cache_root=root / "reviews",
                )

    def test_read_only_cli_prints_a_pending_template_and_verifies_review(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            finalize, _pack, _frozen = self.make_finalize(root)
            finalize_path = root / "finalize.json"
            finalize_path.write_text(json_text(finalize), encoding="utf-8")
            output = StringIO()
            with redirect_stdout(output):
                status_code = impact.main(
                    [
                        "template",
                        "--finalize-receipt",
                        str(finalize_path),
                        "--reviewer-id",
                        "acceptance-reviewer",
                        "--json",
                    ]
                )
            self.assertEqual(status_code, 0)
            report = json.loads(output.getvalue())
            self.assertEqual(report["decision"], "pending")
            self.assertTrue(all(row["status"] == "pending" for row in report["claims"]))

            accept_template(report)
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")
            output = StringIO()
            with redirect_stdout(output):
                status_code = impact.main(
                    [
                        "seal-review",
                        "--finalize-receipt",
                        str(finalize_path),
                        "--review-report",
                        str(report_path),
                        "--json",
                    ]
                )
            self.assertEqual(status_code, 0)
            report_path.write_text(output.getvalue(), encoding="utf-8")
            output = StringIO()
            with redirect_stdout(output):
                status_code = impact.main(
                    [
                        "verify-review",
                        "--finalize-receipt",
                        str(finalize_path),
                        "--review-report",
                        str(report_path),
                        "--json",
                    ]
                )
            self.assertEqual(status_code, 0)
            self.assertTrue(json.loads(output.getvalue())["ok"])


def json_text(value: object) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def accept_template(report: dict[str, object]) -> None:
    report["decision"] = "accepted"
    for row in report["claims"]:
        row["status"] = "accepted"
    for row in report["refuter_checks"]:
        row["status"] = "passed"


if __name__ == "__main__":
    unittest.main()
