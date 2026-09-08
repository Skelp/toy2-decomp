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
    ".notes/source-models.md",
    ".notes/reccmp-mechanics.md",
    ".notes/original-names.md",
    ".notes/refactor-debt.md",
    ".notes/shared-globals.md",
    ".notes/lint-baseline.tsv",
    ".notes/lint-rules.md",
    "tools/Resources/reconstruction-blockers.tsv",
    "tools/Resources/campaign-ledger.jsonl",
    "tools/Resources/tool_artifacts.tsv",
)


def script_section(script: str, start: str, end: str) -> str:
    start_offset = script.index(start)
    return script[start_offset : script.index(end, start_offset)]


class NormativeInputTests(unittest.TestCase):
    def test_comparison_commands_refresh_the_game_build(self):
        script = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        ensure_build = re.search(
            r"^ensure_build\(\) \{\n(.*?)^\}", script, re.MULTILINE | re.DOTALL
        )
        self.assertIsNotNone(ensure_build)
        self.assertEqual(ensure_build.group(1).strip(), "build_game")

    def test_linux_baseline_propagates_each_setup_failure(self):
        script = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        build = script_section(script, "build() {", "# Print only")
        self.assertEqual(build.count("configure || return"), 2)
        self.assertIn('cmake --build build -- "-j${build_jobs}" || return', build)
        self.assertIn(
            'cmake --build build --target toy2decomp -- "-j${build_jobs}" || return',
            build,
        )
        self.assertEqual(build.count(") || return"), 2)
        baseline = script_section(script, "baseline() {", "validate() {")
        for command in (
            "build || return",
            "write_comparison_report decomp-baseline-report.json || return",
            "write_data_report decomp-baseline-data-report.json || return",
            "refresh_function_sizes || return",
            "--data-report build/decomp-baseline-data-report.json || return",
            "revision=$(git rev-parse --short HEAD) || return",
        ):
            self.assertIn(command, baseline)
        size_refresh = script_section(
            script, "function_sizes_are_valid() {", "baseline() {"
        )
        self.assertIn("_read_snapshot_sizes", size_refresh)
        self.assertIn("if ! function_sizes_are_valid", size_refresh)

        campaigns = script_section(script, "    campaigns)", "    defer)")
        self.assertIn("campaign_global_arguments", campaigns)
        self.assertIn('campaign_state_is_finalizing "$campaign_state_file"', campaigns)
        self.assertIn(
            '"${campaign_global_arguments[@]}" set-baseline --quiet', campaigns
        )
        self.assertIn('"${campaign_global_arguments[@]}" abort', campaigns)

    def test_windows_campaign_dispatch_has_lifecycle_parity(self):
        script = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        commands = re.search(r"\[ValidateSet\((.*?)\)\]", script, re.DOTALL)
        self.assertIsNotNone(commands)
        self.assertIn('"campaigns"', commands.group(1))
        self.assertIn('"data"', commands.group(1))

        help_dispatch = script.index('if ($Command -eq "help")')
        campaign_dispatch = script.index('if ($Command -eq "campaigns")')
        self.assertLess(help_dispatch, campaign_dispatch)
        campaign = script_section(
            script, 'if ($Command -eq "campaigns")', 'if ($Command -eq "lint")'
        )
        self.assertIn('$CampaignAction -eq "start"', campaign)
        self.assertIn('$CampaignAction -eq "record"', campaign)
        self.assertIn('$CampaignHelp', campaign)
        self.assertIn("$CampaignGlobalArgs", campaign)
        self.assertIn("$CampaignFinalizing", campaign)
        self.assertIn('$CommandArgs -contains "--help"', campaign)
        self.assertIn('@CampaignGlobalArgs "set-baseline" "--quiet"', campaign)
        self.assertIn("The campaign baseline setup failed.", campaign)
        self.assertIn("decomp-current-report.json", campaign)
        self.assertIn("decomp-current-data-report.json", campaign)

        baseline = script_section(
            script, "function Save-Baseline", "function Stamp-FirstScore"
        )
        self.assertIn("decomp-baseline-data-report.json", baseline)
        self.assertIn("Update-FunctionSizes", baseline)
        size_update = script_section(
            script, "function Test-FunctionSizes", "function Save-Baseline"
        )
        self.assertIn("_read_snapshot_sizes", size_update)
        self.assertIn("No valid retail function-size snapshot", size_update)

        help_dispatch = script_section(
            script,
            'if (($CommandArgs -contains "--help"',
            'if ($Command -eq "lint")',
        )
        for command in ("baseline", "score", "bc", "data"):
            self.assertIn(f'"{command}"', help_dispatch)

    def test_windows_score_paths_build_and_stamp_after_success(self):
        script = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        score = script_section(script, '    "score" {', '    "bc" {')
        self.assertLess(
            score.index("Build-Project"), score.index("Write-ComparisonReport")
        )
        self.assertLess(
            score.index("decomp_verify.py"), score.index("Stamp-FirstScore")
        )
        self.assertIn("$ScoreTargets", score)
        self.assertIn("^0x[0-9A-Fa-f]{1,8}$", score)

        bc = script_section(script, '    "bc" {', '    "baseline" {')
        self.assertLess(bc.index("Build-Project"), bc.index("reccmp-reccmp"))
        self.assertLess(bc.index("decomp_diff.py"), bc.index("Stamp-FirstScore"))
        for argument in (
            '"--address"',
            '"--functions-map"',
            '"--function-sizes"',
            '"--source-root"',
        ):
            self.assertIn(argument, bc)

        experiment = script_section(script, '    "experiment" {', '    "report" {')
        trial = experiment[experiment.index('$Action -eq "try"') :]
        self.assertLess(
            trial.index("Build-Project"), trial.index("Write-ComparisonReport")
        )
        self.assertLess(
            trial.index("decomp_verify.py"), trial.index("Stamp-FirstScore")
        )

        data = script_section(script, '    "data" {', '    "progress" {')
        self.assertLess(data.index("Build-Project"), data.index("Write-DataReport"))
        self.assertLess(data.index("decomp_data.py"), data.index("Stamp-FirstScore"))

    def test_windows_validate_has_campaign_mode_parity(self):
        script = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        validate = script_section(script, '    "validate" {', '    "experiment" {')
        for argument in (
            '"--target"',
            '"--mode"',
            '"--allow-target-regression"',
            '"--meta-resolution"',
            '"--staged"',
            '"--accounting-correction"',
            '"--baseline-data"',
            '"--current-data"',
        ):
            self.assertIn(argument, validate)
        self.assertIn("decomp-baseline-data-report.json", validate)
        self.assertIn("decomp-current-data-report.json", validate)
        self.assertIn("Write-DataReport", validate)
        self.assertIn("git diff --cached --name-only -- src", validate)
        self.assertIn("A staged data campaign must change the source tree.", validate)
        self.assertIn('$VerifyArgs += "--allow-target-regression"', validate)
        self.assertIn('$VerifyArgs += "--meta-resolution"', validate)
        self.assertIn('$VerifyArgs += "--staged"', validate)
        self.assertIn('$VerifyArgs += @("--mode", $Mode)', validate)
        self.assertIn(
            '$VerifyArgs += @("--accounting-correction", $AccountingCorrection)',
            validate,
        )
        self.assertLess(
            validate.index("Build-Project"), validate.index("Write-ComparisonReport")
        )
        self.assertLess(
            validate.index("decomp_verify.py"), validate.index("decomp_lint.py")
        )
        self.assertLess(
            validate.index("git diff --check"), validate.index("Stamp-FirstScore")
        )

    def test_windows_report_preserves_function_size_snapshot(self):
        script = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        report = script_section(
            script, "function New-DecompReport", "function Show-Help"
        )
        self.assertIn("Update-FunctionSizes", report)
        self.assertNotIn("ghidra function list", report)
        self.assertNotIn("Set-Content", report)

    def test_normative_inputs_are_present_tracked_and_not_ignored(self):
        for name in NORMATIVE_INPUTS:
            with self.subTest(name=name):
                self.assertTrue((ROOT / name).is_file(), name)
                tracked = subprocess.run(
                    ["git", "ls-files", "--error-unmatch", "--", name],
                    cwd=ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(tracked.returncode, 0, f"{name} is not tracked")
                result = subprocess.run(
                    ["git", "check-ignore", "--quiet", "--", name],
                    cwd=ROOT,
                    check=False,
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
