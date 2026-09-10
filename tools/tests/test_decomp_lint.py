from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
