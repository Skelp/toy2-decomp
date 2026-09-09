import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[1] / "decomp_verify.py"
SPEC = importlib.util.spec_from_file_location("decomp_verify", SCRIPT)
VERIFY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VERIFY)


class VerifyRegressionTests(unittest.TestCase):
    def write_build_context(
        self,
        root,
        *,
        source="main.cpp",
        definitions="/DWIN32",
        includes="-Isrc",
        flags="/O2",
        compiler="cl",
        link_flags="/debug",
    ):
        build = Path(root) / "build"
        rules = build / "CMakeFiles"
        rules.mkdir(parents=True, exist_ok=True)
        (rules / "rules.ninja").write_text(
            "rule CXX_COMPILER__app\n"
            f"  command = {compiler} $DEFINES $INCLUDES $FLAGS /Fo$out -c $in\n"
            "  description = Compile $out\n"
            "rule CXX_EXECUTABLE_LINKER__app\n"
            "  command = link $in /out:$TARGET_FILE $LINK_FLAGS $LINK_LIBRARIES\n",
            encoding="utf-8",
        )
        (build / "build.ninja").write_text(
            f"build main.obj: CXX_COMPILER__app {source}\n"
            f"  DEFINES = {definitions}\n"
            f"  INCLUDES = {includes}\n"
            f"  FLAGS = {flags}\n"
            "build app.exe: CXX_EXECUTABLE_LINKER__app main.obj\n"
            f"  LINK_FLAGS = {link_flags}\n"
            "  LINK_LIBRARIES = user32.lib\n"
            "  TARGET_FILE = app.exe\n",
            encoding="utf-8",
        )
        return build

    def write_report(self, directory, name, rows):
        path = Path(directory) / name
        path.write_text(json.dumps({"data": rows}), encoding="utf-8")
        return path

    def test_experiment_record_adds_taxonomy_without_changing_structural_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.write_report(
                directory,
                "report.json",
                [
                    {
                        "address": "0x00401000",
                        "matching": 0.6,
                        "diff": [
                            [
                                "@@ -0x401000,1 +0x501000,2 @@",
                                [
                                    {
                                        "orig": [["0x401000", "call Original"]],
                                        "recomp": [["0x501000", "call Recompiled"]],
                                    },
                                    {
                                        "orig": [],
                                        "recomp": [["0x501005", "push eax"]],
                                    },
                                ],
                            ]
                        ],
                    }
                ],
            )
            record = VERIFY.experiment_record(report, 0x401000)
        self.assertEqual(
            record["structural_changes"],
            {
                "row_count_groups": 1,
                "opcode_rows": 0,
                "register_rows": 0,
                "stack_or_frame_rows": 0,
                "control_flow_rows": 1,
            },
        )
        self.assertEqual(len(record["normalized_diff"]), 1)
        self.assertEqual(record["mismatch_taxonomy"]["schema_version"], 1)
        self.assertEqual(
            record["mismatch_taxonomy"]["row_changes"],
            {"replace": 1, "insert": 1, "delete": 0},
        )

    def write_data_report(
        self,
        directory,
        name,
        rows,
        *,
        unscored=None,
        section_explained=None,
    ):
        explained = sum(item.get("matched_bytes", 0) for item in rows)
        scored = sum(item.get("size", 0) for item in rows)
        section_explained = (
            explained if section_explained is None else section_explained
        )
        payload = {
            "variables": {
                "variables": rows,
                "unscored_variables": unscored or [],
                "explained_bytes": explained,
                "scored_bytes": scored,
            },
            "sections": {
                "sections": [
                    {
                        "name": ".data",
                        "size": 100,
                        "explained_bytes": section_explained,
                        "score": section_explained / 100,
                    }
                ]
            },
            "vtables": {"explained_bytes": 0},
            "imports": {"matched_entries": 5},
            "relocations": {"matched_entries": 0},
        }
        path = Path(directory) / name
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def validate(
        self,
        baseline,
        current,
        targets,
        allow=False,
        source_root=None,
        mode="coverage",
        metadata=None,
        baseline_data=None,
        current_data=None,
        accounting_correction=None,
        staged=False,
        lint_change=None,
    ):
        old_artifacts = VERIFY.TOOL_ARTIFACTS
        VERIFY.TOOL_ARTIFACTS = baseline.parent / "none.tsv"
        try:
            with mock.patch.object(VERIFY, "validate_metadata", return_value=[]), mock.patch.object(
                VERIFY,
                "read_lint_debt_change",
                return_value=lint_change or VERIFY.LintDebtChange(),
            ):
                return VERIFY.validate(
                    baseline,
                    current,
                    set(targets),
                    allow,
                    metadata_path=metadata,
                    source_root=source_root or baseline.parent / "src",
                    check_annotation_tags=False,
                    mode=mode,
                    meta_resolution=allow or accounting_correction is not None,
                    baseline_data_path=baseline_data,
                    current_data_path=current_data,
                    accounting_correction=accounting_correction,
                    staged=staged,
                )
        finally:
            VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_rejects_an_untouched_score_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8},
                {"address": "0x402000", "matching": 0.8},
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.9},
                {"address": "0x402000", "matching": 0.7},
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 1)

    def test_target_regression_requires_the_explicit_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.7}
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 1)
            self.assertEqual(self.validate(baseline, current, {0x401000}, True), 0)

    def test_coverage_rejects_49_percent_and_accepts_50_percent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.0, "stub": True}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.49}
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 1)
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.5}
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 0)

    def test_target_source_debt_still_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
                "void Test(char* value) { *(int*)((char*)value + 4) = 1; }\n",
                encoding="utf-8",
            )
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.2}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.4}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, source_root=source), 1
            )

    def test_baseline_metadata_rejects_a_changed_report(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.write_report(Path(directory), "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            metadata = Path(directory) / "metadata.json"
            VERIFY.write_metadata(metadata, report)
            report.write_text('{"data": []}', encoding="utf-8")
            self.assertIn(
                "baseline report does not match its saved metadata",
                VERIFY.validate_metadata(metadata, report),
            )

    def test_baseline_metadata_rejects_a_changed_data_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self.write_report(root, "before.json", [])
            data_report = self.write_data_report(root, "before-data.json", [])
            metadata = root / "metadata.json"
            VERIFY.write_metadata(metadata, report, data_report)
            data_report.write_text("{}", encoding="utf-8")
            self.assertIn(
                "baseline data report does not match its saved metadata",
                VERIFY.validate_metadata(metadata, report, data_report),
            )

    def test_build_context_allows_a_source_list_only_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = self.write_build_context(root)
            cmake = root / "CMakeLists.txt"
            cmake.write_text("add_executable(app main.cpp)\n", encoding="utf-8")
            before = VERIFY.normalized_build_context(build)
            cmake.write_text(
                "add_executable(app main.cpp Initializer.inc)\n", encoding="utf-8"
            )
            self.assertEqual(before, VERIFY.normalized_build_context(build))

    def test_metadata_uses_generated_context_instead_of_cmake_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self.write_report(root, "before.json", [])
            common = {
                "build_context_sha256": "same-generated-context",
                "compiler_driver_sha256": "compiler",
                "compiler_backend_sha256": "backend",
                "sdk_headers_sha256": "sdk",
                "vc6_headers_sha256": "vc6",
                "reccmp_git_head": "reccmp",
            }
            metadata = root / "metadata.json"
            metadata.write_text(
                json.dumps(
                    {
                        **common,
                        "git_head": "baseline",
                        "baseline_report_sha256": VERIFY.file_hash(report),
                        "cmake_flags_sha256": "old-cmake-text",
                    }
                ),
                encoding="utf-8",
            )
            current = {
                **common,
                "git_head": "current",
                "cmake_flags_sha256": "new-cmake-text",
            }
            ancestor = mock.Mock(returncode=0)
            with mock.patch.object(VERIFY, "metadata", return_value=current), mock.patch.object(
                VERIFY.subprocess, "run", return_value=ancestor
            ):
                self.assertEqual(VERIFY.validate_metadata(metadata, report), [])

            current["build_context_sha256"] = "changed-generated-context"
            with mock.patch.object(VERIFY, "metadata", return_value=current), mock.patch.object(
                VERIFY.subprocess, "run", return_value=ancestor
            ):
                self.assertIn(
                    "baseline build_context_sha256 does not match the current build context",
                    VERIFY.validate_metadata(metadata, report),
                )

    def test_build_context_rejects_compile_context_changes(self):
        changes = {
            "source compile unit": {"source": "other.cpp"},
            "compiler definitions": {"definitions": "/DDEBUG"},
            "compiler include paths": {"includes": "-Iother"},
            "compiler flags": {"flags": "/Od"},
            "compiler toolchain": {"compiler": "clang-cl"},
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = VERIFY.normalized_build_context(self.write_build_context(root))
            for name, arguments in changes.items():
                with self.subTest(name=name):
                    build = self.write_build_context(root, **arguments)
                    self.assertNotEqual(before, VERIFY.normalized_build_context(build))

    def test_build_context_rejects_link_context_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = VERIFY.normalized_build_context(self.write_build_context(root))
            build = self.write_build_context(root, link_flags="/incremental:no")
            self.assertNotEqual(before, VERIFY.normalized_build_context(build))

    def test_annotation_tag_becomes_stale_after_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "src"
            root.mkdir()
            (root / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = self.write_report(Path(directory), "report.json", [
                {"address": "0x401000", "matching": 0.7}
            ])
            self.assertTrue(any(
                "requires provisional" in item
                for item in VERIFY.check_annotations(report, root)
            ))

    def test_session_summary_reports_new_targets_and_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.4, "stub": True},
                {"address": "0x402000", "matching": 0.8},
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 1.0},
                {"address": "0x402000", "matching": 0.9},
            ])
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = VERIFY.session_summary(
                    baseline, current, [0x401000, 0x402000]
                )
            self.assertEqual(result, 0)
            self.assertIn("2 (1 new, 1 exact)", output.getvalue())

    def test_refinement_requires_a_strict_increase(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.6}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.6}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="refinement"), 1
            )
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.6001}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="refinement"), 0
            )

    def test_non_function_lint_debt_removal_counts_as_refinement_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            change = VERIFY.LintDebtChange(removed_errors=5)
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    mode="refinement",
                    staged=True,
                    lint_change=change,
                ),
                0,
            )

    def test_new_lint_debt_blocks_refinement_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            change = VERIFY.LintDebtChange(removed_errors=1, new_errors=1)
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    mode="refinement",
                    staged=True,
                    lint_change=change,
                ),
                1,
            )

    def test_lint_debt_removal_does_not_hide_untouched_function_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8},
                {"address": "0x402000", "matching": 0.8},
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.8},
                {"address": "0x402000", "matching": 0.7},
            ])
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    mode="refinement",
                    staged=True,
                    lint_change=VERIFY.LintDebtChange(removed_errors=1),
                ),
                1,
            )

    def test_lint_debt_change_requires_source_and_baseline_repairs(self):
        finding = VERIFY.decomp_lint.Finding(
            Path("src/first.cpp"),
            1,
            "repeated-private-type",
            "error",
            "struct SharedType { int value; };",
            "type repeats",
            subject="SharedType",
            fingerprint="abc123",
        )
        owner, rule, subject, fingerprint = finding.baseline_key
        entry = VERIFY.decomp_lint.BaselineEntry(
            owner, rule, subject, fingerprint, "src/first.cpp"
        )

        removed = VERIFY.classify_lint_debt_change([finding], [entry], [], [])
        self.assertEqual(removed, VERIFY.LintDebtChange(removed_errors=1))

        row_only = VERIFY.classify_lint_debt_change(
            [finding], [entry], [finding], []
        )
        self.assertEqual(row_only.removed_errors, 0)
        self.assertEqual(row_only.new_errors, 1)

        source_only = VERIFY.classify_lint_debt_change(
            [finding], [entry], [], [entry]
        )
        self.assertEqual(source_only.removed_errors, 0)
        self.assertEqual(source_only.stale_rows, 1)

    def test_refinement_below_50_must_cross_the_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.2}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.49}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="refinement"), 1
            )

    def test_refinement_promotion_to_effective_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.8, "effective": True}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="refinement"), 0
            )

    def test_source_debt_removal_requires_terminal_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            metadata = root / "meta.json"
            metadata.write_text(json.dumps({
                "implemented_addresses": [0x401000],
                "source_debt": {str(0x401000): ["raw-layout-access"]},
            }), encoding="utf-8")
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 1.0}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.9, "effective": True}
            ])
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    source_root=source,
                    mode="refinement",
                    metadata=metadata,
                ),
                0,
            )
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.9}
            ])
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    source_root=source,
                    mode="refinement",
                    metadata=metadata,
                ),
                1,
            )

    def test_newly_integrated_function_must_meet_50_percent_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid First() {}\n"
                "// FUNCTION: TOY2 0x00402000 [PROVISIONAL]\nvoid Second() {}\n",
                encoding="utf-8",
            )
            metadata = root / "meta.json"
            metadata.write_text(json.dumps({
                "implemented_addresses": [0x401000],
                "source_debt": {},
            }), encoding="utf-8")
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.6}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.7},
                {"address": "0x402000", "matching": 0.49},
            ])
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    source_root=source,
                    mode="refinement",
                    metadata=metadata,
                ),
                1,
            )

    def test_coverage_rejects_an_implemented_loss_hidden_by_two_additions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid Target() {}\n"
                "// FUNCTION: TOY2 0x00402000 [PROVISIONAL]\nvoid Extra() {}\n"
                "// LIBRARY: TOY2 0x00403000\nvoid Lost() {}\n",
                encoding="utf-8",
            )
            metadata = root / "meta.json"
            metadata.write_text(
                json.dumps(
                    {
                        "implemented_addresses": [0x403000],
                        "source_debt": {},
                    }
                ),
                encoding="utf-8",
            )
            baseline = self.write_report(
                root,
                "before.json",
                [
                    {"address": "0x401000", "matching": 0.0, "stub": True},
                    {"address": "0x402000", "matching": 0.0, "stub": True},
                    {"address": "0x403000", "matching": 0.8},
                ],
            )
            current = self.write_report(
                root,
                "after.json",
                [
                    {"address": "0x401000", "matching": 0.6},
                    {"address": "0x402000", "matching": 0.6},
                    {"address": "0x403000", "matching": 0.8},
                ],
            )

            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x401000},
                    source_root=source,
                    mode="coverage",
                    metadata=metadata,
                ),
                1,
            )

    def test_data_campaign_accepts_improved_typed_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "data.cpp").write_text("int value = 2;\n", encoding="utf-8")
            metadata = root / "meta.json"
            metadata.write_text(
                json.dumps(
                    {
                        "implemented_addresses": [],
                        "source_debt": {},
                        "source_dependency_sha256": "baseline-source",
                    }
                ),
                encoding="utf-8",
            )
            baseline = self.write_report(root, "before.json", [])
            current = self.write_report(root, "after.json", [])
            baseline_data = self.write_data_report(
                root,
                "before-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 0,
                        "score": 0.0,
                    }
                ],
            )
            current_data = self.write_data_report(
                root,
                "after-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    }
                ],
            )
            self.assertEqual(
                self.validate(
                    baseline,
                    current,
                    {0x501000},
                    source_root=source,
                    mode="data",
                    metadata=metadata,
                    baseline_data=baseline_data,
                    current_data=current_data,
                ),
                0,
            )

    def test_data_campaign_rejects_unchanged_and_bss_targets(self):
        before = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 2,
                        "score": 0.5,
                    }
                ],
                "unscored_variables": [
                    {
                        "original_address": 0x502000,
                        "reason": "bss_only",
                    }
                ],
                "explained_bytes": 2,
            }
        }
        problems = VERIFY.validate_data_campaign(
            before, before, {0x501000, 0x502000}, None
        )
        self.assertTrue(any("did not improve" in item for item in problems))
        self.assertTrue(any("BSS-only" in item for item in problems))

    def test_data_campaign_rejects_unknown_and_unscored_targets(self):
        payload = {
            "variables": {
                "variables": [],
                "unscored_variables": [
                    {
                        "original_address": 0x502000,
                        "reason": "unknown_size",
                    }
                ],
                "explained_bytes": 0,
            }
        }
        problems = VERIFY.validate_data_campaign(
            payload, payload, {0x501000, 0x502000}, None
        )
        self.assertTrue(any("is unknown" in item for item in problems))
        self.assertTrue(any("no scored type size" in item for item in problems))

    def test_accounting_correction_accepts_one_removed_and_two_added_targets(self):
        before = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x4E0588,
                        "size": 4,
                        "matched_bytes": 2,
                        "score": 0.5,
                    }
                ],
                "explained_bytes": 2,
            },
            "sections": {
                "sections": [
                    {"name": ".data", "explained_bytes": 2, "score": 0.02}
                ]
            },
        }
        after = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x4E0374,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x4E058C,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                ],
                "explained_bytes": 8,
            },
            "sections": {
                "sections": [
                    {"name": ".data", "explained_bytes": 8, "score": 0.08}
                ]
            },
        }
        self.assertEqual(
            VERIFY.validate_data_campaign(
                before,
                after,
                {0x4E0374, 0x4E0588, 0x4E058C},
                "Correct the typed range.",
            ),
            [],
        )

    def test_accounting_correction_rejects_a_target_absent_from_both_reports(self):
        payload = {
            "variables": {"variables": [], "explained_bytes": 0},
        }
        problems = VERIFY.validate_data_campaign(
            payload,
            payload,
            {0x501000},
            "Correct the typed range.",
        )
        self.assertTrue(any("absent from both reports" in item for item in problems))

    def test_normal_data_campaign_rejects_range_replacement(self):
        before = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 2,
                        "score": 0.5,
                    }
                ],
                "explained_bytes": 2,
            }
        }
        after = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x502000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x503000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                ],
                "explained_bytes": 8,
            }
        }
        problems = VERIFY.validate_data_campaign(
            before,
            after,
            {0x501000, 0x502000, 0x503000},
            None,
        )
        self.assertTrue(any("is unknown" in item for item in problems))
        self.assertTrue(
            any("not scored in the current report" in item for item in problems)
        )

    def test_accounting_correction_does_not_hide_unrelated_regressions(self):
        before = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 2,
                        "score": 0.5,
                    },
                    {
                        "original_address": 0x504000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                ],
                "explained_bytes": 6,
            }
        }
        after = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x502000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x503000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x504000,
                        "size": 4,
                        "matched_bytes": 3,
                        "score": 0.75,
                    },
                ],
                "explained_bytes": 11,
            }
        }
        problems = VERIFY.validate_data_campaign(
            before,
            after,
            {0x501000, 0x502000, 0x503000},
            "Correct the typed range.",
        )
        self.assertTrue(
            any("unrelated data bytes regressed" in item for item in problems)
        )

    def test_accounting_correction_requires_selected_target_improvement(self):
        before = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x501000,
                        "size": 8,
                        "matched_bytes": 8,
                        "score": 1.0,
                    }
                ],
                "explained_bytes": 8,
            }
        }
        after = {
            "variables": {
                "variables": [
                    {
                        "original_address": 0x502000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x503000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                ],
                "explained_bytes": 8,
            }
        }
        problems = VERIFY.validate_data_campaign(
            before,
            after,
            {0x501000, 0x502000, 0x503000},
            "Correct the typed range.",
        )
        self.assertTrue(
            any("did not improve explained bytes" in item for item in problems)
        )

    def test_data_campaign_rejects_unrelated_and_section_regressions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_data_report(
                root,
                "before-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 0,
                        "score": 0.0,
                    },
                    {
                        "original_address": 0x502000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                ],
                section_explained=10,
            )
            current = self.write_data_report(
                root,
                "after-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x502000,
                        "size": 4,
                        "matched_bytes": 0,
                        "score": 0.0,
                    },
                ],
                section_explained=8,
            )
            problems = VERIFY.validate_data_campaign(
                VERIFY.read_data_report(baseline),
                VERIFY.read_data_report(current),
                {0x501000},
                None,
            )
            self.assertTrue(
                any("unrelated data bytes regressed" in item for item in problems)
            )
            self.assertTrue(
                any("section bytes regressed" in item for item in problems)
            )

    def test_data_campaign_rejects_unchanged_source_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            metadata = root / "meta.json"
            metadata.write_text(
                json.dumps(
                    {
                        "implemented_addresses": [],
                        "source_debt": {},
                        "source_dependency_sha256": VERIFY.tree_hash(source),
                    }
                ),
                encoding="utf-8",
            )
            report = self.write_report(root, "functions.json", [])
            baseline_data = self.write_data_report(
                root,
                "before-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 0,
                        "score": 0.0,
                    }
                ],
            )
            current_data = self.write_data_report(
                root,
                "after-data.json",
                [
                    {
                        "original_address": 0x501000,
                        "size": 4,
                        "matched_bytes": 4,
                        "score": 1.0,
                    }
                ],
            )
            self.assertEqual(
                self.validate(
                    report,
                    report,
                    {0x501000},
                    source_root=source,
                    mode="data",
                    metadata=metadata,
                    baseline_data=baseline_data,
                    current_data=current_data,
                ),
                1,
            )


if __name__ == "__main__":
    unittest.main()
