import contextlib
import importlib.util
import io
import json
import subprocess
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

    def structure_setup(self, directory, *, current_rows=None, current_variables=None, same_tree=False):
        root = Path(directory)
        source = root / "src"
        source.mkdir()
        (source / "Ini.cpp").write_text(
            "// FUNCTION: TOY2 0x00401000\nvoid Parse(int* value) { *value = 7; }\n", encoding="utf-8"
        )
        rows = [{"address": "0x401000", "matching": 0.5}, {"address": "0x401100", "matching": 1.0}]
        variables = [{"original_address": 0x500000, "size": 4, "matched_bytes": 4, "score": 1.0}]
        baseline = self.write_report(root, "baseline.json", rows)
        current = self.write_report(root, "current.json", current_rows or rows)
        baseline_data = self.write_data_report(root, "baseline-data.json", variables)
        current_data = self.write_data_report(root, "current-data.json", current_variables or variables)
        payload = json.loads(current_data.read_text(encoding="utf-8"))
        payload["relocations"]["matched_entries"] = -27
        current_data.write_text(json.dumps(payload), encoding="utf-8")
        metadata = root / "meta.json"
        metadata.write_text(json.dumps({
            "source_dependency_sha256": VERIFY.tree_hash(source) if same_tree else "before the move",
            "implemented_addresses": [0x401000], "source_debt": {},
            "file_evidence": {"whole_file_bytes": 1000.0, "pe_header_bytes": 3729.0},
        }), encoding="utf-8")
        return dict(baseline=baseline, current=current, targets=[], source_root=source, mode="structure",
                    metadata=metadata, baseline_data=baseline_data, current_data=current_data)

    def validate_structure(self, arguments):
        stderr = io.StringIO()
        evidence = {"whole_file_bytes": 926.53, "pe_header_bytes": 3732.0}
        with mock.patch.object(VERIFY, "file_evidence", return_value=evidence), \
                contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(io.StringIO()):
            code = self.validate(**arguments)
        return code, stderr.getvalue()

    def test_structure_move_passes_with_file_evidence_deltas_as_warnings(self):
        with tempfile.TemporaryDirectory() as directory:
            code, stderr = self.validate_structure(self.structure_setup(directory))
        self.assertEqual(code, 0, stderr)
        self.assertIn("warning: structure: relocations matched_entries 0.00 -> -27.00 (-27.00)", stderr)
        self.assertIn("warning: structure: whole-file evidence 1,000.00 -> 926.53 (-73.47)", stderr)
        self.assertIn("warning: structure: PE header bytes 3,729.00 -> 3,732.00 (+3.00)", stderr)

    def test_missing_file_evidence_is_named_instead_of_read_as_unchanged(self):
        measured = {"whole_file_bytes": 926.53, "pe_header_bytes": 3732.0}
        warnings = VERIFY.structure_warnings({}, {}, {}, measured)
        self.assertIn("file evidence unavailable: the baseline numbers are missing", warnings[-1])
        warnings = VERIFY.structure_warnings({}, {}, measured, {})
        self.assertIn("file evidence unavailable: the current numbers are missing", warnings[-1])
        self.assertEqual(VERIFY.structure_warnings({}, {}, measured, measured), [])

    def test_structure_rejects_any_score_or_variable_change_and_an_unchanged_tree(self):
        changed_score = [{"address": "0x401000", "matching": 0.51}, {"address": "0x401100", "matching": 1.0}]
        changed_variable = [{"original_address": 0x500000, "size": 4, "matched_bytes": 2, "score": 0.5}]
        cases = (
            (dict(current_rows=changed_score), "0x00401000: score changed in a structure campaign"),
            (dict(current_variables=changed_variable), "0x00500000: variable changed in a structure campaign"),
            (dict(same_tree=True), "structure campaign did not change the source tree"),
        )
        for options, problem in cases:
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as directory:
                code, stderr = self.validate_structure(self.structure_setup(directory, **options))
                self.assertEqual(code, 1)
                self.assertIn(problem, stderr)
        with tempfile.TemporaryDirectory() as directory:
            arguments = self.structure_setup(directory)
            arguments["targets"] = [0x401000]
            code, stderr = self.validate_structure(arguments)
        self.assertEqual(code, 1)
        self.assertIn("a structure campaign cannot have target addresses", stderr)

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

    def test_large_coverage_body_is_accepted_provisionally_below_50_percent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.0, "stub": True}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.37}
            ])
            with mock.patch.object(
                VERIFY, "provisional_coverage_ok", return_value=(True, "2270 retail bytes")
            ):
                self.assertEqual(self.validate(baseline, current, {0x401000}), 0)
            with mock.patch.object(
                VERIFY, "provisional_coverage_ok", return_value=(False, "300 retail bytes")
            ):
                self.assertEqual(self.validate(baseline, current, {0x401000}), 1)

    def test_provisional_coverage_needs_a_large_body_and_a_quarter_of_the_ceiling(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            functions_map = root / "functions_map.txt"
            functions_map.write_text(
                "0x00401000 Big\n0x00402000 Small\n0x00402100 Next\n", encoding="utf-8"
            )
            sizes = root / "sizes.json"
            sizes.write_text(
                json.dumps([
                    {"address": "00401000", "size": 4000},
                    {"address": "00402000", "size": 256},
                ]),
                encoding="utf-8",
            )
            accepted, why = VERIFY.provisional_coverage_ok(0x401000, 0.30, functions_map, sizes)
            self.assertTrue(accepted, why)
            self.assertIn("4000 retail bytes", why)
            # 4000 of a 4096-byte gap: the ceiling lifts 0.24 raw above 0.25.
            self.assertTrue(VERIFY.provisional_coverage_ok(0x401000, 0.245, functions_map, sizes)[0])
            self.assertFalse(VERIFY.provisional_coverage_ok(0x401000, 0.20, functions_map, sizes)[0])
            self.assertFalse(VERIFY.provisional_coverage_ok(0x402000, 0.40, functions_map, sizes)[0])

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

    def test_build_context_allows_a_new_translation_unit_with_the_usual_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            build = self.write_build_context(directory)
            before = VERIFY.normalized_build_context(build)
            ninja = build / "build.ninja"
            text = ninja.read_text(encoding="utf-8").replace(
                "CXX_EXECUTABLE_LINKER__app main.obj\n", "CXX_EXECUTABLE_LINKER__app main.obj ini.obj\n"
            )
            unit = "build ini.obj: CXX_COMPILER__app ini.cpp\n  DEFINES = /DWIN32\n  INCLUDES = -Isrc\n"
            ninja.write_text(unit + "  FLAGS = /O2\n" + text, encoding="utf-8")
            self.assertEqual(before, VERIFY.normalized_build_context(build))
            ninja.write_text(unit + "  FLAGS = /Od\n" + text, encoding="utf-8")
            self.assertNotEqual(before, VERIFY.normalized_build_context(build))

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

    def test_a_replaced_compile_unit_is_reported_and_an_added_one_is_not(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            saved = VERIFY.compile_units(self.write_build_context(root))
            self.assertEqual(saved, ["main.cpp"])
            replaced = VERIFY.compile_units(self.write_build_context(root, source="other.cpp"))
            self.assertEqual(
                VERIFY.compile_unit_problems(saved, replaced),
                ["baseline compile unit main.cpp is not in the current build"],
            )
            self.assertEqual(VERIFY.compile_unit_problems(saved, ["ini.cpp", "main.cpp"]), [])
            self.assertEqual(VERIFY.compile_unit_problems(None, replaced), [])
            # A structure campaign renames a unit or merges two: its exact score
            # comparison already covers a unit that goes.
            self.assertEqual(
                VERIFY.compile_unit_problems(saved, replaced, allow_removed=True), []
            )

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

    def test_refinement_below_50_accepts_a_strict_improvement(self):
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
                self.validate(baseline, current, {0x401000}, mode="refinement"), 0
            )
            # Coverage keeps the 50% floor for the same scores.
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="coverage"), 1
            )

    def test_refinement_below_50_rejects_flat_or_regressed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.2}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.2}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, mode="refinement"), 1
            )
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.3}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.25}
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

    def test_code_campaign_prints_typed_data_regressions_as_warnings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.0, "stub": True}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.9}
            ])
            baseline_data = self.write_data_report(root, "before-data.json", [
                {"original_address": 0x501000, "size": 4, "matched_bytes": 4, "score": 1.0}
            ])
            current_data = self.write_data_report(root, "after-data.json", [
                {"original_address": 0x501000, "size": 4, "matched_bytes": 0, "score": 0.0}
            ])
            for mode in ("coverage", "refinement"):
                with self.subTest(mode=mode):
                    errors = io.StringIO()
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                        result = self.validate(
                            baseline,
                            current,
                            {0x401000},
                            mode=mode,
                            baseline_data=baseline_data,
                            current_data=current_data,
                        )
                    self.assertEqual(result, 0)
                    self.assertIn(
                        "warning: 0x00501000: data bytes regressed", errors.getvalue()
                    )
                    self.assertIn(
                        "warning: .data: explained section bytes regressed",
                        errors.getvalue(),
                    )
                    self.assertNotIn("validation failed", errors.getvalue())

    def test_scoreboard_writes_shared_format(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid First() {}\n"
                "// FUNCTION: TOY2 0x00401010 [EFFECTIVE]\nint Second(int value) { return value + 1; }\n"
                "// FUNCTION: TOY2 0x00401040 [PROVISIONAL]\n"
                "void Third(char* value) { *(int*)((char*)value + 4) = 1; }\n"
                "// STUB: TOY2 0x00401080\nvoid Fourth() {}\n",
                encoding="utf-8",
            )
            functions_map = root / "functions_map.txt"
            functions_map.write_text(
                "0x00401000 First\n0x00401010 Second\n0x00401040 Third\n"
                "0x00401080 Fourth\n0x00401100 Fifth\n",
                encoding="utf-8",
            )
            sizes = root / "sizes.json"
            sizes.write_text(json.dumps({"data": [
                {"address": "0x00401000", "size": 12},
                {"address": "0x00401080", "size": 1},
                {"address": "0x00401100", "size": 32},
            ]}), encoding="utf-8")
            report = self.write_report(root, "report.json", [
                {"address": "0x401000", "matching": 1.0},
                {"address": "0x401010", "matching": 0.8, "effective": True},
                {"address": "0x401040", "matching": 0.25},
                {"address": "0x401080", "matching": 0.3, "stub": True},
            ])
            output = root / "Resources" / "scoreboard.tsv"
            with mock.patch.object(VERIFY, "reccmp_short_head", return_value="abc1234"):
                VERIFY.write_scoreboard(
                    report, output, functions_map, sizes, source, "report"
                )
            lines = output.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "# reccmp_head=abc1234 generated_by=report")
            self.assertEqual(lines[1], "address\tsize\tmatching\texact\teffective\tdebt")
            self.assertEqual(lines[2:], [
                "0x00401000\t12\t1.000000\t1\t0\t0",
                "0x00401010\t48\t1.000000\t0\t1\t0",
                "0x00401040\t64\t0.250000\t0\t0\t2",
                "0x00401080\t1\t0.000000\t0\t0\t0",
                "0x00401100\t32\t0.000000\t0\t0\t0",
            ])
            rows = [line.split("\t") for line in lines[2:]]
            effective_bytes = sum(int(row[1]) * float(row[2]) for row in rows)
            self.assertAlmostEqual(effective_bytes, 12 + 48 + 64 * 0.25)
            terminal = [
                row[0]
                for row in rows
                if (row[3] == "1" or row[4] == "1") and row[5] == "0"
            ]
            self.assertEqual(terminal, ["0x00401000", "0x00401010"])

            cli_output = root / "cli.tsv"
            argv = [
                "decomp_verify.py", "scoreboard", str(report), str(cli_output),
                "--functions-map", str(functions_map),
                "--function-sizes", str(sizes),
                "--source-root", str(source),
                "--generated-by", "validate",
            ]
            with mock.patch.object(VERIFY, "reccmp_short_head", return_value="unknown"), \
                 mock.patch.object(VERIFY.sys, "argv", argv):
                self.assertEqual(VERIFY.main(), 0)
            cli_lines = cli_output.read_text(encoding="utf-8").splitlines()
            self.assertEqual(cli_lines[0], "# reccmp_head=unknown generated_by=validate")
            self.assertEqual(cli_lines[1:], lines[1:])


class HeaderSideEffectTests(unittest.TestCase):
    """`score --changed CURRENT --baseline BASELINE --exclude TARGET` output."""

    NINJA_DEPS = (
        "CMakeFiles/toy2decomp.dir/src/A.cpp.obj: #deps 2, deps mtime 1 (VALID)\n"
        "    ../src/A.cpp\n"
        "    ../src/Numerics.h\n"
        "\n"
        "CMakeFiles/toy2decomp.dir/src/B.cpp.obj: deps not found\n"
        "\n"
        "CMakeFiles/patcher.dir/src/C.cpp.obj: #deps 2, deps mtime 1 (VALID)\n"
        "    Z:\\proj\\src\\C.cpp\n"
        "    Z:\\proj\\src\\Numerics.h\n"
    )

    def write_report(self, directory, name, rows):
        path = Path(directory) / name
        path.write_text(json.dumps({"data": rows}), encoding="utf-8")
        return path

    def run_score(self, baseline, current, exclude, headers, deps):
        argv = [
            "decomp_verify.py", "score", "--changed", str(current),
            "--baseline", str(baseline), "--exclude", exclude,
        ]
        with mock.patch.object(VERIFY, "dirty_headers", return_value=headers), \
             mock.patch.object(VERIFY, "ninja_deps", return_value=deps), \
             mock.patch.object(VERIFY.sys, "argv", argv), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            status = VERIFY.main()
        return status, output.getvalue().splitlines()

    def test_moved_untouched_functions_are_listed_with_the_dirty_headers(self):
        with tempfile.TemporaryDirectory() as root:
            baseline = self.write_report(root, "baseline.json", [
                {"address": "0x41c190", "matching": 0.8916},
                {"address": "0x41c640", "matching": 0.5},
                {"address": "0x41c700", "matching": 1.0},
                {"address": "0x41c800", "matching": 0.7, "effective": True},
            ])
            current = self.write_report(root, "current.json", [
                {"address": "0x41c190", "matching": 0.8075},
                {"address": "0x41c640", "matching": 0.9},
                {"address": "0x41c700", "matching": 1.0},
                {"address": "0x41c800", "matching": 0.7},
                {"address": "0x41c900", "matching": 0.4},
            ])
            deps = VERIFY.parse_ninja_deps(self.NINJA_DEPS)
            status, lines = self.run_score(
                baseline, current, "0x41c640", ["src/Numerics.h", "src/Toy2/Levels.h"], deps
            )
        self.assertEqual(status, 0)
        self.assertEqual(lines, [
            "header side effect: 0x0041C190 89.16% -> 80.75%",
            "header side effect: 0x0041C800 100.00% -> 70.00%",
            "header side effect: 2 untouched function(s) moved; dirty headers: "
            "src/Numerics.h (2 translation units), src/Toy2/Levels.h (0 translation units)",
        ])

    def test_nothing_is_printed_below_the_display_resolution(self):
        with tempfile.TemporaryDirectory() as root:
            baseline = self.write_report(root, "baseline.json", [
                {"address": "0x41c190", "matching": 0.8916},
                {"address": "0x41c640", "matching": 0.5},
            ])
            current = self.write_report(root, "current.json", [
                {"address": "0x41c190", "matching": 0.89164},
                {"address": "0x41c640", "matching": 0.2},
            ])
            status, lines = self.run_score(baseline, current, "0x41c640", ["src/X.h"], None)
        self.assertEqual((status, lines), (0, []))

    def test_translation_unit_counts_fall_back_without_ninja(self):
        deps = VERIFY.parse_ninja_deps(self.NINJA_DEPS)
        self.assertEqual(sorted(deps), [
            "CMakeFiles/patcher.dir/src/C.cpp.obj", "CMakeFiles/toy2decomp.dir/src/A.cpp.obj",
        ])
        self.assertEqual(VERIFY.translation_units_including("src/Numerics.h", deps), "2")
        self.assertEqual(VERIFY.translation_units_including("src/C.cpp", deps), "1")
        self.assertEqual(VERIFY.translation_units_including("src/Numerics.h", None), "?")
        with tempfile.TemporaryDirectory() as root:
            baseline = self.write_report(root, "baseline.json", [
                {"address": "0x41c190", "matching": 0.5},
            ])
            current = self.write_report(root, "current.json", [
                {"address": "0x41c190", "matching": 0.6},
            ])
            status, lines = self.run_score(baseline, current, "0x41c640", ["src/X.h"], None)
        self.assertEqual(lines, [
            "header side effect: 0x0041C190 50.00% -> 60.00%",
            "header side effect: 1 untouched function(s) moved; dirty headers: "
            "src/X.h (? translation units)",
        ])

    def test_score_still_needs_a_report_and_addresses_without_changed(self):
        argv = ["decomp_verify.py", "score", "--baseline", "x.json"]
        with mock.patch.object(VERIFY.sys, "argv", argv), \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                VERIFY.main()


def git(root, *args):
    subprocess.run(["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@t", *args],
                   check=True, capture_output=True)


class ReportStampTests(unittest.TestCase):
    """finish and baseline reuse the validate reports only while nothing they describe changed."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name, text in (("src/a.cpp", "int a;\n"), ("tools/Resources/functions_map.txt", "0x1 A\n"),
                           ("build/toy2.exe", "exe"), ("build/toy2.pdb", "pdb"), ("build/patcher.dll", "dll"),
                           ("reccmp-project.yml", "targets: {}\n"), ("build/r.json", "{}"), ("build/d.json", "[]")):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text, encoding="utf-8")
        git(self.root, "init", "-q")
        git(self.root, "add", "src", "tools")
        git(self.root, "commit", "-q", "-m", "base")
        self.stamp = Path("build/stamp.json")
        self.write_stamp()

    def write_stamp(self):
        VERIFY.write_report_stamp(self.stamp, [Path("build/r.json"), Path("build/d.json")], self.root)

    def problem(self):
        return VERIFY.report_stamp_problem(self.stamp, self.root)

    def commit_notes(self, text):
        (self.root / "notes.txt").write_text(text, encoding="utf-8")
        git(self.root, "add", "notes.txt")
        git(self.root, "commit", "-q", "-m", text)

    def test_a_changed_tree_index_output_or_report_disables_reuse(self):
        self.assertEqual(self.problem(), "")
        for name, key in (("src/a.cpp", "source_dependency_sha256"), ("build/toy2.exe", "recompiled_sha256"),
                          ("build/toy2.pdb", "pdb_sha256"), ("build/d.json", "build/d.json"),
                          ("tools/Resources/functions_map.txt", "functions_map_sha256"),
                          ("reccmp-project.yml", "reccmp_project_sha256")):
            with self.subTest(name=name):
                path = self.root / name
                old = path.read_text(encoding="utf-8")
                path.write_text(old + "x", encoding="utf-8")
                self.assertIn(key, self.problem())
                path.write_text(old, encoding="utf-8")
                self.assertEqual(self.problem(), "")
        (self.root / "build" / "patcher.dll").write_text("new", encoding="utf-8")
        self.assertEqual(self.problem(), "")  # no report reads patcher.dll
        self.assertEqual(VERIFY.report_stamp_problem(self.stamp, self.root, required=(
            Path("build/r.json"), Path("build/x.json"))), "build/x.json not stamped")
        (self.root / "notes.txt").write_text("n", encoding="utf-8")
        git(self.root, "add", "notes.txt")
        self.assertEqual(self.problem(), "index_tree changed")
        self.assertEqual(VERIFY.report_stamp_problem(self.stamp, self.root, exempt=("index_tree",)), "")
        git(self.root, "reset", "-q", "notes.txt")
        self.assertEqual(self.problem(), "")
        (self.root / self.stamp).write_text("[]", encoding="utf-8")
        self.assertEqual(self.problem(), "no valid report stamp")

    def test_a_new_head_disables_reuse_until_a_clean_commit_advances_the_stamp(self):
        self.commit_notes("one")
        self.assertEqual(self.problem(), "git_head, index_tree changed")
        self.assertEqual(VERIFY.advance_report_stamp(self.stamp, self.root), "")
        self.assertEqual(self.problem(), "")
        (self.root / "src" / "a.cpp").write_text("int b;\n", encoding="utf-8")
        self.write_stamp()
        self.commit_notes("two")
        self.assertEqual(VERIFY.advance_report_stamp(self.stamp, self.root),
                         "src or the function map differs from HEAD")
        self.assertFalse((self.root / self.stamp).exists())

class LintDebtTests(unittest.TestCase):
    @staticmethod
    def finding(rule: str, severity: str, legacy: bool, address: str = "0x00401000"):
        return VERIFY.decomp_lint.Finding(
            Path("src/a.cpp"), 1, rule, severity, "x", "detail", owner_kind="function",
            owner_address=address, subject=rule, fingerprint="f", legacy=legacy,
        )

    def test_a_removed_baselined_warning_is_removed_debt_unless_it_is_advice(self):
        finding = self.finding("unexplained-helper", "warning", False)
        entry = VERIFY.decomp_lint.BaselineEntry(*finding.baseline_key, "src/a.cpp")
        change = VERIFY.classify_lint_debt_change([finding], [entry], [], [])
        self.assertEqual(change, VERIFY.LintDebtChange(removed_warnings=1))
        advice = self.finding("unnamed-constant", "warning", False)
        entry = VERIFY.decomp_lint.BaselineEntry(*advice.baseline_key, "src/a.cpp")
        self.assertEqual(VERIFY.classify_lint_debt_change([advice], [entry], [], []), VERIFY.LintDebtChange())

    def test_legacy_debt_can_be_left_out(self):
        legacy = self.finding("magic-pointer", "error", True)
        new = self.finding("decompiler-identifier", "error", False, "0x00402000")
        self.assertEqual(VERIFY.debt_by_address([legacy, new]),
                         {0x401000: ["magic-pointer"], 0x402000: ["decompiler-identifier"]})
        self.assertEqual(VERIFY.debt_by_address([legacy, new], legacy=False),
                         {0x402000: ["decompiler-identifier"]})

    def test_advisory_findings_are_not_source_debt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.cpp"
            path.write_text(
                "enum { PHASE_DONE = 3 };\n// FUNCTION: TOY2 0x00ABCDE0\nvoid f(Boss* b)\n{\n"
                "\tif (b->phase == PHASE_DONE) b->phase = 0;\n\tif (b->phase == 3) use(iVar2);\n}\n",
                encoding="utf-8",
            )
            rules = [item.rule for item in VERIFY.read_debt_findings(path)]
        self.assertEqual(rules, ["decompiler-identifier"])


if __name__ == "__main__":
    unittest.main()
