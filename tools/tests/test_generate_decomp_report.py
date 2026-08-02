import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "generate-decomp-report.py"
SPEC = importlib.util.spec_from_file_location("generate_decomp_report", SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(REPORT)


class ReportMetricTests(unittest.TestCase):
    def test_top_cards_lead_with_terminal_and_effective_bytes(self):
        template = (SCRIPT.parent / "decomp-report-template.html").read_text(
            encoding="utf-8"
        )
        cards = template[template.index("const cards = [") : template.index(
            "];", template.index("const cards = [")
        )]
        self.assertLess(cards.index('"Terminal bytes"'), cards.index('"Effective bytes"'))
        self.assertLess(cards.index('"Effective bytes"'), cards.index('"Implementation coverage"'))
        self.assertIn('"Change gate"', cards)
        self.assertIn('"Source debt"', cards)
        self.assertNotIn('"Project progress"', cards)
        self.assertNotIn('"Project coverage"', cards)
        self.assertNotIn('"Project accuracy"', cards)

    def test_treemap_renders_the_byte_weighted_hierarchy_recursively(self):
        template = (SCRIPT.parent / "decomp-report-template.html").read_text(
            encoding="utf-8"
        )
        self.assertIn("function buildTreemapHierarchy()", template)
        self.assertIn(
            "renderTreemapNode(child, child, branch, visualDepth + 1)", template
        )
        self.assertIn(
            "layout(focus.children, 0, 0, host.clientWidth, host.clientHeight)",
            template,
        )
        self.assertIn("function worstAspect(row, shortSide)", template)
        self.assertNotIn("tree-mosaic", template)
        self.assertNotIn("tree-dot", template)

    def test_function_map_provides_original_address_spans(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functions.txt"
            path.write_text("0x00401000 First\n0x00401025 Second\n", encoding="utf-8")
            names, sizes = REPORT.read_function_map(path)
            self.assertEqual(names["0x401000"], "First")
            self.assertEqual(sizes["0x401000"], 0x25)

    def test_reads_original_function_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sizes.json"
            path.write_text(
                '[{"address":"00401000","size":37},'
                '{"address":"00402000","size":1}]',
                encoding="utf-8",
            )
            self.assertEqual(REPORT.read_function_sizes(path), {"0x401000": 37})

    def test_binary_layout_counts_each_raw_file_byte_once(self):
        metadata = REPORT.decomp_binary.ImageMetadata(
            file_size=0x4A0,
            image_base=0x400000,
            header_size=0x200,
            sections=(
                REPORT.decomp_binary.Section(
                    ".text", 0x401000, 0x100, 0x200, 0x100, 0x60000020
                ),
                REPORT.decomp_binary.Section(
                    ".data", 0x402000, 0x180, 0x400, 0x80, 0xC0000040
                ),
            ),
        )
        entities = [
            {
                "address": "0x401010",
                "category": "project",
                "original_size": 0x20,
                "matching": 0.5,
                "status": "partial",
            },
            {
                "address": "0x401030",
                "category": "project",
                "original_size": 0x10,
                "matching": 0.2,
                "effective": True,
                "status": "effective",
            },
        ]

        layout = REPORT.build_binary_layout(metadata, entities)

        self.assertEqual(sum(item["size"] for item in layout["segments"]), 0x4A0)
        self.assertEqual(
            [(item["kind"], item["offset"], item["size"]) for item in layout["segments"]],
            [
                ("headers", 0, 0x200),
                ("section", 0x200, 0x100),
                ("gap", 0x300, 0x100),
                ("section", 0x400, 0x80),
                ("overlay", 0x480, 0x20),
            ],
        )
        self.assertEqual(layout["scored_code_bytes"], 0x30)
        self.assertEqual(layout["explained_code_bytes"], 0x20)
        self.assertEqual(layout["unexplained_scored_code_bytes"], 0x10)
        self.assertEqual(layout["unscored_file_bytes"], 0x4A0 - 0x30)
        text = next(item for item in layout["sections"] if item["name"] == ".text")
        self.assertEqual(text["unscored_bytes"], 0xD0)
        data = next(item for item in layout["sections"] if item["name"] == ".data")
        self.assertEqual(data["measurement"], "unscored")
        self.assertEqual(data["explained_bytes"], 0)

    def test_reads_pe_layout_fields(self):
        image = bytearray(0x500)
        image[:2] = b"MZ"
        struct.pack_into("<I", image, 0x3C, 0x80)
        image[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<H", image, 0x86, 1)
        struct.pack_into("<H", image, 0x94, 0xE0)
        optional = 0x98
        struct.pack_into("<H", image, optional, 0x10B)
        struct.pack_into("<I", image, optional + 28, 0x400000)
        struct.pack_into("<I", image, optional + 60, 0x200)
        section = optional + 0xE0
        image[section : section + 8] = b".text\0\0\0"
        struct.pack_into("<I", image, section + 8, 0x280)
        struct.pack_into("<I", image, section + 12, 0x1000)
        struct.pack_into("<I", image, section + 16, 0x200)
        struct.pack_into("<I", image, section + 20, 0x200)
        struct.pack_into("<I", image, section + 36, 0x60000020)

        metadata = REPORT.decomp_binary.parse_image_metadata(bytes(image))

        self.assertEqual(metadata.file_size, 0x500)
        self.assertEqual(metadata.header_size, 0x200)
        self.assertEqual(metadata.sections[0].virtual_address, 0x401000)
        self.assertEqual(metadata.sections[0].raw_pointer, 0x200)
        self.assertEqual(metadata.sections[0].characteristics, 0x60000020)

    def test_missing_and_malformed_retail_files_do_not_stop_the_report(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            missing = REPORT.read_binary_layout(directory / "missing.exe", [])
            malformed_path = directory / "bad.exe"
            malformed_path.write_bytes(b"not a PE image")
            malformed = REPORT.read_binary_layout(malformed_path, [])

        self.assertFalse(missing["available"])
        self.assertEqual(missing["error"], "The retail executable is not available.")
        self.assertFalse(malformed["available"])
        self.assertIn("Cannot read the retail executable layout", malformed["error"])

    def test_template_has_separate_raw_region_views(self):
        template = (SCRIPT.parent / "decomp-report-template.html").read_text(
            encoding="utf-8"
        )
        self.assertIn('id="binary-layout-content"', template)
        self.assertIn('id="code-treemap"', template)
        self.assertIn('id="data-treemap"', template)
        self.assertIn('id="other-treemap"', template)
        self.assertIn("Unscored bytes do not count as explained.", template)

    def test_project_and_runtime_metrics_are_separate(self):
        report = {
            "file": "toy2.exe",
            "timestamp": 0,
            "data": [
                {"address": "0x401000", "name": "Game", "matching": 0.5, "effective": True},
                {"address": "0x500000", "name": "memcpy", "matching": 1.0},
            ],
        }
        annotations = {
            "0x401000": {"kind": "function", "source": "Game.cpp", "line": 1},
            "0x401010": {"kind": "stub", "source": "Game.cpp", "line": 2},
        }
        summary = {"implemented": 2, "total": 3, "accuracy": 75.0, "progress": 50.0, "effective_score": 1.5}
        result = REPORT.enrich_report(
            report,
            annotations,
            {"0x401000": "Game", "0x401010": "Stub", "0x401020": "Missing"},
            summary,
            {
                "0x401000": [
                    {"severity": "error", "rule": "raw-layout-access"},
                    {"severity": "warning", "rule": "unknown-symbol"},
                ]
            },
            {"0x401000": 16, "0x401010": 8, "0x401020": 24},
        )
        metrics = result["metrics"]
        self.assertEqual(metrics["project_total"], 3)
        self.assertEqual(metrics["project_implemented"], 1)
        self.assertEqual(metrics["project_started"], 2)
        self.assertEqual(metrics["project_compared"], 1)
        self.assertEqual(metrics["project_accuracy"], 100.0)
        self.assertEqual(metrics["project_original_bytes"], 48)
        self.assertEqual(metrics["project_matched_bytes"], 8)
        self.assertEqual(metrics["project_effective_bytes"], 16)
        self.assertAlmostEqual(metrics["project_byte_progress"], 100 / 6)
        self.assertAlmostEqual(metrics["project_effective_byte_progress"], 100 / 3)
        self.assertTrue(metrics["change_gate_passed"])
        self.assertEqual(metrics["verified_functions"], 0)
        self.assertEqual(metrics["terminal_functions"], 0)
        self.assertEqual(metrics["terminal_bytes"], 0)
        self.assertEqual(metrics["coverage_gap_bytes"], 32)
        self.assertEqual(metrics["refinement_gap_bytes"], 0)
        self.assertEqual(metrics["provisional_functions"], 1)
        self.assertEqual(metrics["source_debt_functions"], 1)
        self.assertEqual(metrics["binary_effective_functions"], 1)
        self.assertEqual(metrics["runtime_compared"], 1)
        self.assertEqual(metrics["runtime_accuracy"], 100.0)
        game = next(item for item in result["entities"] if item["address"] == "0x401000")
        self.assertEqual(game["quality_errors"], 1)
        self.assertEqual(game["quality_warnings"], 1)
        self.assertEqual(game["verification"], "provisional")
        self.assertEqual(game["original_size"], 16)
        missing = next(item for item in result["entities"] if item["address"] == "0x401020")
        self.assertEqual(missing["name"], "Missing")
        self.assertEqual(missing["category"], "project")
        self.assertEqual(missing["status"], "unmatched")
        self.assertEqual(missing["original_size"], 24)

    def test_lint_owner_addresses_are_canonicalized(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000\n"
                "void Test() { int *g_unk401000 = 0; }\n",
                encoding="utf-8",
            )
            quality, _ = REPORT.read_lint_quality(source)
            self.assertIn("0x401000", quality)

    def test_terminal_metrics_exclude_tool_only_matches(self):
        symbol_diff = [[0, [{
            "orig": [[0, "mov eax, (OFFSET) g_first"]],
            "recomp": [[0, "mov eax, (DATA) g_second"]],
        }]]]
        report = {
            "file": "toy2.exe",
            "timestamp": 0,
            "data": [
                {"address": "0x401000", "matching": 1.0},
                {"address": "0x402000", "matching": 0.8, "effective": True},
                {"address": "0x403000", "matching": 0.9, "diff": symbol_diff},
            ],
        }
        annotations = {
            address: {"kind": "function", "source": "Game.cpp", "line": index}
            for index, address in enumerate(
                ("0x401000", "0x402000", "0x403000"), 1
            )
        }
        result = REPORT.enrich_report(
            report,
            annotations,
            {address: address for address in annotations},
            {"implemented": 3, "total": 3},
            function_sizes={address: 10 for address in annotations},
            tool_artifacts={0x403000: "symbol display"},
        )
        metrics = result["metrics"]
        self.assertEqual(metrics["verified_functions"], 3)
        self.assertEqual(metrics["terminal_functions"], 2)
        self.assertEqual(metrics["terminal_bytes"], 20)
        self.assertEqual(metrics["project_effective_bytes"], 29)
        self.assertAlmostEqual(metrics["refinement_gap_bytes"], 1)


if __name__ == "__main__":
    unittest.main()
