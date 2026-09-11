from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from dataclasses import replace
from io import StringIO
from unittest.mock import patch
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
for entry in (str(ROOT), str(TOOLS)):
    if entry not in sys.path:
        sys.path.insert(0, entry)

spec = importlib.util.spec_from_file_location("toy2_decomp_lint", TOOLS / "decomp_lint.py")
assert spec is not None and spec.loader is not None
lint = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = lint
spec.loader.exec_module(lint)


def findings_for(source: str) -> list[lint.Finding]:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "probe.cpp"
        path.write_text(source, encoding="utf-8")
        return lint.check_file(path)


def rules(source: str) -> set[str]:
    return {finding.rule for finding in findings_for(source)}


def severities(source: str) -> dict[str, str]:
    return {finding.rule: finding.severity for finding in findings_for(source)}


class RawOffsetCastTests(unittest.TestCase):
    def test_literal_displacement_is_an_error(self):
        source = "int16_t f() { return ((int16_t*)((char*)g_buffer + 0x104))[slot]; }\n"
        self.assertIn("raw-layout-access", rules(source))
        self.assertEqual(severities(source)["raw-layout-access"], "error")

    def test_literal_displacement_dereference_is_an_error(self):
        source = "void f() { *(int16_t*)((char*)g_object + 0x9fdd8) = 0; }\n"
        self.assertIn("raw-layout-access", rules(source))

    def test_decimal_literal_displacement_is_an_error(self):
        source = "void f() { *(int16_t*)((char*)g_object + 4) = 0; }\n"
        self.assertIn("raw-layout-access", rules(source))

    def test_runtime_stride_is_accepted(self):
        # Walking a locked surface by its pitch is correct, idiomatic code.
        source = (
            "uint16_t* row(void* base, int y, int pitch)\n"
            "{ return (uint16_t*)((uint8_t*)base + y * pitch); }\n"
        )
        self.assertNotIn("raw-layout-access", rules(source))

    def test_a_declared_struct_access_is_accepted(self):
        source = "int16_t ok(DrawBuffer* drawb, int i) { return drawb->VerticeCount[i]; }\n"
        self.assertEqual(rules(source), set())


class PlaceholderParameterTests(unittest.TestCase):
    def test_field_offset_parameter_is_an_error(self):
        source = "void f(int32_t vertexCount, int32_t field88) {}\n"
        self.assertIn("placeholder-parameter", rules(source))
        self.assertEqual(severities(source)["placeholder-parameter"], "error")

    def test_numbered_parameter_is_an_error(self):
        source = "void f(void* target, int32_t param5) {}\n"
        self.assertIn("placeholder-parameter", rules(source))

    def test_decompiler_temporary_parameter_is_an_error(self):
        source = "void f(int32_t iVar1) {}\n"
        self.assertIn("placeholder-parameter", rules(source))

    def test_named_parameters_are_accepted(self):
        source = "void f(int32_t vertexCount, int32_t renderState, uint32_t* texData) {}\n"
        self.assertEqual(rules(source), set())

    def test_a_struct_field_placeholder_is_only_a_warning(self):
        # A layout still being recovered may hold a placeholder field, but a
        # parameter may not: the caller already proves the argument's role.
        source = "struct Command\n{\n\tint32_t field80;\n};\n"
        self.assertEqual(severities(source).get("placeholder-field"), "warning")
        self.assertNotIn("placeholder-parameter", rules(source))

    def test_a_stub_parameter_is_advice_until_the_body_reveals_its_role(self):
        source = "// STUB: TOY2 0x00401000\nvoid f(int32_t param1) {}\n"
        self.assertEqual(severities(source).get("placeholder-parameter"), "warning")

    def test_a_completed_local_is_an_error(self):
        source = "// FUNCTION: TOY2 0x00401000\nvoid f() { int32_t iVar2 = 4; use(iVar2); }\n"
        self.assertEqual(severities(source).get("decompiler-identifier"), "error")


class MagicPointerTests(unittest.TestCase):
    def test_integer_cast_to_pointer_is_an_error(self):
        source = "void f() { g_handle = (void*)0x3ff; }\n"
        self.assertIn("magic-pointer", rules(source))

    def test_null_is_accepted(self):
        source = "void f() { g_handle = (void*)0; }\n"
        self.assertNotIn("magic-pointer", rules(source))


class ArithmeticNameTests(unittest.TestCase):
    def test_copy_suffix_warns(self):
        source = "int32_t g_destRectWidthCopy;\n"
        self.assertEqual(severities(source).get("arithmetic-name"), "warning")

    def test_times_minus_suffix_warns(self):
        source = "int32_t g_destRectWidthTimes1024Minus1;\n"
        self.assertIn("arithmetic-name", rules(source))

    def test_an_ordinary_word_ending_is_accepted(self):
        # `Template` must not match the `Temp` suffix rule.
        source = "int32_t g_instanceSpriteTemplate;\n"
        self.assertEqual(rules(source), set())

    def test_a_role_based_name_is_accepted(self):
        source = "int32_t g_screenHalfWidth;\n"
        self.assertEqual(rules(source), set())


class CommentTests(unittest.TestCase):
    def test_a_pattern_inside_a_comment_is_ignored(self):
        source = "// ((int16_t*)((char*)g_buffer + 0x104))[slot] is the old form\n"
        self.assertEqual(rules(source), set())

    def test_a_retail_log_string_is_not_a_struct_declaration(self):
        source = '\t\t\tLogger::LogDDError("drawb->VerticeCount[i]", error);\n'
        self.assertEqual(rules(source), set())

    def test_inactive_source_is_ignored(self):
        source = "#if 0\nvoid f() { *(int*)((char*)p + 0x20) = 1; }\n#endif\n"
        self.assertEqual(rules(source), set())

    def test_multiline_offset_cast_is_detected(self):
        source = "void f() { return_value((int16_t*)((uint8_t*)base\n + 0x20)); }\n"
        self.assertIn("raw-layout-access", rules(source))


class PointerModelTests(unittest.TestCase):
    def test_numbered_data_slot_is_source_debt(self):
        findings = lint.check_text(
            Path("Camera.cpp"),
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
            "void Init() { camera.data[3] = 0; }\n",
        )
        self.assertIn("opaque-state-slot", {item.rule for item in findings})

    def test_address_named_global_is_source_debt(self):
        findings = lint.check_text(
            Path("Camera.cpp"),
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
            "void Init() { g_unk50A118 = 0; }\n",
        )
        self.assertIn("address-named-symbol", {item.rule for item in findings})

    def test_vertex_pointer_byte_roundtrip_is_an_error(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\n"
            "void f(Nu3D::VertexTL* lpvVertices, int offset)\n"
            "{ Nu3D::VertexTL* vertex = (Nu3D::VertexTL*)((uint8_t*)lpvVertices + offset); }\n"
        )
        self.assertEqual(severities(source).get("typed-byte-roundtrip"), "error")

    def test_row_pointer_byte_roundtrip_is_an_error(self):
        source = (
            "uint32_t* next(uint32_t* primaryRow, int pitch)\n"
            "{ return (uint32_t*)((uint8_t*)primaryRow + pitch); }\n"
        )
        self.assertEqual(severities(source).get("typed-byte-roundtrip"), "error")

    def test_callback_context_can_be_named_once(self):
        source = "void f(void* context) { Device* device = reinterpret_cast<Device*>(context); use(device); }\n"
        self.assertNotIn("anonymous-buffer-view", rules(source))

    def test_inline_untyped_dereference_is_an_error(self):
        source = "int f(uint8_t* data) { return *reinterpret_cast<int32_t*>(data); }\n"
        self.assertEqual(severities(source).get("anonymous-buffer-view"), "error")

    def test_runtime_surface_cursor_with_named_view_is_accepted(self):
        source = (
            "void f(uint8_t* row, int pitch)\n"
            "{ uint32_t* pixels = reinterpret_cast<uint32_t*>(row); use(pixels); row += pitch; }\n"
        )
        self.assertNotIn("anonymous-buffer-view", rules(source))

    def test_local_view_with_reserved_storage_warns(self):
        source = (
            "struct RecordView { uint8_t reserved[8]; void* data; };\n"
            "STATIC_ASSERT(sizeof(RecordView) == 12);\n"
        )
        self.assertEqual(severities(source).get("surrogate-layout"), "warning")

    def test_completed_cast_to_local_view_warns(self):
        source = (
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
            "void f(void* data) { RecordView* view = reinterpret_cast<RecordView*>(data); use(view); }\n"
        )
        self.assertEqual(severities(source).get("surrogate-layout-use"), "warning")

    def test_narrow_suppression_needs_a_reason_and_applies_to_next_line(self):
        source = (
            "uint32_t* f(uint32_t* row, int pitch)\n{\n"
            "// decomp-lint: allow[typed-byte-roundtrip] reason: SDK pitch is measured in bytes\n"
            "return (uint32_t*)((uint8_t*)row + pitch);\n}\n"
        )
        findings = findings_for(source)
        item = next(item for item in findings if item.rule == "typed-byte-roundtrip")
        self.assertTrue(item.suppressed)

    def test_repeated_scalar_offsets_require_a_record(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\n"
            "int f(uint8_t* record)\n"
            "{ return *reinterpret_cast<int16_t*>(record + 2) + "
            "*reinterpret_cast<int32_t*>(record + 8); }\n"
        )
        self.assertEqual(severities(source).get("implicit-record-layout"), "error")


class AnnotationTests(unittest.TestCase):
    def test_global_does_not_inherit_the_previous_function(self):
        source = (
            "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
            "void f() { use(); }\n"
            "// GLOBAL: TOY2 0x00500000\n"
            "int32_t g_unk500000;\n"
        )
        self.assertNotIn("unknown-symbol", rules(source))

    def test_empty_function_must_be_a_stub(self):
        source = "// FUNCTION: TOY2 0x00401000\nvoid f() {}\n"
        self.assertEqual(severities(source).get("unfinished-function"), "error")

    def test_matched_null_function_is_accepted(self):
        source = "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid f() {}\n"
        self.assertNotIn("unfinished-function", rules(source))

    def test_library_body_is_not_checked(self):
        source = "// LIBRARY: TOY2 0x00401000\nvoid f() { int iVar2 = 0; }\n"
        self.assertNotIn("decompiler-identifier", rules(source))


class ControlFlowTests(unittest.TestCase):
    def test_several_targets_warn(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\n"
            "void f(int value) { if (value == 1) goto first; if (value == 2) goto second; "
            "goto third; first: use(1); second: use(2); third: use(3); }\n"
        )
        self.assertIn("unstructured-control-flow", rules(source))

    def test_one_cleanup_target_is_accepted(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\n"
            "void f(int value) { if (value == 1) goto cleanup; if (value == 2) goto cleanup; "
            "if (value == 3) goto cleanup; cleanup: release(); }\n"
        )
        self.assertNotIn("unstructured-control-flow", rules(source))

    def test_gotos_inside_a_macro_body_are_not_charged_to_the_preceding_function(self):
        # A shared edge-walker macro defined between two functions expands into
        # its users; its literal gotos are not the previous function's flow.
        source = (
            "// FUNCTION: TOY2 0x00401000\n"
            "void f(int value) { use(value); }\n"
            "#define WALK(a) \\\n"
            "    if (a == 1) goto first; \\\n"
            "    if (a == 2) goto second; \\\n"
            "    goto third;\n"
            "// FUNCTION: TOY2 0x00402000\n"
            "void g(int value) { WALK(value); first: use(1); second: use(2); third: use(3); }\n"
        )
        self.assertNotIn("unstructured-control-flow", rules(source))


class LayoutAssertionTests(unittest.TestCase):
    def test_qualified_nested_struct_size_assertion_is_accepted(self):
        source = (
            "struct Outer\n{\n"
            "\tstruct Inner\n\t{\n\t\tint32_t unk10;\n\t};\n"
            "};\n"
            "STATIC_ASSERT(sizeof(Outer) == 4);\n"
            "STATIC_ASSERT(sizeof(Outer::Inner) == 4);\n"
        )
        self.assertNotIn("unpinned-layout", rules(source))

    def test_unrelated_qualified_size_assertion_does_not_pin_nested_struct(self):
        source = (
            "struct Outer\n{\n"
            "\tstruct Inner\n\t{\n\t\tint32_t unk10;\n\t};\n"
            "};\n"
            "STATIC_ASSERT(sizeof(Outer) == 4);\n"
            "STATIC_ASSERT(sizeof(Other::Inner) == 4);\n"
        )
        self.assertIn("unpinned-layout", rules(source))


class ScanCacheTests(unittest.TestCase):
    """A whole-tree scan of src is saved by content, so a later process reads it back."""

    def test_the_same_text_is_read_back_and_a_changed_or_outside_unit_scans_again(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            source, cache = root / "src", root / "build" / "decomp-cache" / "lint"
            units = [lint.SourceUnit(source / "a.cpp", "int a;\n"), lint.SourceUnit(source / "b.cpp", "int b;\n")]

            def check(path, text):
                return [lint.Finding(path, 1, "rule", "error", text, "detail", owner_address="0x00401000")]

            with patch.object(lint, "SOURCE_ROOT", source), patch.object(lint, "CACHE_DIR", cache), \
                    patch.object(lint, "check_text", side_effect=check) as scan:
                first = lint.scan_units(units)
                self.assertEqual(lint.scan_units(list(units)), first)
                self.assertEqual(scan.call_count, 2)
                lint.scan_units([units[0], lint.SourceUnit(source / "b.cpp", "int c;\n")])
                self.assertEqual(scan.call_count, 4)
                outside = [lint.SourceUnit(root / "other.cpp", "int a;\n")]
                lint.scan_units(outside)
                lint.scan_units(outside)
                self.assertEqual(scan.call_count, 6)
            self.assertEqual(len(list(cache.glob("*.json"))), 2)


class BaselineTests(unittest.TestCase):
    def test_baseline_classification_and_stale_rows(self):
        source = "// FUNCTION: TOY2 0x00401000\nvoid f() { int iVar2 = 0; use(iVar2); }\n"
        finding = next(item for item in findings_for(source) if item.rule == "decompiler-identifier")
        entry = lint.BaselineEntry(*finding.baseline_key, finding.relative_path)
        classified, stale = lint.apply_baseline([finding], [entry])
        self.assertTrue(classified[0].legacy)
        self.assertEqual(stale, [])
        _, stale = lint.apply_baseline([], [entry])
        self.assertEqual(stale, [entry])


class CrossFileTests(unittest.TestCase):
    def repeated_type_findings(self, sources: dict[str, str]) -> list[lint.Finding]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            units = [lint.SourceUnit(root / name, source) for name, source in sources.items()]
            return lint.scan_units(units)

    def test_two_equivalent_private_structs_are_errors(self):
        definition = "namespace Toy2 { namespace Level { struct Record { int id; const char* text; }; } }\n"
        findings = self.repeated_type_findings({f"Level{index}.cpp": definition for index in range(2)})
        repeated = [item for item in findings if item.rule == "repeated-private-type"]
        self.assertEqual(len(repeated), 2)
        self.assertEqual({item.subject for item in repeated}, {"Toy2::Record"})
        self.assertEqual({item.severity for item in repeated}, {"error"})

    def test_qualified_field_types_are_compared(self):
        definition = (
            "namespace Toy2 { struct SurfaceCollisionResult { "
            "Platform::CollisionFace* face; int32_t distance; }; }\n"
        )
        findings = self.repeated_type_findings({f"Collision{index}.cpp": definition for index in range(2)})
        repeated = [item for item in findings if item.rule == "repeated-private-type"]
        self.assertEqual(len(repeated), 2)
        self.assertEqual({item.subject for item in repeated}, {"Toy2::SurfaceCollisionResult"})

    def test_bit_field_layouts_are_skipped(self):
        definition = "struct StatusBits { unsigned int active : 1; unsigned int mode : 3; };\n"
        findings = self.repeated_type_findings({f"Status{index}.cpp": definition for index in range(2)})
        self.assertNotIn("repeated-private-type", {item.rule for item in findings})

    def test_one_private_struct_is_accepted(self):
        definition = "struct Record { int id; const char* text; };\n"
        findings = self.repeated_type_findings({"One.cpp": definition})
        self.assertNotIn("repeated-private-type", {item.rule for item in findings})

    def test_different_root_namespaces_are_accepted(self):
        sources = {
            f"{name}.cpp": f"namespace {name} {{ struct Record {{ int id; }}; }}\n"
            for name in ("Audio", "Game", "Render")
        }
        findings = self.repeated_type_findings(sources)
        self.assertNotIn("repeated-private-type", {item.rule for item in findings})

    def test_forward_empty_header_and_different_layout_are_accepted(self):
        sources = {
            "Forward.cpp": "struct Record;\n",
            "Empty.cpp": "struct Record {};\n",
            "Owner.h": "struct Record { int id; };\n",
            "One.cpp": "struct Record { int id; };\n",
            "Two.cpp": "struct Record { int count; };\n",
        }
        findings = self.repeated_type_findings(sources)
        self.assertNotIn("repeated-private-type", {item.rule for item in findings})

    def test_a_subsystem_internal_header_is_a_local_definition(self):
        definition = "namespace Toy2 { struct IniState { int line; const char* text; }; }\n"
        use = '#include "Toy2/Toy2Internal.h"\n'
        for header in ("Toy2/Toy2Internal.h", "Toy2/Internal/IniTypes.h"):
            with self.subTest(header=header):
                shared = self.repeated_type_findings({header: definition, "Toy2.cpp": use, "Ini.cpp": use})
                self.assertNotIn("repeated-private-type", {item.rule for item in shared})
                leftover = self.repeated_type_findings({header: definition, "Ini.cpp": definition})
                repeated = [item for item in leftover if item.rule == "repeated-private-type"]
                self.assertEqual(len(repeated), 2)
                self.assertIn("Internal.h", repeated[0].detail)
        plain = self.repeated_type_findings({"Toy2/Ini.h": definition, "Ini.cpp": definition})
        self.assertNotIn("repeated-private-type", {item.rule for item in plain})

    def test_array_extents_remain_distinct(self):
        sources = {
            f"{index}.cpp": f"struct Table {{ Record records[{index}]; int end; }};\n"
            for index in range(1, 5)
        }
        findings = self.repeated_type_findings(sources)
        self.assertNotIn("repeated-private-type", {item.rule for item in findings})

    def test_a_new_duplicate_stays_new_after_existing_rows_are_baselined(self):
        definition = "struct Record { int id; const char* text; };\n"
        findings = self.repeated_type_findings({f"{index}.cpp": definition for index in range(4)})
        repeated = [item for item in findings if item.rule == "repeated-private-type"]
        baseline = [
            lint.BaselineEntry(*finding.baseline_key, finding.relative_path)
            for finding in repeated[:3]
        ]
        classified, _ = lint.apply_baseline(repeated, baseline)
        self.assertEqual(sum(item.legacy for item in classified), 3)
        self.assertEqual(sum(not item.legacy for item in classified), 1)

    def test_project_call_cast_is_signature_concealment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = lint.SourceUnit(root / "Thing.h", "void UseThing(Thing* thing);\n")
            source = lint.SourceUnit(
                root / "Thing.cpp",
                "// FUNCTION: TOY2 0x00401000\n"
                "void Caller(void* value) { UseThing((Thing*)value); }\n",
            )
            findings = lint.scan_units([header, source])
        self.assertIn("signature-concealment", {item.rule for item in findings})

    def test_sdk_void_output_cast_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = lint.SourceUnit(root / "Thing.h", "void Lock(void** output);\n")
            source = lint.SourceUnit(
                root / "Thing.cpp",
                "// FUNCTION: TOY2 0x00401000\n"
                "void Caller(Vertex** value) { Lock((void**)value); }\n",
            )
            findings = lint.scan_units([header, source])
        self.assertNotIn("signature-concealment", {item.rule for item in findings})


class ExplicitTargetTests(unittest.TestCase):
    def test_a_named_path_that_is_gone_is_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            present = root / "Present.cpp"
            present.write_text("// FUNCTION: TOY2 0x00401000\nvoid f() {}\n", encoding="utf-8")
            # `git diff --name-only` still names a file the campaign deleted or moved.
            units = lint.target_units(False, [str(present), str(root / "Moved.cpp")])
        self.assertEqual([unit.path for unit in units], [present.resolve()])


class StagedSourceTests(unittest.TestCase):
    def test_staged_scan_reads_the_index_not_the_worktree(self):
        listed = subprocess_result(stdout="src/Probe.cpp\n")
        indexed = subprocess_result(
            stdout=b"// FUNCTION: TOY2 0x00401000\nvoid f() { int iVar2 = 0; }\n"
        )
        with patch.object(lint.subprocess, "run", side_effect=[listed, indexed]):
            units = lint.target_units(True, [])
        self.assertEqual(len(units), 1)
        self.assertIn("iVar2", units[0].text)
        self.assertIn("decompiler-identifier", {item.rule for item in lint.scan_units(units)})

    def test_staged_duplicate_is_compared_with_an_existing_index_file(self):
        listed = subprocess_result(stdout="src/Existing.cpp\nsrc/New.cpp\n")
        existing = subprocess_result(stdout=b"struct Record { int id; };\n")
        staged = subprocess_result(stdout=b"struct Record { int id; };\n")
        with patch.object(lint.subprocess, "run", side_effect=[listed, existing, staged]):
            units = lint.target_units(True, [])
        repeated = [item for item in lint.scan_units(units) if item.rule == "repeated-private-type"]
        self.assertEqual(len(repeated), 2)

    def test_unstaged_duplicate_does_not_change_the_index_view(self):
        listed = subprocess_result(stdout="src/One.cpp\nsrc/Two.cpp\n")
        first_index = subprocess_result(stdout=b"struct Record { int id; };\n")
        second_index = subprocess_result(stdout=b"struct Record { int count; };\n")
        with patch.object(lint.subprocess, "run", side_effect=[listed, first_index, second_index]):
            units = lint.target_units(True, [])
        self.assertNotIn("repeated-private-type", {item.rule for item in lint.scan_units(units)})

    def test_staged_baseline_uses_the_index_and_keeps_stale_rows(self):
        row = b"src/Old.cpp\trepeated-private-type\tRecord\t123456789abc\tsrc/Old.cpp\n"
        with patch.object(lint.subprocess, "run", return_value=subprocess_result(stdout=row)):
            entries = lint.read_baseline(staged=True)
        self.assertEqual(len(entries), 1)
        _, stale = lint.apply_baseline([], entries)
        self.assertEqual(stale, entries)

    def test_staged_lint_rejects_a_stale_baseline_row(self):
        entry = lint.BaselineEntry(
            "src/Old.cpp", "repeated-private-type", "Record", "123456789abc", "src/Old.cpp"
        )
        with (
            patch.object(sys, "argv", ["decomp_lint.py", "--staged"]),
            patch.object(lint, "target_units", return_value=[]),
            patch.object(lint, "read_baseline", return_value=[entry]),
            patch("builtins.print"),
        ):
            self.assertEqual(lint.main(), 1)


def subprocess_result(*, stdout):
    class Result:
        returncode = 0

        def __init__(self, value):
            self.stdout = value

    return Result(stdout)



def macro(name: str, lines: int, last: str = "done(a)") -> str:
    """Return an object of `lines` physical lines: the #define line, steps, then `last`."""
    return f"#define {name}(a) \\\n" + "".join(f"\tstep{i}(a); \\\n" for i in range(lines - 2)) + f"\t{last}\n"


class QualityRuleTests(unittest.TestCase):
    def test_decimal_and_const_pointer_literals_are_magic(self):
        for body in ("Play(1, reinterpret_cast<const Vector3I*>(1));", "owner = (void*)1;",
                     "ok = guid != (GUID*)2;"):
            with self.subTest(body=body):
                source = f"// FUNCTION: TOY2 0x00401000\nvoid f() {{ {body} }}\n"
                self.assertEqual(severities(source).get("magic-pointer"), "error")

    def test_null_pointers_and_integer_casts_are_not_magic(self):
        source = "void f() { p = (void*)0x0; q = reinterpret_cast<Foo*>(0); n = sizeof(Foo*) * 2; }\n"
        self.assertNotIn("magic-pointer", rules(source))

    def test_a_cast_of_an_expression_is_not_magic(self):
        for body in ("b = (int*)(8 * n);", "c = (char*)(1 + base);"):
            with self.subTest(body=body):
                self.assertNotIn("magic-pointer", rules(f"void f() {{ {body} }}\n"))
        self.assertIn("magic-pointer", rules("void f() { p = (Foo*)(0x1234); }\n"))

    def test_a_directive_cannot_accept_a_cast_that_hides_a_type(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\nvoid f()\n{\n"
            "// decomp-lint: allow[magic-pointer] reason: PlaySound reads mode 1, not a Vector3I\n"
            "Play(1, reinterpret_cast<const Vector3I*>(1));\n}\n"
        )
        item = next(item for item in findings_for(source) if item.rule == "magic-pointer")
        self.assertFalse(item.suppressed)
        header = lint.SourceUnit(Path("Draw.h"), "void Draw(Vector3I* position);\n")
        caller = lint.SourceUnit(Path("Level.cpp"), "// FUNCTION: TOY2 0x00401000\nvoid f(Vector4I* record)\n{\n"
                                 "// decomp-lint: allow[signature-concealment] reason: Draw reads a Vector4I\n"
                                 "Draw((Vector3I*)record);\n}\n")
        self.assertEqual([item.suppressed for item in lint.check_signature_concealment([header, caller])],
                         [False])

    def test_a_suppressed_finding_never_hides_a_later_open_one(self):
        first = lint.Finding(Path("a.cpp"), 1, "typed-byte-roundtrip", "error", "x", "d",
                             owner_address="0x1", subject="s", suppressed=True)
        unique: dict = {}
        for finding in (first, replace(first, line=2, suppressed=False), replace(first, line=3)):
            lint._keep_first_open(unique, ("0x1", "s"), finding)
        self.assertEqual((unique["0x1", "s"].line, unique["0x1", "s"].suppressed), (2, False))

    def test_advisory_warnings_never_fail_warnings_as_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.cpp"
            path.write_text("enum { PHASE_DONE = 3 };\n// FUNCTION: TOY2 0x00401000\nvoid f(Boss* b)\n{\n"
                            "\tif (b->phase == PHASE_DONE) b->phase = 0;\n\tif (b->phase == 3) b->phase = 0;\n}\n",
                            encoding="utf-8")
            output = StringIO()
            with patch.object(sys, "argv", ["lint", "--warnings-as-errors", str(path)]), redirect_stdout(output):
                code = lint.main()
        self.assertEqual(code, 0, output.getvalue())
        self.assertIn("[unnamed-constant]", output.getvalue())

    def test_identical_long_macro_bodies_warn_once_per_copy(self):
        source = macro("BLEND25", 8) + macro("BLEND50", 8) + macro("BLEND75", 8)
        found = [item for item in findings_for(source) if item.rule == "repeated-macro-body"]
        self.assertEqual([item.subject for item in found], ["BLEND50", "BLEND75"])
        self.assertTrue(all(item.severity == "warning" and not item.advisory for item in found))
        self.assertIn("'BLEND25' (line 1)", found[0].detail)

    def test_a_macro_that_holds_a_run_of_another_macro_warns(self):
        for source in (macro("A", 12) + macro("B", 12, "done(a + 1)"),
                       macro("A", 12, 'log("a")') + macro("B", 12, 'log("b")')):
            with self.subTest(source=source[-20:]):
                found = [item for item in findings_for(source) if item.rule == "repeated-macro-body"]
                self.assertEqual([item.subject for item in found], ["B"])
                self.assertIn("holds 10 of its 11 lines as a run that 'A' (line 1)", found[0].detail)
                self.assertFalse(found[0].advisory or found[0].suppressed)

    def test_scattered_shared_lines_are_not_a_macro_copy(self):
        """Two edge walks state the same declarations and idioms without a copy."""
        steps = [f"\tstep{index}(a); \\\n" for index in range(12)]
        source = ("#define A(a) \\\n" + "".join(steps) + "\tdone(a)\n"
                  + "#define B(a) \\\n"
                  + "".join(step if index % 2 else f"\tblend{index}(a); \\\n"
                           for index, step in enumerate(steps))
                  + "\tdone(a)\n")
        self.assertNotIn("repeated-macro-body", rules(source))

    def test_short_unrelated_or_inactive_macros_do_not_warn(self):
        for source in (
            macro("A", 7) + macro("B", 7),
            macro("A", 8) + "#define B(a) \\\n" + "".join(
                f"\tother{i}(a); \\\n" for i in range(6)) + "\tdone(a)\n",
            "#if 0\n" + macro("A", 8) + "#endif\n" + macro("B", 8),
        ):
            with self.subTest(source=source[:30]):
                self.assertNotIn("repeated-macro-body", rules(source))

    def test_a_retail_duplicate_comment_accepts_a_repeated_macro(self):
        source = (macro("BLEND25", 8)
                  + "// retail-duplicate: retail writes one edge walk for each blend\n"
                  + macro("BLEND50", 8))
        found = [item for item in findings_for(source) if item.rule == "repeated-macro-body"]
        self.assertEqual([item.suppressed for item in found], [True])

    def test_a_reported_macro_is_not_reported_again_as_a_copied_block(self):
        self.assertEqual(rules(macro("A", 14) + macro("B", 14)), {"repeated-macro-body"})

    def test_a_literal_where_the_file_writes_a_name_warns(self):
        source = (
            "enum Phase { PHASE_DEFEATED = 3, STATE_RUN = 5 };\n#define SOUND_JUMP 0x3D\n"
            "// FUNCTION: TOY2 0x00401000\nvoid f(Boss* boss)\n{\n"
            "\tif (boss->phase == PHASE_DEFEATED) Play(SOUND_JUMP, 0);\n"
            "\tg_records[PHASE_DEFEATED] = 0;\n\tswitch (state) { case STATE_RUN: break; }\n}\n"
            "// FUNCTION: TOY2 0x00402000\nvoid g(Boss* boss)\n{\n"
            "\tif (boss->phase == 3) Play(0x3D, 0);\n\tg_records[3] = 0;\n"
            "\tswitch (state) { case 5: break; }\n\tswitch (other) { case 5: break; }\n}\n"
        )
        found = {(item.owner_address, item.subject): item for item in findings_for(source)
                 if item.rule == "unnamed-constant"}
        self.assertEqual(sorted(found), [("0x00402000", "3"), ("0x00402000", "5"), ("0x00402000", "61")])
        self.assertIn("(2 use(s)) stands where this file writes PHASE_DEFEATED",
                      found["0x00402000", "3"].detail)
        self.assertIn("(1 use(s))", found["0x00402000", "5"].detail)
        self.assertTrue(found["0x00402000", "61"].advisory)

    def test_a_value_in_another_place_or_a_plain_value_does_not_warn(self):
        source = (
            "enum Phase { PHASE_DEFEATED = 3, PHASE_TWO = 2 };\n"
            "// FUNCTION: TOY2 0x00401000\nvoid f(Boss* boss)\n{\n"
            "\tif (boss->phase == PHASE_DEFEATED || boss->phase == PHASE_TWO) boss->phase = 0;\n}\n"
            "// FUNCTION: TOY2 0x00402000\nvoid g(Boss* boss)\n{\n"
            "\tint sound = random & 3;\n\tif (boss->phase == 2) boss->timer = 3;\n}\n"
        )
        self.assertNotIn("unnamed-constant", rules(source))

    def test_a_name_inside_one_function_names_the_value_only_there(self):
        source = (
            "// FUNCTION: TOY2 0x00401000\nint f(int radius)\n{\n\tconst int32_t radiusScale = 0x1644;\n"
            "\treturn radius * radiusScale + radius * 0x1644;\n}\n"
            "// FUNCTION: TOY2 0x00402000\nint g(int radius)\n{\n\treturn radius * 0x1644;\n}\n"
        )
        found = [(item.owner_address, item.subject) for item in findings_for(source)
                 if item.rule == "unnamed-constant"]
        self.assertEqual(found, [("0x00401000", str(0x1644))])

    def test_an_accepted_finding_needs_no_baseline_row(self):
        finding = lint.Finding(Path("a.cpp"), 1, "magic-pointer", "error", "x", "detail",
                               owner_address="0x1", subject="integer-pointer", fingerprint="f",
                               suppressed=True)
        entry = lint.BaselineEntry(*finding.baseline_key, "a.cpp")
        self.assertEqual(lint.apply_baseline([finding], [entry])[1], [entry])
        self.assertEqual(lint.apply_baseline([replace(finding, suppressed=False)], [entry])[1], [])

    def test_prune_removes_only_stale_baseline_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baseline.tsv"
            header = "# owner\trule\tsubject\tfingerprint\tpath\n"
            path.write_text(header + "0x1\tr\ts\tf\ta.cpp\n0x2\tr\ts\tf\tb.cpp\n", encoding="utf-8")
            stale = [lint.BaselineEntry("0x2", "r", "s", "f", "b.cpp")]
            self.assertEqual(lint.prune_baseline(path, stale), 1)
            self.assertEqual(path.read_text(encoding="utf-8"), header + "0x1\tr\ts\tf\ta.cpp\n")


def walk(count: int, prefix: str = "edge") -> str:
    """Return `count` distinct statement lines, one per line."""
    return "".join(f"\t{prefix}.x{i} = {prefix}.y{i} * scale{i};\n" for i in range(count))


def function(address: str, name: str, body: str) -> str:
    return f"// FUNCTION: TOY2 {address}\nvoid {name}(Edge& edge)\n{{\n{body}}}\n"


class DuplicatedBlockTests(unittest.TestCase):
    def test_each_later_copy_of_a_long_run_is_one_finding(self):
        body = walk(14)
        source = (function("0x00401000", "drawFirst", body)
                  + function("0x00402000", "drawSecond", body)
                  + function("0x00403000", "drawThird", body))
        found = [item for item in findings_for(source) if item.rule == "duplicated-block"]
        self.assertEqual([item.owner_address for item in found], ["0x00402000", "0x00403000"])
        self.assertEqual([item.subject for item in found], ["14 lines", "14 lines"])
        self.assertEqual({item.severity for item in found}, {"warning"})
        self.assertFalse(any(item.advisory or item.suppressed for item in found))
        self.assertIn("14 lines repeat the block at line 4 (0x00401000)", found[0].detail)

    def test_comments_blank_lines_and_braces_do_not_separate_two_copies(self):
        body = walk(12)
        spaced = body.replace("\tedge.x3", "\n\t// The far edge.\n\t{\n\tedge.x3").replace(
            "\tedge.x7", "\t}\n\tedge.x7")
        source = (function("0x00401000", "drawFirst", body)
                  + function("0x00402000", "drawSecond", spaced))
        found = [item for item in findings_for(source) if item.rule == "duplicated-block"]
        self.assertEqual([(item.owner_address, item.subject) for item in found],
                         [("0x00402000", "12 lines")])

    def test_a_short_run_and_an_unrelated_body_are_accepted(self):
        for second in (walk(11), walk(14, "span")):
            with self.subTest(second=second[:20]):
                source = (function("0x00401000", "drawFirst", walk(11) + walk(3, "span"))
                          + function("0x00402000", "drawSecond", second))
                self.assertNotIn("duplicated-block", rules(source))

    def test_a_retail_duplicate_comment_accepts_a_copy(self):
        body = walk(14)
        source = (function("0x00401000", "drawFirst", body)
                  + "// retail-duplicate: retail walks each edge in the body\n"
                  + function("0x00402000", "drawSecond", body))
        found = [item for item in findings_for(source) if item.rule == "duplicated-block"]
        self.assertEqual([item.suppressed for item in found], [True])

    def test_one_comment_accepts_one_copy(self):
        body = walk(14)
        source = (function("0x00401000", "drawFirst", body)
                  + "// retail-duplicate: retail walks each edge in the body\n"
                  + function("0x00402000", "drawSecond", body + "\tmid(edge);\n" + body))
        found = [item for item in findings_for(source) if item.rule == "duplicated-block"]
        self.assertEqual([item.suppressed for item in found], [True, False])

    def test_a_comment_over_one_block_does_not_accept_a_copy_outside_it(self):
        guard = ("\t// retail-duplicate: retail states this guard in each mode\n"
                 "\t{\n\t\tedge.ready = 1;\n\t}\n")
        source = (function("0x00401000", "drawFirst", walk(14))
                  + function("0x00402000", "drawSecond", guard + walk(14)))
        found = [item for item in findings_for(source) if item.rule == "duplicated-block"]
        self.assertEqual([item.suppressed for item in found], [False])

    def test_a_repeated_data_table_is_not_copied_source(self):
        """A sprite sheet states its grid; AGENTS keeps a large initializer in an .inc file."""
        table = "".join(f"\t{{ {index}, 0 }},\n" for index in range(14))
        source = ("// GLOBAL: TOY2 0x00500000\nSheet g_first = {\n" + table + "};\n"
                  + "// GLOBAL: TOY2 0x00500100\nSheet g_second = {\n" + table + "};\n")
        self.assertNotIn("duplicated-block", rules(source))

    def test_an_edit_elsewhere_in_the_owner_keeps_the_same_finding(self):
        body = walk(14)
        first = function("0x00401000", "drawFirst", body)
        second = function("0x00402000", "drawSecond", body + "\tedge.done = 1;\n")
        before = [item for item in findings_for(first + second) if item.rule == "duplicated-block"]
        edited = second.replace("edge.done = 1;", "edge.done = edge.count;")
        after = [item for item in findings_for("// A leading comment.\n" + first + edited)
                 if item.rule == "duplicated-block"]
        self.assertEqual([item.baseline_key for item in before],
                         [item.baseline_key for item in after])
        self.assertEqual(after[0].line - before[0].line, 1)

    def test_a_baseline_row_stands_for_one_copy_of_its_owner(self):
        entry = lint.BaselineEntry("0x1", "duplicated-block", "20 lines", "aaaa", "a.cpp")
        finding = lint.Finding(Path("a.cpp"), 9, "duplicated-block", "warning", "x", "detail",
                               owner_address="0x1", subject="14 lines", fingerprint="bbbb")
        # A copy the writer shortened, and one the block it repeats lengthened, are the
        # same debt. A second copy, and a copy in an owner with no row, are new debt.
        for subject in ("14 lines", "24 lines"):
            classified, stale = lint.apply_baseline([replace(finding, subject=subject)], [entry])
            self.assertEqual(([item.legacy for item in classified], stale), ([True], []))
        added = replace(finding, subject="14 lines #2", fingerprint="cccc")
        self.assertEqual(
            [item.legacy for item in lint.apply_baseline([finding, added], [entry])[0]], [True, False])
        elsewhere = replace(finding, owner_address="0x2")
        self.assertFalse(lint.apply_baseline([elsewhere], [entry])[0][0].legacy)
        self.assertEqual(lint.apply_baseline([], [entry])[1], [entry])

    def test_sharing_one_copy_away_makes_its_row_stale(self):
        rows = [lint.BaselineEntry("0x1", "duplicated-block", f"{count} lines", f"f{count}", "a.cpp")
                for count in (20, 14)]
        kept = lint.Finding(Path("a.cpp"), 9, "duplicated-block", "warning", "x", "detail",
                            owner_address="0x1", subject="20 lines", fingerprint="f20")
        classified, stale = lint.apply_baseline([kept], rows)
        self.assertEqual([item.legacy for item in classified], [True])
        self.assertEqual([entry.subject for entry in stale], ["14 lines"])

    def test_a_new_copy_fails_warnings_as_errors_until_it_is_accepted(self):
        body = walk(14)
        source = function("0x00401000", "drawFirst", body) + function("0x00402000", "drawSecond", body)
        accepted = source.replace("// FUNCTION: TOY2 0x00402000",
                                  "// retail-duplicate: retail walks each edge\n// FUNCTION: TOY2 0x00402000")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.cpp"
            for text, expected in ((source, 1), (accepted, 0)):
                path.write_text(text, encoding="utf-8")
                output = StringIO()
                with patch.object(sys, "argv", ["lint", "--warnings-as-errors", str(path)]), \
                        redirect_stdout(output):
                    code = lint.main()
                self.assertEqual(code, expected, output.getvalue())
            self.assertIn("[duplicated-block]", output.getvalue())


if __name__ == "__main__":
    unittest.main()
