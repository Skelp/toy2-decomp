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
    def test_resource_validation_has_linux_and_windows_parity(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        for script in (linux, windows):
            self.assertIn("--resource", script)
            self.assertIn("resource-score", script)
            self.assertIn("resource campaign cannot have target addresses", script.casefold())

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

        experiment = script_section(script, "experiment() {", "report() {")
        self.assertIn("baseline-report.json", experiment)
        self.assertIn("baseline-data-report.json", experiment)
        self.assertIn('$directory/compiler-context.json', experiment)
        self.assertNotIn("build/decomp-baseline-report.json", experiment)
        self.assertNotIn("build/decomp-baseline-meta.json", experiment)

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
        self.assertIn('Join-Path $Directory "baseline-report.json"', experiment)
        self.assertIn('Join-Path $Directory "baseline-data-report.json"', experiment)
        self.assertNotIn(r"build\decomp-baseline-report.json", experiment)
        self.assertNotIn(r"build\decomp-baseline-meta.json", experiment)
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


CONTRACT_LINE_LIMITS = {"AGENTS.md": 160, "docs/decomp-agent.md": 40}
SKILL_LINE_LIMIT = 100
USAGE_BODY_LINE_LIMIT = 80
TOOL_MODULE_LINE_BUDGET = 20000
TEST_LINE_BUDGET = 10000
TOOL_MODULES = {
    "annotations",
    "attempts",
    "binary",
    "candidates",
    "campaigns",
    "data",
    "dependencies",
    "diff",
    "discover",
    "evidence",
    "lint",
    "notes",
    "original_names",
    "resources",
    "similar",
    "status",
    "throughput",
    "verify",
}
CEREMONY_WORDS = (
    "receipt",
    "doctor",
    "brief",
    "forecast",
    "deadline",
    "cohort",
    "lane",
    "fork_turns",
    "spawn_agent",
    "followup_task",
    "interrupt_agent",
)
CEREMONY_PATTERN = re.compile(r"\b(" + "|".join(CEREMONY_WORDS) + r")\b", re.IGNORECASE)
COMMAND_MENTION = re.compile(r"tools/decomp ([a-z][a-z0-9-]*)")
CASE_LABEL = re.compile(r'^\s*([\w"|-]+)\)\s*$', re.MULTILINE)
EXPERIMENT_COLUMNS = 8
EXPERIMENT_OUTCOMES = {"PENDING", "KEEP", "REVERTED"}


def line_count(path: Path) -> int:
    return len(path.read_text(encoding="utf-8").splitlines())


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def skill_files() -> list[Path]:
    return sorted((ROOT / ".agents" / "skills").glob("*/SKILL.md"))


def contract_files() -> list[Path]:
    """AGENTS.md, CLAUDE.md when present, and every skill: the agent contract."""
    files = [ROOT / "AGENTS.md"]
    if (ROOT / "CLAUDE.md").is_file():
        files.append(ROOT / "CLAUDE.md")
    return files + skill_files()


def usage_body(script: str) -> str | None:
    match = re.search(r"^usage\(\) \{\n(.*?)^\}", script, re.MULTILINE | re.DOTALL)
    return None if match is None else match.group(1)


def wrapper_case_labels(script: str) -> set[str]:
    """Every NAME that has a ``NAME)`` case label in tools/decomp."""
    labels: set[str] = set()
    for match in CASE_LABEL.finditer(script):
        labels.update(label.strip('"') for label in match.group(1).split("|"))
    return labels


def first_lines(text: str, pattern: re.Pattern) -> dict[str, int]:
    """Map each distinct match (lower-cased) to the first line it appears on."""
    found: dict[str, int] = {}
    for number, line in enumerate(text.splitlines(), 1):
        for word in pattern.findall(line):
            found.setdefault(word.lower(), number)
    return found


def read_experiment_table(path: Path) -> tuple[list[str], list[tuple[int, list[str]]]]:
    """Return the header columns and the (line number, cells) data rows.

    The header is the first line naming an ``outcome`` column, whether it is a
    ``# ...`` comment (the repository's TSV convention) or a plain first row.
    """
    header: list[str] = []
    rows: list[tuple[int, list[str]]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        comment = line.startswith("#")
        cells = [cell.strip() for cell in line.lstrip("#").strip().split("\t")]
        if not header and "outcome" in [cell.lower() for cell in cells]:
            header = [cell.lower() for cell in cells]
        elif not comment:
            rows.append((number, line.split("\t")))
    return header, rows


class AntiRegrowthTests(unittest.TestCase):
    """The agent contract and the tooling must stay small and honest."""

    def test_contract_size_limits(self):
        for name, limit in CONTRACT_LINE_LIMITS.items():
            with self.subTest(file=name):
                count = line_count(ROOT / name)
                self.assertLessEqual(count, limit, f"{name} is {count} lines; limit {limit}")
        for skill in skill_files():
            with self.subTest(file=relative(skill)):
                count = line_count(skill)
                self.assertLessEqual(
                    count, SKILL_LINE_LIMIT, f"{relative(skill)} is {count} lines; limit {SKILL_LINE_LIMIT}"
                )
        with self.subTest(file="tools/decomp usage()"):
            body = usage_body((ROOT / "tools" / "decomp").read_text(encoding="utf-8"))
            self.assertIsNotNone(body, "tools/decomp has no usage() function")
            count = len(body.splitlines())
            self.assertLessEqual(
                count, USAGE_BODY_LINE_LIMIT, f"usage() body is {count} lines; limit {USAGE_BODY_LINE_LIMIT}"
            )
        budgets = (
            ("tools/decomp_*.py", TOOL_MODULE_LINE_BUDGET),
            ("tools/tests/*.py", TEST_LINE_BUDGET),
        )
        for pattern, budget in budgets:
            with self.subTest(file=pattern):
                total = sum(line_count(path) for path in ROOT.glob(pattern))
                self.assertLessEqual(total, budget, f"{pattern} totals {total} lines; budget {budget}")

    def test_tool_module_allowlist(self):
        modules = {path.stem.removeprefix("decomp_") for path in (ROOT / "tools").glob("decomp_*.py")}
        self.assertTrue(modules)
        self.assertLessEqual(
            modules,
            TOOL_MODULES,
            f"tools/decomp_*.py modules outside the allowlist: {sorted(modules - TOOL_MODULES)}",
        )

    def test_contract_has_no_ceremony_vocabulary(self):
        for path in contract_files():
            with self.subTest(file=relative(path)):
                hits = first_lines(path.read_text(encoding="utf-8"), CEREMONY_PATTERN)
                self.assertEqual(
                    hits, {}, f"{relative(path)} uses ceremony vocabulary (word: first line): {hits}"
                )

    def test_contract_names_only_existing_commands(self):
        labels = wrapper_case_labels((ROOT / "tools" / "decomp").read_text(encoding="utf-8"))
        self.assertIn("help", labels, "could not parse the case labels of tools/decomp")
        for path in contract_files():
            with self.subTest(file=relative(path)):
                mentioned = first_lines(path.read_text(encoding="utf-8"), COMMAND_MENTION)
                unknown = {name: line for name, line in mentioned.items() if name not in labels}
                self.assertEqual(
                    unknown,
                    {},
                    f"{relative(path)} names commands tools/decomp lacks (command: first line): {unknown}",
                )

    def test_tooling_experiments_rows_are_well_formed(self):
        path = ROOT / "tools" / "Resources" / "tooling-experiments.tsv"
        if not path.is_file():
            self.skipTest(f"{relative(path)} is not present")
        header, rows = read_experiment_table(path)
        self.assertIn("outcome", header, f"{relative(path)} needs a header row naming the outcome column")
        self.assertEqual(
            len(header), EXPERIMENT_COLUMNS, f"{relative(path)} header has {len(header)} columns; expected 8"
        )
        outcome = header.index("outcome")
        for number, cells in rows:
            with self.subTest(line=number):
                self.assertEqual(
                    len(cells), EXPERIMENT_COLUMNS, f"line {number} has {len(cells)} columns; expected 8"
                )
                self.assertIn(
                    cells[outcome],
                    EXPERIMENT_OUTCOMES,
                    f"line {number} outcome {cells[outcome]!r} is not one of {sorted(EXPERIMENT_OUTCOMES)}",
                )


if __name__ == "__main__":
    unittest.main()
