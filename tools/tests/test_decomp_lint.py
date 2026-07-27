from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
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
        self.assertIn("raw-offset-cast", rules(source))
        self.assertEqual(severities(source)["raw-offset-cast"], "error")

    def test_literal_displacement_dereference_is_an_error(self):
        source = "void f() { *(int16_t*)((char*)g_object + 0x9fdd8) = 0; }\n"
        self.assertIn("raw-offset-cast", rules(source))

    def test_runtime_stride_is_accepted(self):
        # Walking a locked surface by its pitch is correct, idiomatic code.
        source = (
            "uint16_t* row(void* base, int y, int pitch)\n"
            "{ return (uint16_t*)((uint8_t*)base + y * pitch); }\n"
        )
        self.assertNotIn("raw-offset-cast", rules(source))

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


if __name__ == "__main__":
    unittest.main()
