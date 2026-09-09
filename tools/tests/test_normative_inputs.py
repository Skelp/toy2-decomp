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
            "--data-report build/decomp-baseline-data-report.json || return",
            "revision=$(git rev-parse --short HEAD) || return",
        ):
            self.assertIn(command, baseline)
        self.assertNotIn("refresh_function_sizes", baseline)
        size_refresh = script_section(
            script, "function_sizes_are_valid() {", "baseline() {"
        )
        self.assertIn("_read_snapshot_sizes", size_refresh)
        self.assertIn("if ! function_sizes_are_valid", size_refresh)

        campaigns = script_section(script, "    campaigns)", "    defer)")
        self.assertIn("campaign_global_arguments", campaigns)
        self.assertNotIn("tools/decomp_doctor.py", campaigns)
        self.assertIn("campaign-progress-before.json", campaigns)
        self.assertIn('"${campaign_global_arguments[@]}" set-baseline', campaigns)
        self.assertIn('"${campaign_global_arguments[@]}" set-meta-baseline', campaigns)
        self.assertIn('--progress "$campaign_progress" --quiet', campaigns)
        self.assertIn('"${campaign_global_arguments[@]}" abort', campaigns)
        self.assertIn('campaign_arguments=("$@")', campaigns)
        self.assertIn('campaign_mode != meta', campaigns)
        self.assertIn('coverage) campaign_lane="research"', campaigns)
        self.assertIn('refinement) campaign_lane="production"', campaigns)
        self.assertIn("campaign_briefs=()", campaigns)
        self.assertIn("explicit --doctor-receipt", campaigns)
        self.assertIn("one --brief for each ordered target", campaigns)
        start = campaigns[campaigns.index("            start)") :]
        self.assertLess(
            start.index("explicit --doctor-receipt"),
            start.index('tools/decomp_campaigns.py "${campaign_arguments[@]}"'),
        )
        self.assertLess(
            start.index("one --brief for each ordered target"),
            start.index("campaign_progress="),
        )
        record = start[start.index("            record)") :]
        self.assertNotIn("write_comparison_report", record)
        self.assertNotIn("write_data_report", record)

        experiment = script_section(script, "experiment() {", "report() {")
        self.assertIn("decomp_experiment.py begin-session", experiment)
        self.assertIn("decomp_experiment.py attach-baseline", experiment)
        self.assertIn('write_comparison_report "$baseline_report"', experiment)
        self.assertIn('write_data_report "$baseline_data_report"', experiment)
        self.assertNotIn("build/decomp-baseline-report.json", experiment)
        self.assertNotIn("build/decomp-baseline-meta.json", experiment)

    def test_experiment_controller_has_linux_and_windows_parity(self):
        linux = script_section(
            (ROOT / "tools/decomp").read_text(encoding="utf-8"),
            "experiment() {",
            "report() {",
        )
        windows = script_section(
            (ROOT / "tools/decomp.ps1").read_text(encoding="utf-8"),
            '    "experiment" {',
            '    "report" {',
        )
        for script in (linux, windows):
            for operation in (
                "begin-session",
                "attach-baseline",
                "reserve",
                "fail",
                "record",
                "status",
                "best",
                "advise",
                "report",
            ):
                self.assertIn(operation, script)
            self.assertIn("seal-diff", script)
            self.assertIn("build.lock", (ROOT / "tools/decomp_experiment.py").read_text(encoding="utf-8"))

        linux_trial = linux[linux.index("        try)") :]
        windows_trial = windows[windows.index('$Action -eq "try"') :]
        self.assertLess(linux_trial.index(" reserve "), linux_trial.index("build_game"))
        self.assertLess(
            windows_trial.index(" reserve "), windows_trial.index("Build-Project")
        )
        self.assertIn("flock 9", linux)
        self.assertNotIn("if ! build_game", linux)
        self.assertIn("if build_game; then", linux)
        self.assertIn("[IO.FileShare]::None", windows)
        self.assertNotIn("decomp-experiment-$Address-$Label.json", windows)

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
        self.assertIn('$CommandArgs -contains "--help"', campaign)
        self.assertNotIn("decomp_doctor.py", campaign)
        self.assertIn("campaign-progress-before.json", campaign)
        self.assertIn('@CampaignGlobalArgs "set-baseline"', campaign)
        self.assertIn('@CampaignGlobalArgs "set-meta-baseline"', campaign)
        self.assertIn('"--progress" $ProgressPath "--quiet"', campaign)
        self.assertIn("The campaign baseline setup failed.", campaign)
        self.assertIn('$CampaignMode -ne "meta"', campaign)
        self.assertIn('"coverage" { "research" }', campaign)
        self.assertIn('"refinement" { "production" }', campaign)
        self.assertIn("$CommandArgs += @(\"--progress-before\", $ProgressPath)", campaign)
        self.assertIn("$BriefPaths = @()", campaign)
        self.assertIn("explicit --doctor-receipt", campaign)
        self.assertIn("one --brief for each ordered target", campaign)
        self.assertLess(
            campaign.index("one --brief for each ordered target"),
            campaign.index("$CacheDirectory ="),
        )
        record = campaign[campaign.index('$CampaignAction -eq "record"') :]
        self.assertNotIn("Build-Project", record)
        self.assertNotIn("Write-ComparisonReport", record)
        self.assertNotIn("Write-DataReport", record)

        baseline = script_section(
            script, "function Save-Baseline", "function Stamp-FirstScore"
        )
        self.assertIn("decomp-baseline-data-report.json", baseline)
        self.assertNotIn("Update-FunctionSizes", baseline)
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

    def test_preflight_brief_and_finalize_have_wrapper_parity(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        for command in ("doctor", "brief", "context", "finalize"):
            self.assertIn(f"  {command}", linux)
            self.assertIn(f"{command}", windows)
        for script_name in (
            "tools/decomp_doctor.py",
            "tools/decomp_brief.py",
            "tools/decomp_context.py",
            "tools/decomp_campaigns.py finalize",
        ):
            self.assertIn(script_name, linux)
        self.assertIn('if ($Command -eq "context")', windows)
        self.assertIn('$Command -eq "finalize"', windows)
        self.assertIn('"tools\\decomp_campaigns.py") finalize', windows)

        linux_help = script_section(linux, "usage() {", 'command="${1:-}"')
        windows_help = script_section(
            windows, "function Show-Help", "Set-Location $Root"
        )
        for help_text in (linux_help, windows_help):
            for token in (
                "--doctor-receipt",
                "--brief",
                "--prediction-version",
                "--prediction-lower-bound-bytes",
                "--prediction-features",
                "--prediction-features-out",
                "--prediction-handoff",
                "--scout-report",
                "--expected-retained-bytes MEDIAN_BYTES",
                "--mode meta --lane meta",
                "--result meta-fix --mode meta --staged",
                "finalize --result source --mode resource",
            ):
                self.assertIn(token, help_text)
            self.assertIn("success_probability", help_text)
            self.assertIn("cohort_sample_size", help_text)
            self.assertIn("median_retained_bytes", help_text)
            self.assertIn("median_minutes", help_text)
            self.assertIn("retail-abi-control-flow-evidence", help_text)
            self.assertIn(
                "callers-types-layout-translation-unit-analogue", help_text
            )
            self.assertIn("expires 60 minutes", help_text)
            self.assertIn("Add --json only when candidate output", help_text)
            self.assertIn("rejects a HEAD change after the doctor", help_text)
            self.assertIn("refinement for closure and production", help_text)
            self.assertIn("coverage for research", help_text)
            self.assertNotIn("validate --mode resource", help_text)

            source_preflight = help_text[
                help_text.index("--lane closure --limit 1") :
                help_text.index("The closure and production features JSON")
            ]
            normalized_source_preflight = source_preflight.replace("\\", "/")
            self.assertEqual(source_preflight.count("--scout-report"), 2)
            self.assertIn(
                "--prediction-features-out build/decomp-cache/prediction-handoff.json",
                normalized_source_preflight,
            )
            self.assertIn(
                "--prediction-handoff build/decomp-cache/prediction-handoff.json",
                normalized_source_preflight,
            )
            self.assertNotIn("--expected-retained-bytes", source_preflight)

            meta = help_text[
                help_text.index("A meta campaign is one bounded") :
                help_text.index("Record", help_text.index("A meta campaign is one bounded"))
            ]
            self.assertIn("--mode meta --lane meta", meta)
            self.assertIn("--result meta-fix --mode meta --staged", meta)
            self.assertNotIn(" doctor --mode", meta)
            self.assertIn("no forecast", meta)

            resource = help_text[
                help_text.index("Resource work uses the same preflight") :
                help_text.index("For no-source")
            ]
            self.assertIn("doctor --mode resource --lane resource", resource)
            self.assertEqual(resource.count("--scout-report"), 2)
            self.assertIn("finalize --result source --mode resource", resource)

            no_source = help_text[
                help_text.index("For no-source") :
                help_text.index("A meta campaign is one bounded")
            ]
            self.assertIn("finalize --result no-source --mode MODE", no_source)
            self.assertIn("--target ADDRESS --staged", no_source)
        linux_sequence = (
            linux_help.index("integrate origin/agent/continuous"),
            linux_help.index("--lane closure --limit 1"),
            linux_help.index("tools/decomp doctor --mode"),
            linux_help.index("tools/decomp brief --lane"),
            linux_help.index("tools/decomp campaigns start --mode refinement"),
        )
        windows_sequence = (
            windows_help.index("integrate origin/agent/continuous"),
            windows_help.index("--lane closure --limit 1"),
            windows_help.index("tools/decomp.ps1 doctor --mode"),
            windows_help.index("tools/decomp.ps1 brief --lane"),
            windows_help.index(
                "tools/decomp.ps1 campaigns start --mode refinement"
            ),
        )
        self.assertEqual(linux_sequence, tuple(sorted(linux_sequence)))
        self.assertEqual(windows_sequence, tuple(sorted(windows_sequence)))

    def test_delivery_contract_has_wrapper_help_parity(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        linux_help = script_section(linux, "usage() {", 'command="${1:-}"')
        windows_help = script_section(
            windows, "function Show-Help", "Set-Location $Root"
        )
        for help_text in (linux_help, windows_help):
            flat_help = " ".join(help_text.split())
            offsets = [
                help_text.index(f"--status {status}")
                for status in (
                    "staged",
                    "accepted",
                    "integrated",
                    "committed",
                    "pushed",
                )
            ]
            self.assertEqual(offsets, sorted(offsets))
            self.assertIn("campaigns delivery-verify", help_text)
            self.assertEqual(help_text.count("--delivery-receipt"), 3)
            self.assertIn("telemetry-only follow-up commit", flat_help)
            self.assertIn("campaign row", flat_help)
            self.assertIn("staged entry", flat_help)
            self.assertIn("accepted entry", flat_help)
            self.assertIn("committed, and pushed", flat_help)
            self.assertIn("does not count as source progress", flat_help)
            self.assertIn("rejected entry ends delivery", flat_help)
            for token in (
                "impact template",
                "impact seal-review",
                "impact verify-review",
                "--review-report build/decomp-cache/review-sealed.json",
                "first reviewed acceptance rejects a missing report",
                "exact accepted retry can omit the report",
                "Legacy, no-source, data, resource, and meta",
            ):
                self.assertIn(token, flat_help)

        self.assertIn("COMMIT=$(git rev-parse HEAD)", linux_help)
        self.assertIn("BASE=$(git rev-parse HEAD^)", linux_help)
        self.assertIn("DELIVERY_RECEIPT=$(", linux_help)
        self.assertIn(
            '--commit "$COMMIT" --base-commit "$BASE"', linux_help
        )
        self.assertEqual(linux_help.count('--commit "$COMMIT"'), 4)
        self.assertEqual(
            linux_help.count('--delivery-receipt "$DELIVERY_RECEIPT"'), 3
        )
        self.assertIn('json.load(sys.stdin)["path"]', linux_help)

        self.assertIn("`$COMMIT = git rev-parse HEAD", windows_help)
        self.assertIn("`$BASE = git rev-parse HEAD^", windows_help)
        self.assertIn("`$DELIVERY_RECEIPT = (", windows_help)
        self.assertIn(
            "--commit `$COMMIT --base-commit `$BASE", windows_help
        )
        self.assertEqual(windows_help.count("--commit `$COMMIT"), 4)
        self.assertEqual(
            windows_help.count("--delivery-receipt `$DELIVERY_RECEIPT"), 3
        )
        self.assertIn("ConvertFrom-Json).path", windows_help)
        self.assertEqual(
            windows_help.count("Set-Content -Encoding ASCII"), 2
        )
        self.assertNotIn("> build/decomp-cache/review-", windows_help)

    def test_pivot_help_requires_fresh_preflight_artifacts(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        linux_help = script_section(linux, "usage() {", 'command="${1:-}"')
        windows_help = script_section(
            windows, "function Show-Help", "Set-Location $Root"
        )
        for help_text in (linux_help, windows_help):
            pivot = help_text[help_text.index("Before a pivot") :]
            flat_pivot = " ".join(pivot.split())
            normalized_pivot = pivot.replace("\\", "/")
            self.assertIn("--replace OLD_ADDRESS", pivot)
            self.assertIn("--doctor-receipt PIVOT_RECEIPT", pivot)
            self.assertIn("--brief PIVOT_BRIEF", pivot)
            self.assertIn("candidates --lane production --limit 1", pivot)
            self.assertIn(
                "doctor --mode refinement --lane production", flat_pivot
            )
            self.assertIn("brief --lane production", flat_pivot)
            self.assertIn(
                "--prediction-features-out build/decomp-cache/prediction-handoff.json",
                normalized_pivot,
            )
            self.assertIn(
                "--prediction-handoff build/decomp-cache/prediction-handoff.json",
                normalized_pivot,
            )
            self.assertEqual(pivot.count("--scout-report"), 2)
            self.assertIn("only the new address", flat_pivot)
            self.assertIn("--selection-started-at PIVOT_SELECTION_UTC", help_text)
            self.assertIn("family expansion omits --replace", help_text.casefold())
            self.assertIn(
                "prediction events preserve each target forecast",
                flat_pivot.casefold(),
            )
            self.assertIn("deadlines do not reset", flat_pivot)

    def test_wrappers_bound_ghidra_state_and_prune_logs(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        linux_environment = (ROOT / "tools" / "linux-decomp-env.sh").read_text(
            encoding="utf-8"
        )
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        for name in (
            "GHIDRA_CLI_CACHE_DIR",
            "GHIDRA_CLI_STATE_DIR",
            "GHIDRA_CLI_LOG_DIR",
            "GHIDRA_CLI_FULL_RESPONSE_LOGGING",
        ):
            self.assertIn(name, linux_environment)
            self.assertIn(name, windows)
        self.assertIn(
            '${XDG_DATA_HOME:-$HOME/.local/share}', linux_environment
        )
        self.assertLess(
            linux_environment.index("TOY2_GHIDRA_LIVE_DATA_HOME"),
            linux_environment.index('export XDG_DATA_HOME="$TOY2_GHIDRA_STATE"'),
        )
        self.assertIn("[Environment+SpecialFolder]::ApplicationData", windows)
        self.assertLess(
            windows.index("$LiveGhidraDataHome ="),
            windows.index("$env:XDG_DATA_HOME = $GhidraState"),
        )
        self.assertIn("trap prune_ghidra_logs EXIT", linux)
        for command in (
            "decomp_doctor.py",
            "decomp_brief.py",
            "decomp_campaigns.py finalize",
        ):
            self.assertNotIn(f"exec python tools/{command}", linux)

        function_sizes = script_section(
            windows, "function Update-FunctionSizes", "function Save-Baseline"
        )
        self.assertGreaterEqual(function_sizes.count("Prune-GhidraLogs"), 2)
        self.assertIn("finally", function_sizes)
        preflight = script_section(
            windows,
            'if ($Command -in @("doctor", "brief"))',
            'if ($Command -eq "finalize")',
        )
        self.assertGreaterEqual(preflight.count("Prune-GhidraLogs"), 2)
        self.assertIn("finally", preflight)

    def test_windows_context_dispatch_is_standalone_and_read_only(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        context_dispatch = script_section(
            windows,
            'if ($Command -eq "context")',
            '$Vcvars = Join-Path $MsvcBase',
        )

        self.assertIn('"tools\\decomp_context.py"', context_dispatch)
        self.assertIn("$VenvPython", context_dispatch)
        self.assertIn("Set-Location $Root", context_dispatch)
        self.assertIn("@CommandArgs", context_dispatch)
        self.assertIn("exit $LASTEXITCODE", context_dispatch)
        self.assertEqual(windows.count('if ($Command -eq "context")'), 1)
        self.assertNotIn("Import-VC6Environment", context_dispatch)
        self.assertNotIn("Prune-GhidraLogs", context_dispatch)
        self.assertNotIn("New-Item", context_dispatch)
        self.assertLess(
            windows.index('if ($Command -eq "context")'),
            windows.index("New-Item -ItemType Directory"),
        )
        self.assertNotIn('"context"', windows[windows.index('if ($Command -in @("doctor", "brief"))'):])
        linux_dispatch = script_section(
            linux,
            'case "$command" in',
            "source tools/linux-decomp-env.sh",
        )
        self.assertIn("context)", linux_dispatch)
        self.assertIn('exec "$ROOT/.tooling/venv/bin/python"', linux_dispatch)
        self.assertEqual(linux.count("context)"), 1)
        self.assertLess(
            linux.index("context)"), linux.index("source tools/linux-decomp-env.sh")
        )
        self.assertLess(linux.index("context)"), linux.index("trap prune_ghidra_logs EXIT"))

    def test_report_help_exits_before_toolchain_setup(self):
        linux = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        windows = (ROOT / "tools" / "decomp.ps1").read_text(encoding="utf-8")
        early_linux = script_section(linux, 'case "$command" in', "source tools/linux-decomp-env.sh")
        self.assertIn("report)", early_linux)
        self.assertIn("Usage: tools/decomp report [output.html]", early_linux)
        early_windows = script_section(
            windows,
            'if (($CommandArgs -contains "--help"',
            'if ($Command -eq "lint")',
        )
        self.assertIn('"report"', early_windows)
        self.assertIn("Usage: tools/decomp.ps1 report [output.html]", early_windows)

    def test_linux_score_paths_bind_generated_artifacts(self):
        script = (ROOT / "tools" / "decomp").read_text(encoding="utf-8")
        score = script_section(script, "score() {", "ensure_build() {")
        self.assertLess(
            score.index("write_comparison_report"), score.index("first-score")
        )
        self.assertIn("--report build/decomp-score-report.json", score)
        for argument in (
            "--score-functions-map",
            "--score-function-sizes",
            "--score-source-root",
        ):
            self.assertIn(argument, score)

        bc = script_section(script, "    bc)", "    baseline)")
        self.assertLess(bc.index("reccmp-reccmp"), bc.index("first-score"))
        self.assertIn('--diff "$diff_path"', bc)
        for argument in (
            "--score-functions-map",
            "--score-function-sizes",
            "--score-source-root",
        ):
            self.assertIn(argument, bc)

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
        self.assertIn(
            "Stamp-FirstScore -Addresses $ScoreTargets -Report $Report", score
        )

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
        self.assertIn("Stamp-FirstScore", bc)
        self.assertIn("-Diff $Diff", bc)

        experiment = script_section(script, '    "experiment" {', '    "report" {')
        self.assertIn("begin-session $Address", experiment)
        self.assertIn("attach-baseline $Address", experiment)
        self.assertIn("Write-ComparisonReport $BaselineReport", experiment)
        self.assertIn("Write-DataReport $BaselineDataReport", experiment)
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


if __name__ == "__main__":
    unittest.main()
