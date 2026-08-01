import csv
import re
import subprocess
import unittest
from pathlib import Path

from tools.decomp_annotations import read_source_annotations


ROOT = Path(__file__).resolve().parents[2]
NORMATIVE_INPUTS = (
    "AGENTS.md",
    ".agents/skills/continue-decomp/SKILL.md",
    ".agents/skills/decomp-expert/SKILL.md",
    ".agents/skills/decomp-expert/agents/openai.yaml",
    ".notes/README.md",
    ".notes/codegen-patterns.md",
    ".notes/reccmp-mechanics.md",
    ".notes/original-names.md",
    ".notes/refactor-debt.md",
    ".notes/shared-globals.md",
    ".notes/lint-baseline.tsv",
    ".notes/lint-rules.md",
    "tools/Resources/reconstruction-blockers.tsv",
    "tools/Resources/tool_artifacts.tsv",
)


class NormativeInputTests(unittest.TestCase):
    def test_comparison_commands_refresh_the_game_build(self):
        script = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        ensure_build = re.search(
            r"^ensure_build\(\) \{\n(.*?)^\}", script, re.MULTILINE | re.DOTALL
        )
        self.assertIsNotNone(ensure_build)
        self.assertEqual(ensure_build.group(1).strip(), "build_game")

    def test_normative_inputs_exist_and_are_not_ignored(self):
        for name in NORMATIVE_INPUTS:
            with self.subTest(name=name):
                self.assertTrue((ROOT / name).is_file(), name)
                result = subprocess.run(
                    ["git", "check-ignore", "--quiet", name], cwd=ROOT, check=False
                )
                self.assertNotEqual(result.returncode, 0, f"{name} is ignored")

    def test_all_current_functions_have_a_verification_tag(self):
        allowed = {"matched", "effective", "tool", "provisional"}
        functions = [
            item
            for item in read_source_annotations(ROOT / "src")
            if item.kind == "function"
        ]
        self.assertTrue(functions)
        self.assertFalse(
            [item for item in functions if item.tag not in allowed],
            "each FUNCTION must have one verification tag",
        )

    def test_tool_artifact_rows_match_tool_annotations(self):
        path = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
        with path.open(encoding="utf-8", newline="") as handle:
            addresses = {
                int(row[0], 16)
                for row in csv.reader(handle, delimiter="\t")
                if row and not row[0].startswith("#")
            }
        annotated = {
            int(item.address, 16)
            for item in read_source_annotations(ROOT / "src")
            if item.kind == "function" and item.tag == "tool"
        }
        self.assertEqual(addresses, annotated)
        self.assertNotIn(0x00414320, addresses)

    def test_blocker_addresses_are_sorted_and_mapped(self):
        mapped = {
            int(line.split(None, 1)[0], 16)
            for line in (ROOT / "tools/Resources/functions_map.txt")
            .read_text(encoding="utf-8")
            .splitlines()
            if line and not line.startswith("#")
        }
        with (ROOT / "tools/Resources/reconstruction-blockers.tsv").open(
            encoding="utf-8", newline=""
        ) as handle:
            rows = [
                row
                for row in csv.reader(handle, delimiter="\t")
                if row and not row[0].startswith("#")
            ]
        targets = [int(row[0], 16) for row in rows]
        dependencies = {
            int(value, 16)
            for row in rows
            for value in row[1].split(",")
            if value != "-"
        }
        self.assertEqual(targets, sorted(set(targets)))
        self.assertTrue(set(targets) <= mapped)
        self.assertTrue(dependencies <= mapped)
        self.assertFalse([row for row in rows if len(row) < 3 or not row[2].strip()])

        kinds = {
            "semantic",
            "layout",
            "abi",
            "indirect-dispatch",
            "ownership",
            "source-form",
            "compiler-codegen",
            "tooling",
        }
        self.assertFalse([row for row in rows if len(row) != 4])
        self.assertFalse([row for row in rows if row[3] not in kinds])
        unfinished = {
            int(item.address, 16)
            for item in read_source_annotations(ROOT / "src")
            if item.kind == "stub"
        }
        annotated = {
            int(item.address, 16)
            for item in read_source_annotations(ROOT / "src")
            if item.kind in ("stub", "function")
        }
        unfinished |= set(targets) - annotated
        self.assertTrue(set(targets) <= unfinished)


if __name__ == "__main__":
    unittest.main()
