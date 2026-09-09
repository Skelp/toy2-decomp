[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("configure", "build", "compare", "score", "bc", "candidates", "doctor", "brief", "context", "impact", "campaigns", "finalize", "discover", "evidence", "notes", "names", "defer", "undefer", "blockers", "baseline", "validate", "experiment", "lint", "data", "report", "session-summary", "progress", "check", "sync", "run", "shell", "help")]
    [string] $Command = "help",

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CommandArgs = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Tooling = Join-Path $Root ".tooling"
$MsvcBase = Join-Path $Tooling "msvc600-8168"
$VenvScripts = Join-Path $Tooling "venv\Scripts"
$VenvPython = Join-Path $VenvScripts "python.exe"
if ($Command -eq "context") {
    Set-Location $Root
    & $VenvPython (Join-Path $Root "tools\decomp_context.py") @CommandArgs
    exit $LASTEXITCODE
}
if ($Command -eq "impact") {
    Set-Location $Root
    & $VenvPython -m tools.decomp_impact @CommandArgs
    exit $LASTEXITCODE
}
$Vcvars = Join-Path $MsvcBase "VC98\Bin\VCVARS32.BAT"
$DecompCache = Join-Path $Root "build\decomp-cache"
$GhidraCache = Join-Path $DecompCache "ghidra\cache"
$GhidraState = Join-Path $DecompCache "ghidra\state"
$LiveGhidraDataHome = $env:TOY2_GHIDRA_LIVE_DATA_HOME
if (-not $LiveGhidraDataHome) { $LiveGhidraDataHome = $env:XDG_DATA_HOME }
if (-not $LiveGhidraDataHome) {
    $LiveGhidraDataHome = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ApplicationData
    )
}
New-Item -ItemType Directory -Force $GhidraCache, $GhidraState | Out-Null
$env:XDG_CACHE_HOME = $GhidraCache
$env:XDG_DATA_HOME = $GhidraState
$env:GHIDRA_CLI_CACHE_DIR = $GhidraCache
$env:GHIDRA_CLI_STATE_DIR = $GhidraState
$env:GHIDRA_CLI_LOG_DIR = $GhidraCache
$env:GHIDRA_CLI_FULL_RESPONSE_LOGGING = "0"
$env:TOY2_GHIDRA_LIVE_DATA_HOME = $LiveGhidraDataHome
$env:RUST_LOG = if ($env:RUST_LOG) { $env:RUST_LOG } else { "warn" }

function Prune-GhidraLogs {
    & $VenvPython -c `
        "from pathlib import Path; from tools.decomp_doctor import prune_ghidra_logs; prune_ghidra_logs(Path.cwd())" `
        *> $null
}

function Assert-LastExit([string] $Action) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Action failed with exit code $LASTEXITCODE"
    }
}

function Import-VC6Environment {
    if (-not (Test-Path $Vcvars)) {
        throw "VC6 toolchain is missing. Run tools/setup-windows-decomp.ps1 first."
    }
    $EnvironmentLines = & $env:ComSpec /d /s /c "`"$Vcvars`" >nul && set"
    Assert-LastExit "Activating VC6"
    foreach ($Line in $EnvironmentLines) {
        $Separator = $Line.IndexOf("=")
        if ($Separator -gt 0) {
            [Environment]::SetEnvironmentVariable(
                $Line.Substring(0, $Separator),
                $Line.Substring($Separator + 1),
                "Process"
            )
        }
    }
    $env:Path = "$VenvScripts;$env:Path"
}

function Configure-Project {
    & cmake -S $Root -B (Join-Path $Root "build") -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo
    Assert-LastExit "Configuring project"
}

function Build-Project {
    if (-not (Test-Path (Join-Path $Root "build\Makefile"))) {
        Configure-Project
    }
    & cmake --build (Join-Path $Root "build") -- -NOLOGO
    Assert-LastExit "Building project"
    Push-Location (Join-Path $Root "build")
    try {
        & reccmp-project detect --what recompiled
        Assert-LastExit "Registering recompiled executable"
    } finally {
        Pop-Location
    }
}

function Ensure-Build {
    Build-Project
}

function Write-ComparisonReport([string] $Output) {
    $ReportPath = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $Root "build\$Output" }
    $InputReceipt = Join-Path $DecompCache "provenance\report-input-$PID-$([guid]::NewGuid().ToString('N')).json"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") capture-input $InputReceipt --output $ReportPath | Out-Null
    Assert-LastExit "Capturing comparison inputs"
    Push-Location (Join-Path $Root "build")
    try {
        & reccmp-reccmp --target TOY2 --silent --no-color --json $Output | Out-Null
        Assert-LastExit "Comparing binaries"
    } finally {
        Pop-Location
    }
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-report $ReportPath --input-receipt $InputReceipt | Out-Null
    Assert-LastExit "Sealing comparison provenance"
    Remove-Item -Force $InputReceipt -ErrorAction SilentlyContinue
}

function Write-DataReport([string] $Output) {
    $ReportPath = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $Root "build\$Output" }
    $InputReceipt = Join-Path $DecompCache "provenance\data-input-$PID-$([guid]::NewGuid().ToString('N')).json"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") capture-input $InputReceipt --output $ReportPath | Out-Null
    Assert-LastExit "Capturing data-report inputs"
    & $VenvPython (Join-Path $Root "tools\generate-decomp-data-report.py") `
        --original (Join-Path $Root "original\toy2.exe") `
        --recompiled (Join-Path $Root "build\toy2.exe") `
        --pdb (Join-Path $Root "build\toy2.pdb") `
        --source-root (Join-Path $Root "src") `
        --output $ReportPath
    Assert-LastExit "Comparing global data"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-report $ReportPath --input-receipt $InputReceipt | Out-Null
    Assert-LastExit "Sealing data-report provenance"
    Remove-Item -Force $InputReceipt -ErrorAction SilentlyContinue
}

function Ensure-CandidateReport {
    $Report = Join-Path $Root "build\decomp-current-report.json"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") validate-report $Report *> $null
    if ($LASTEXITCODE -eq 0) { return }
    Import-VC6Environment
    Build-Project
    Update-FunctionSizes
    Write-ComparisonReport $Report
}

function Test-FunctionSizes([string] $Path) {
    & $VenvPython -c `
        "import sys; from pathlib import Path; from tools.ghidra_sync import _read_snapshot_sizes; _read_snapshot_sizes(Path(sys.argv[1]))" `
        $Path *> $null
    return $LASTEXITCODE -eq 0
}

function Update-FunctionSizes {
    $FunctionSizes = Join-Path $Root "build\decomp-function-sizes.json"
    $TemporarySizes = "$FunctionSizes.tmp"
    if (Get-Command ghidra -ErrorAction SilentlyContinue) {
        Prune-GhidraLogs
        try {
            & ghidra function list --json --limit 0 --fields address,size |
                Set-Content -Encoding utf8 $TemporarySizes
            Assert-LastExit "Reading original function sizes"
            if (-not (Test-FunctionSizes $TemporarySizes)) {
                throw "The new function-size snapshot is invalid."
            }
            Move-Item -Force $TemporarySizes $FunctionSizes
        } catch {
            Remove-Item -Force $TemporarySizes -ErrorAction SilentlyContinue
            Write-Warning "Could not refresh the function-size snapshot."
        } finally {
            Prune-GhidraLogs
        }
    } else {
        Write-Warning "Ghidra is unavailable. The existing function-size snapshot was kept."
    }
    if (-not (Test-FunctionSizes $FunctionSizes)) {
        throw "No valid retail function-size snapshot is available."
    }
}

function Save-Baseline {
    Build-Project
    $Report = Join-Path $Root "build\decomp-baseline-report.json"
    $DataReport = Join-Path $Root "build\decomp-baseline-data-report.json"
    Write-ComparisonReport $Report
    Write-DataReport $DataReport
    & $VenvPython (Join-Path $Root "tools\decomp_verify.py") metadata `
        (Join-Path $Root "build\decomp-baseline-meta.json") `
        --report $Report `
        --data-report $DataReport
    Assert-LastExit "Recording baseline metadata"
}

function Stamp-FirstScore(
    [string[]] $Addresses,
    [string] $Report = "",
    [string] $Diff = "",
    [string] $DataReport = ""
) {
    $ArtifactArgs = @()
    if ($Report) {
        $ArtifactArgs = @("--report", $Report)
    } elseif ($Diff) {
        $ArtifactArgs = @("--diff", $Diff)
    } elseif ($DataReport) {
        $ArtifactArgs = @("--data-report", $DataReport)
    }
    foreach ($Address in $Addresses) {
        & $VenvPython (Join-Path $Root "tools\decomp_campaigns.py") first-score `
            --address $Address @ArtifactArgs `
            --score-functions-map (Join-Path $Root "tools\Resources\functions_map.txt") `
            --score-function-sizes (Join-Path $Root "build\decomp-function-sizes.json") `
            --score-source-root (Join-Path $Root "src") `
            --quiet
        Assert-LastExit "Recording the first-score time"
    }
}

function New-DecompReport([string] $Output = "build\decomp-report.html") {
    Ensure-Build
    $ReportJson = Join-Path $Root "build\decomp-current-report.json"
    $ReportSummary = Join-Path $Root "build\decomp-report-summary.txt"
    $FunctionSizes = Join-Path $Root "build\decomp-function-sizes.json"
    $DataReport = Join-Path $Root "build\decomp-current-data-report.json"
    if (-not [IO.Path]::IsPathRooted($Output)) {
        $Output = Join-Path $Root $Output
    }

    Update-FunctionSizes
    $ReportInput = Join-Path $DecompCache "provenance\report-input-$PID-$([guid]::NewGuid().ToString('N')).json"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") capture-input $ReportInput --output $ReportJson | Out-Null
    Assert-LastExit "Capturing comparison inputs"
    Push-Location (Join-Path $Root "build")
    try {
        & reccmp-reccmp --target TOY2 --silent --no-color --json $ReportJson |
            Tee-Object -FilePath $ReportSummary
        Assert-LastExit "Comparing binaries"
    } finally {
        Pop-Location
    }
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-report $ReportJson --input-receipt $ReportInput | Out-Null
    Assert-LastExit "Sealing comparison provenance"
    Remove-Item -Force $ReportInput -ErrorAction SilentlyContinue

    $DataInput = Join-Path $DecompCache "provenance\data-input-$PID-$([guid]::NewGuid().ToString('N')).json"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") capture-input $DataInput --output $DataReport | Out-Null
    Assert-LastExit "Capturing data-report inputs"
    & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\generate-decomp-data-report.py") `
        --original (Join-Path $Root "original\toy2.exe") `
        --recompiled (Join-Path $Root "build\toy2.exe") `
        --pdb (Join-Path $Root "build\toy2.pdb") `
        --source-root (Join-Path $Root "src") `
        --output $DataReport
    Assert-LastExit "Comparing global data"
    & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-report $DataReport --input-receipt $DataInput | Out-Null
    Assert-LastExit "Sealing data-report provenance"
    Remove-Item -Force $DataInput -ErrorAction SilentlyContinue

    & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\generate-decomp-report.py") `
        --input $ReportJson `
        --summary $ReportSummary `
        --source-root (Join-Path $Root "src") `
        --functions-map (Join-Path $Root "tools\Resources\functions_map.txt") `
        --function-sizes $FunctionSizes `
        --retail-exe (Join-Path $Root "original\toy2.exe") `
        --recompiled-exe (Join-Path $Root "build\toy2.exe") `
        --data-report $DataReport `
        --template (Join-Path $Root "tools\decomp-report-template.html") `
        --output $Output
    Assert-LastExit "Generating HTML report"
}

function Show-Help {
    @"
Usage: tools/decomp.ps1 <command> [arguments]

Commands:
  configure         Configure the VC6 SP3 build with NMake
  build             Build toy2.exe/patcher.dll and register the output
  baseline          Build and save the pre-edit comparison
  compare [args]    Run reccmp against the reference and recompiled EXEs
  score <addr>...   Show exact/effective/tool/provisional verdicts
  bc <addr>          Save and summarize one artifact-bound verbose diff
  candidates [args] Rank reconstruction candidates
  doctor [args]     Check source-work tools before a campaign starts
  brief [args]      Build or read an immutable target evidence brief
  context --brief   Validate and display one brief's function context pack
  impact [args]     Create or verify an independent impact review
  campaigns [args] Measure campaign time, report changes, and retained throughput
  finalize [args]  Validate once and write a content-addressed receipt
  discover [args]   Find credible Ghidra starts absent from the function map
  evidence <addr>   Collect bounded evidence for a mapped or discovered target
  notes <query>     Search bounded codegen, source-model, debt, and name notes
  names [args]       Search retail names
  defer <addr> ...  Record a committed blocker or prerequisite
  undefer <addr>    Clear all committed blockers for one target
  blockers [addr]   Show committed blockers
  validate [args]   Build and reject comparison or source-quality regressions
  experiment [args] Store and compare one source-form experiment
  lint [args]       Check reconstructed source plausibility
  data [addr]       Show type-aware initialized-global differences
  report [file]     Generate the self-contained HTML decompilation dashboard
  session-summary   Summarize selected targets against the saved baseline
  progress [--json] [scope]  Show annotation progress
  check              Check the function map
  sync [args]        Synchronize supported Ghidra map changes
  run [args]        Run the recompiled toy2.exe
  shell             Start cmd.exe with the VC6 environment active

A non-meta campaign uses the doctor, two independent read-only scout audits,
and a doctor-bound brief before campaign measurement:
  # Fetch and integrate origin/agent/continuous before candidate selection.
  tools/decomp.ps1 candidates --for 0x00403640 --lane closure --limit 1 `
    --prediction-features-out build\decomp-cache\prediction-handoff.json --why
  tools/decomp.ps1 doctor --mode refinement --lane closure `
    --target 0x00403640 --selection-started-at UTC_TIMESTAMP --json
  # Scout A audits retail ABI, control flow, and evidence. Scout B audits
  # callers, types, layouts, translation-unit evidence, and analogues.
  tools/decomp.ps1 brief --lane closure --target 0x00403640 `
    --doctor-receipt build\decomp-cache\doctor\latest.json `
    --scout-report build\decomp-cache\retail-scout.json `
    --scout-report build\decomp-cache\context-scout.json --json
  tools/decomp.ps1 campaigns start --mode refinement --lane closure `
    --address 0x00403640 `
    --prediction-handoff build\decomp-cache\prediction-handoff.json `
    --doctor-receipt build\decomp-cache\doctor\latest.json --brief BRIEF_PATH

Each scout returns a schema-2 JSON file. Use distinct scout IDs and exactly the
schema, scout_id, audit, lane, target, access, owns_mutations, doctor_receipt,
and findings keys. Each finding has only category, claim, and evidence. Each
evidence item has only source and locator. Set access to read-only and
owns_mutations to false. Use the
retail-abi-control-flow-evidence and callers-types-layout-translation-unit-analogue
audits, distinct scout IDs, and nonempty findings. The doctor_receipt object has
only receipt_id and sha256. Bind both reports to the canonical doctor identity.
Keep the doctor artifacts, scout reports, and brief unchanged. Finalization
revalidates them and rejects a HEAD change after the doctor.

The closure and production features JSON must contain success_probability,
cohort_sample_size, median_retained_bytes, and median_minutes. The two expected
values must equal the median values. Use the lower bound for stop decisions.
Other non-meta lanes need a nonempty features JSON, a version, both median
values, and a lower bound. A selector output file is directly valid for
--prediction-handoff. The start command validates its schema, lane, mode, and
address. Pass the file directly; do not rebuild it with jq. Do not mix it with
individual forecast options. A closure or production bundle needs one distinct
selector export for each address. Repeat --prediction-handoff in address order.
Use individual forecast options for resource, data, or research work without a
selector export. For multi-target source work, prediction features need a
per_target object keyed by every exact address. Start creates this object from
the repeated selector handoffs for closure and production. Each entry needs
median_retained_bytes and lower_retained_bytes. The target medians must sum to
the campaign median.
Add --json only when candidate output is also needed on stdout.
The start wrapper does not run the doctor or create the brief. It requires the
explicit receipt and one brief per ordered target.
The doctor receipt expires 60 minutes after the doctor ends. Run the exact
preflight again after expiry.
Use refinement for closure and production. Use coverage for research with a
STUB or unstarted target that has a specific blocker. Use refinement for an
implemented FUNCTION with a specific blocker or a valid selector cooldown or
circuit route. The brief and campaign start recompute this route. Use data for
data and resource for resource. Invalid pairs fail before costly preflight.

Production selection reads the sealed canonical report. After target
selection, run tools/decomp.ps1 bc ADDRESS before the doctor and brief. This
creates the target-local, hash-bound verbose mismatch. The doctor and brief
reject stale mismatch evidence.

Evidence-only research occurs before measurement. A timed research campaign is
a coverage or refinement source pilot, based on the target state, with a ready
doctor-bound brief:
  tools/decomp.ps1 campaigns start --mode coverage --lane research `
    --address 0x00465180 --subsystem SUBSYSTEM `
    --expected-minutes MEDIAN_MINUTES `
    --expected-retained-bytes MEDIAN_BYTES --prediction-version cohort-v1 `
    --prediction-lower-bound-bytes LOWER_BYTES `
    --prediction-features build\decomp-cache\research-prediction.json `
    --doctor-receipt build\decomp-cache\doctor\latest.json --brief BRIEF_PATH

Stamp preflight and get the first score by their absolute deadlines. Start
finalization by the stop deadline, or by the displayed extension deadline when
the forecast is at least 100 retained bytes. The finalizer rejects a late source
result. It also rejects symbolic-link inputs and configured non-generated
sources that are absent from the staged Git tree.

Resource work uses the same preflight and the standard finalizer:
  tools/decomp.ps1 doctor --mode resource --lane resource `
    --resource '2,127,2057' --selection-started-at UTC_TIMESTAMP --json
  # Run the two required scout audits and save their bound reports.
  tools/decomp.ps1 brief --lane resource --target '2,127,2057' `
    --doctor-receipt build\decomp-cache\doctor\latest.json `
    --scout-report build\decomp-cache\retail-scout.json `
    --scout-report build\decomp-cache\context-scout.json --json
  tools/decomp.ps1 campaigns start --mode resource --lane resource `
    --resource '2,127,2057' --expected-minutes MEDIAN_MINUTES `
    --expected-retained-bytes MEDIAN_BYTES --prediction-version resource-v1 `
    --prediction-lower-bound-bytes LOWER_BYTES `
    --prediction-features build\decomp-cache\resource-prediction.json `
    --doctor-receipt build\decomp-cache\doctor\latest.json `
    --brief RESOURCE_BRIEF
  tools/decomp.ps1 finalize --result source --mode resource `
    --resource '2,127,2057' --staged

For no-source, restore all source trials and keep --staged on finalization:
  tools/decomp.ps1 finalize --result no-source --mode MODE `
    --target ADDRESS --staged
  tools/decomp.ps1 campaigns record --result no-source `
    --model "The tested loop form scored 20 percent."
Repeat --target for all active addresses in order. Use --resource for resource
work.

A meta campaign is one bounded workflow repair that blocks reliable source
progress. It skips candidates, the doctor, scouts, and brief.
It has no forecast or source deadlines after start. The fault does not have to
block every source queue:
  tools/decomp.ps1 campaigns start --mode meta --lane meta
  tools/decomp.ps1 finalize --result meta-fix --mode meta --staged
  tools/decomp.ps1 campaigns record --result meta-fix

Record the result after finalization. Add staged. A new coverage or refinement
source result requires a sealed independent impact review. The reviewer edits
all decision and status fields after review. Keep the exact generated
citations:
  tools/decomp.ps1 campaigns record --result source
  tools/decomp.ps1 campaigns delivery --campaign-id ID --status staged
  tools/decomp.ps1 impact template --finalize-receipt RECEIPT `
    --reviewer-id REVIEWER --json `
    | Set-Content -Encoding ASCII build/decomp-cache/review-pending.json
  # The independent reviewer edits review-pending.json.
  tools/decomp.ps1 impact seal-review --finalize-receipt RECEIPT `
    --review-report build/decomp-cache/review-pending.json --json `
    | Set-Content -Encoding ASCII build/decomp-cache/review-sealed.json
  tools/decomp.ps1 impact verify-review --finalize-receipt RECEIPT `
    --review-report build/decomp-cache/review-sealed.json --json
  tools/decomp.ps1 campaigns delivery --campaign-id ID --status accepted `
    --review-report build/decomp-cache/review-sealed.json

The first reviewed acceptance rejects a missing report. An exact accepted
retry can omit the report and revalidates the cached review. A supplied retry
must use the same semantic report. Legacy, no-source, data, resource, and meta
results do not use this impact review.

Stage and commit the finalized files, campaign row, staged entry, and accepted
entry. Fetch and rebase that commit onto current origin/agent/continuous. Set
COMMIT to the resulting HEAD and BASE to its first parent. Then run:
  `$COMMIT = git rev-parse HEAD
  `$BASE = git rev-parse HEAD^
  `$DELIVERY_RECEIPT = (tools/decomp.ps1 campaigns delivery-verify `
    --campaign-id ID --commit `$COMMIT --base-commit `$BASE | `
    ConvertFrom-Json).path
  tools/decomp.ps1 campaigns delivery --campaign-id ID --status integrated `
    --base-commit `$BASE --commit `$COMMIT `
    --delivery-receipt `$DELIVERY_RECEIPT
  tools/decomp.ps1 campaigns delivery --campaign-id ID --status committed `
    --commit `$COMMIT --delivery-receipt `$DELIVERY_RECEIPT
  # Push `$COMMIT to origin/agent/continuous.
  tools/decomp.ps1 campaigns delivery --campaign-id ID --status pushed `
    --commit `$COMMIT --delivery-receipt `$DELIVERY_RECEIPT

The delivery receipt rejects a conflict on a finalized campaign path. Source
delivery rebuilds and validates. Meta delivery runs focused tool
tests. No-source delivery checks the tree, index, and exact ledger and model-note
append. Do not build, create a report, or run Ghidra sync for no-source.

The pushed command verifies that the remote contains COMMIT. Put integrated,
committed, and pushed in one telemetry-only follow-up commit, then push it.
These entries stay outside the campaign commit. This follow-up commit does not
count as source progress. Do not reuse a receipt after HEAD, BASE, an input, or
an artifact changes. A rejected entry ends delivery.

Before a pivot, restore the current target trials. Run the doctor and two scout
audits for only the new address. Create its doctor-bound brief, then add it:
  tools/decomp.ps1 candidates --lane production --limit 1 --for 0x004038E0 `
    --prediction-features-out build\decomp-cache\prediction-handoff.json --why
  tools/decomp.ps1 bc 0x004038E0
  tools/decomp.ps1 doctor --mode refinement --lane production `
    --target 0x004038E0 `
    --selection-started-at PIVOT_SELECTION_UTC --json
  # Run two independent read-only scout audits with this doctor receipt.
  tools/decomp.ps1 brief --lane production --target 0x004038E0 `
    --doctor-receipt PIVOT_RECEIPT `
    --scout-report build\decomp-cache\pivot-retail-scout.json `
    --scout-report build\decomp-cache\pivot-context-scout.json --json
  tools/decomp.ps1 campaigns add-target --address 0x004038E0 `
    --replace OLD_ADDRESS `
    --prediction-handoff build\decomp-cache\prediction-handoff.json `
    --doctor-receipt PIVOT_RECEIPT --brief PIVOT_BRIEF

A family expansion omits --replace. Each new member still needs a fresh doctor
receipt, brief, and forecast. Prediction events preserve each target forecast.
The original campaign deadlines do not reset.

Routine output is bounded. Use --limit 0, --all, or --full when a command
reports omitted evidence. The bc command saves its complete output under
build\decomp-diffs.
"@
}

Set-Location $Root
if ($Command -eq "help") {
    Show-Help
    exit 0
}
if ($Command -in @("doctor", "brief")) {
    Import-VC6Environment
    $Script = if ($Command -eq "doctor") {
        "tools\decomp_doctor.py"
    } else {
        "tools\decomp_brief.py"
    }
    Prune-GhidraLogs
    try {
        & $VenvPython (Join-Path $Root $Script) @CommandArgs
        Assert-LastExit "Running $Command"
    } finally {
        Prune-GhidraLogs
    }
    exit 0
}
if ($Command -eq "finalize") {
    Import-VC6Environment
    & $VenvPython (Join-Path $Root "tools\decomp_campaigns.py") finalize @CommandArgs
    Assert-LastExit "Finalizing the campaign"
    exit 0
}
if ($Command -eq "campaigns") {
    $CampaignScript = Join-Path $Root "tools\decomp_campaigns.py"
    $CampaignHelp = $CommandArgs -contains "--help" -or $CommandArgs -contains "-h"
    $CampaignAction = ""
    $CampaignGlobalArgs = @()
    $CampaignGlobalOptions = @(
        "--file",
        "--state-file",
        "--source-models",
        "--functions-map",
        "--function-sizes",
        "--worktree-root",
        "--source-root"
    )
    $CampaignIndex = 0
    while ($CampaignIndex -lt $CommandArgs.Count) {
        $CampaignArgument = $CommandArgs[$CampaignIndex]
        if ($CampaignArgument -in $CampaignGlobalOptions) {
            if ($CampaignIndex + 1 -ge $CommandArgs.Count) { break }
            $CampaignValue = $CommandArgs[$CampaignIndex + 1]
            $CampaignGlobalArgs += @($CampaignArgument, $CampaignValue)
            $CampaignIndex += 2
        } elseif ($CampaignArgument -match '^--state-file=(.*)$') {
            $CampaignGlobalArgs += $CampaignArgument
            $CampaignIndex++
        } elseif ($CampaignArgument -match '^--(?:file|source-models|functions-map|function-sizes|worktree-root|source-root)=') {
            $CampaignGlobalArgs += $CampaignArgument
            $CampaignIndex++
        } else {
            $CampaignAction = $CampaignArgument
            break
        }
    }
    if ($CampaignHelp) {
        & $VenvPython $CampaignScript @CommandArgs
        Assert-LastExit "Showing campaign help"
    } elseif ($CampaignAction -eq "start") {
        Import-VC6Environment
        $CampaignMode = ""
        $CampaignLane = ""
        $CampaignTargets = @()
        $CampaignResource = ""
        $DoctorReceipt = ""
        $BriefPaths = @()
        for ($OptionIndex = 0; $OptionIndex -lt $CommandArgs.Count; $OptionIndex++) {
            $Option = $CommandArgs[$OptionIndex]
            if ($Option -in @("--mode", "--lane", "--address", "--resource", "--doctor-receipt", "--brief")) {
                if ($OptionIndex + 1 -ge $CommandArgs.Count) { continue }
                $Value = $CommandArgs[++$OptionIndex]
                switch ($Option) {
                    "--mode" { $CampaignMode = $Value }
                    "--lane" { $CampaignLane = $Value }
                    "--address" { $CampaignTargets += $Value }
                    "--resource" { $CampaignResource = $Value }
                    "--doctor-receipt" { $DoctorReceipt = $Value }
                    "--brief" { $BriefPaths += $Value }
                }
            } elseif ($Option -match '^--mode=(.*)$') {
                $CampaignMode = $Matches[1]
            } elseif ($Option -match '^--lane=(.*)$') {
                $CampaignLane = $Matches[1]
            } elseif ($Option -match '^--address=(.*)$') {
                $CampaignTargets += $Matches[1]
            } elseif ($Option -match '^--resource=(.*)$') {
                $CampaignResource = $Matches[1]
            } elseif ($Option -match '^--doctor-receipt=(.*)$') {
                $DoctorReceipt = $Matches[1]
            } elseif ($Option -match '^--brief=(.*)$') {
                $BriefPaths += $Matches[1]
            }
        }
        if (-not $CampaignLane) {
            $CampaignLane = switch ($CampaignMode) {
                "coverage" { "research" }
                "refinement" { "production" }
                default { $CampaignMode }
            }
        }
        if ($CampaignMode -and $CampaignMode -ne "meta") {
            if (-not $DoctorReceipt) {
                throw "A $CampaignMode campaign needs an explicit --doctor-receipt."
            }
            $BriefCount = if ($CampaignMode -eq "resource") {
                1
            } else {
                $CampaignTargets.Count
            }
            if ($BriefPaths.Count -ne $BriefCount) {
                throw "A $CampaignMode campaign needs one --brief for each ordered target."
            }
        }
        $CacheDirectory = Join-Path $Root "build\decomp-cache"
        New-Item -ItemType Directory -Force $CacheDirectory | Out-Null
        $ProgressPath = Join-Path $CacheDirectory "campaign-progress-before.json"
        $ProgressLines = @(
            & $VenvPython (Join-Path $Root "decomp_utils.py") --progress --json
        )
        Assert-LastExit "Reading campaign progress"
        $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText(
            $ProgressPath,
            ($ProgressLines -join "`n") + "`n",
            $Utf8NoBom
        )
        $CommandArgs += @("--progress-before", $ProgressPath)
        & $VenvPython $CampaignScript @CommandArgs
        Assert-LastExit "Starting campaign measurement"
        try {
            if ($CampaignMode -eq "meta") {
                & $VenvPython $CampaignScript @CampaignGlobalArgs "set-meta-baseline" `
                    "--progress" $ProgressPath "--quiet"
                Assert-LastExit "Attaching the meta campaign baseline"
            } else {
                Save-Baseline
                & $VenvPython $CampaignScript @CampaignGlobalArgs "set-baseline" `
                    "--progress" $ProgressPath "--quiet"
                Assert-LastExit "Attaching the campaign baseline"
            }
        } catch {
            $SetupError = $_
            try {
                & $VenvPython $CampaignScript @CampaignGlobalArgs "abort" `
                    "--reason" "The campaign baseline setup failed."
                Assert-LastExit "Aborting campaign measurement"
            } catch {
                throw "Campaign setup failed. The automatic abort also failed. Setup: $($SetupError.Exception.Message) Abort: $($_.Exception.Message)"
            }
            throw $SetupError
        }
    } elseif ($CampaignAction -eq "record") {
        & $VenvPython $CampaignScript @CommandArgs
        Assert-LastExit "Recording the campaign result"
    } elseif ($CampaignAction -eq "delivery-verify") {
        Import-VC6Environment
        & $VenvPython $CampaignScript @CommandArgs
        Assert-LastExit "Verifying the integrated campaign"
    } else {
        & $VenvPython $CampaignScript @CommandArgs
        Assert-LastExit "Running the campaign command"
    }
    exit 0
}
if (($CommandArgs -contains "--help" -or $CommandArgs -contains "-h") -and
    $Command -in @("baseline", "score", "bc", "data", "report")) {
    if ($Command -eq "baseline") {
        Write-Host "Usage: tools/decomp.ps1 baseline"
    } elseif ($Command -eq "score") {
        & $VenvPython (Join-Path $Root "tools\decomp_verify.py") score --help
        Assert-LastExit "Showing score help"
    } elseif ($Command -eq "data") {
        & $VenvPython (Join-Path $Root "tools\decomp_data.py") --help
        Assert-LastExit "Showing data help"
    } elseif ($Command -eq "report") {
        Write-Host "Usage: tools/decomp.ps1 report [output.html]"
    } else {
        Write-Host "Usage: tools/decomp.ps1 bc [--full] <address>"
    }
    exit 0
}
if ($Command -eq "lint") {
    & $VenvPython (Join-Path $Root "tools\decomp_lint.py") @CommandArgs
    Assert-LastExit "Checking source plausibility"
    exit 0
}
if ($Command -eq "notes") {
    & $VenvPython (Join-Path $Root "tools\decomp_notes.py") @CommandArgs
    Assert-LastExit "Searching reconstruction notes"
    exit 0
}
if ($Command -eq "names") {
    & $VenvPython (Join-Path $Root "tools\decomp_original_names.py") @CommandArgs
    Assert-LastExit "Searching retail names"
    exit 0
}
if ($Command -eq "check") {
    & $VenvPython (Join-Path $Root "tools\ghidra_sync.py") check @CommandArgs
    Assert-LastExit "Checking the function map"
    exit 0
}
if ($Command -eq "sync") {
    & $VenvPython (Join-Path $Root "tools\ghidra_sync.py") sync @CommandArgs
    Assert-LastExit "Synchronizing the function map"
    exit 0
}
if ($Command -in @("discover", "evidence")) {
    $Script = if ($Command -eq "discover") { "tools\decomp_discover.py" } else { "tools\decomp_evidence.py" }
    Prune-GhidraLogs
    try {
        & $VenvPython (Join-Path $Root $Script) @CommandArgs
        Assert-LastExit "Reading decompilation evidence"
    } finally {
        Prune-GhidraLogs
    }
    exit 0
}
if ($Command -eq "defer") {
    if ($CommandArgs.Count -lt 3) {
        throw "Usage: tools/decomp.ps1 defer <address> [--blocked-by <address> ...] --reason <text>"
    }
    $DeferralArgs = @("--record-deferral") + $CommandArgs
    & $VenvPython (Join-Path $Root "tools\decomp_candidates.py") @DeferralArgs
    Assert-LastExit "Recording the supported deferral"
    exit 0
}
if ($Command -eq "undefer") {
    if ($CommandArgs.Count -ne 1) {
        throw "Usage: tools/decomp.ps1 undefer <address>"
    }
    & $VenvPython (Join-Path $Root "tools\decomp_candidates.py") --clear-deferral $CommandArgs[0]
    Assert-LastExit "Clear the committed blockers"
    exit 0
}
if ($Command -eq "blockers") {
    $BlockerArgs = @("--list-blockers")
    if ($CommandArgs.Count -gt 0 -and -not $CommandArgs[0].StartsWith("--")) {
        $BlockerArgs += $CommandArgs[0]
        if ($CommandArgs.Count -gt 1) { $BlockerArgs += $CommandArgs[1..($CommandArgs.Count - 1)] }
    } else {
        $BlockerArgs += ""
        $BlockerArgs += $CommandArgs
    }
    & $VenvPython (Join-Path $Root "tools\decomp_candidates.py") @BlockerArgs
    Assert-LastExit "Show the committed blockers"
    exit 0
}
if ($Command -eq "session-summary") {
    if ($CommandArgs.Count -eq 0) {
        throw "Usage: tools/decomp.ps1 session-summary <address> [address...]"
    }
    & $VenvPython (Join-Path $Root "tools\decomp_verify.py") session-summary `
        (Join-Path $Root "build\decomp-baseline-report.json") `
        (Join-Path $Root "build\decomp-current-report.json") @CommandArgs
    Assert-LastExit "Summarizing the session"
    exit 0
}
if ($Command -eq "candidates") {
    if ($CommandArgs -notcontains "-h" -and $CommandArgs -notcontains "--help") {
        Ensure-CandidateReport
    }
    & $VenvPython (Join-Path $Root "tools\decomp_candidates.py") @CommandArgs
    Assert-LastExit "Ranking decompilation candidates"
    exit 0
}
Import-VC6Environment

switch ($Command) {
    "configure" { Configure-Project }
    "build" { Build-Project }
    "compare" {
        Ensure-Build
        Push-Location (Join-Path $Root "build")
        try {
            & reccmp-reccmp --target TOY2 @CommandArgs
            Assert-LastExit "Comparing binaries"
        } finally {
            Pop-Location
        }
    }
    "score" {
        if ($CommandArgs.Count -eq 0) { throw "Usage: tools/decomp.ps1 score <address>..." }
        Build-Project
        $Report = Join-Path $Root "build\decomp-score-report.json"
        Write-ComparisonReport $Report
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") score $Report @CommandArgs
        Assert-LastExit "Classifying comparison results"
        $ScoreTargets = @($CommandArgs | Where-Object { $_ -match '^0x[0-9A-Fa-f]{1,8}$' })
        Stamp-FirstScore -Addresses $ScoreTargets -Report $Report
    }
    "bc" {
        $Full = $false
        $BcArgs = @()
        foreach ($Argument in $CommandArgs) {
            if ($Argument -eq "--full") { $Full = $true } else { $BcArgs += $Argument }
        }
        $CommandArgs = $BcArgs
        if ($CommandArgs.Count -ne 1 -or $CommandArgs[0] -notmatch '^0x[0-9A-Fa-f]{1,8}$') {
            throw "Usage: tools/decomp.ps1 bc [--full] <address>"
        }
        $CanonicalAddress = "0x{0:X8}" -f [Convert]::ToUInt32(
            $CommandArgs[0].Substring(2), 16
        )
        Build-Project
        $CurrentReport = Join-Path $Root "build\decomp-current-report.json"
        Ensure-CandidateReport
        $DiffDirectory = Join-Path $Root "build\decomp-diffs"
        New-Item -ItemType Directory -Force $DiffDirectory | Out-Null
        $Diff = Join-Path $DiffDirectory "$CanonicalAddress.txt"
        Push-Location (Join-Path $Root "build")
        try {
            & reccmp-reccmp --target TOY2 --no-color --verbose $CanonicalAddress | Set-Content -Encoding utf8 $Diff
            Assert-LastExit "Comparing the target"
        } finally {
            Pop-Location
        }
        & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-diff `
            $Diff --address $CanonicalAddress --report $CurrentReport | Out-Null
        Assert-LastExit "Sealing mismatch provenance"
        $DiffArgs = @(
            $Diff
            "--address"
            $CanonicalAddress
            "--functions-map"
            (Join-Path $Root "tools\Resources\functions_map.txt")
            "--function-sizes"
            (Join-Path $Root "build\decomp-function-sizes.json")
            "--source-root"
            (Join-Path $Root "src")
        )
        if ($Full) { $DiffArgs += "--full" }
        & $VenvPython (Join-Path $Root "tools\decomp_diff.py") @DiffArgs
        Assert-LastExit "Formatting the comparison"
        if (Test-Path (Join-Path $Root "build\decomp-campaign-state.json")) {
            Stamp-FirstScore -Addresses @($CanonicalAddress) -Diff $Diff
        }
    }
    "baseline" {
        Save-Baseline
    }
    "validate" {
        $Baseline = Join-Path $Root "build\decomp-baseline-report.json"
        if (-not (Test-Path $Baseline)) { throw "No baseline exists. Run tools/decomp.ps1 baseline before you edit." }
        $BaselineData = Join-Path $Root "build\decomp-baseline-data-report.json"
        $CurrentData = Join-Path $Root "build\decomp-current-data-report.json"
        $AccountingCorrection = ""
        $Mode = ""
        $Resource = ""
        $Targets = @()
        $AllowTargetRegression = $false
        $MetaResolution = $false
        $Staged = $false
        for ($Index = 0; $Index -lt $CommandArgs.Count; $Index++) {
            if ($CommandArgs[$Index] -eq "--target" -and $Index + 1 -lt $CommandArgs.Count) {
                $Index++
                $Targets += $CommandArgs[$Index]
            } elseif ($CommandArgs[$Index] -eq "--allow-target-regression") {
                $AllowTargetRegression = $true
            } elseif ($CommandArgs[$Index] -eq "--mode" -and $Index + 1 -lt $CommandArgs.Count) {
                $Index++
                $Mode = $CommandArgs[$Index]
            } elseif ($CommandArgs[$Index] -eq "--resource" -and $Index + 1 -lt $CommandArgs.Count) {
                $Index++
                $Resource = $CommandArgs[$Index]
            } elseif ($CommandArgs[$Index] -eq "--accounting-correction" -and $Index + 1 -lt $CommandArgs.Count) {
                $Index++
                $AccountingCorrection = $CommandArgs[$Index]
            } elseif ($CommandArgs[$Index] -eq "--meta-resolution") {
                $MetaResolution = $true
            } elseif ($CommandArgs[$Index] -eq "--staged") {
                $Staged = $true
            } else {
                throw "Unknown validate argument: $($CommandArgs[$Index])"
            }
        }
        if ($Mode -ne "resource" -and $Targets.Count -eq 0) { throw "Validate needs at least one --target address." }
        if ($Mode -and $Mode -notin @("coverage", "refinement", "data", "resource")) {
            throw "Unknown validation mode: $Mode"
        }
        if ($Mode -eq "resource" -and $Targets.Count -gt 0) {
            throw "A resource campaign cannot have target addresses."
        }
        if ($Mode -eq "resource" -and -not $Resource) {
            throw "A resource campaign needs exactly one --resource."
        }
        if ($Mode -ne "resource" -and $Resource) {
            throw "--resource requires --mode resource."
        }
        if (-not $MetaResolution -and -not $Mode) {
            throw "Validate needs --mode coverage, refinement, or data."
        }
        if ($Mode -eq "data" -and $Targets.Count -gt 3) {
            throw "A data campaign can have at most three targets."
        }
        if ($AllowTargetRegression -and -not $MetaResolution) {
            throw "--allow-target-regression requires --meta-resolution."
        }
        if ($AccountingCorrection -and -not $MetaResolution) {
            throw "--accounting-correction requires --meta-resolution."
        }
        if ($AccountingCorrection -and $Mode -ne "data") {
            throw "--accounting-correction requires --mode data."
        }
        if ($Mode -in @("data", "resource") -and -not (Test-Path $BaselineData)) {
            throw "No data baseline exists. Run tools/decomp.ps1 baseline before a data campaign."
        }
        if ($Mode -eq "data" -and $Staged -and -not $AccountingCorrection) {
            $StagedSourcePaths = @(& git diff --cached --name-only -- src)
            Assert-LastExit "Checking the staged source diff"
            if ($StagedSourcePaths.Count -eq 0) {
                throw "A staged data campaign must change the source tree."
            }
        }

        Build-Project
        $Current = Join-Path $Root "build\decomp-current-report.json"
        Write-ComparisonReport $Current
        if ($Mode -in @("data", "resource")) {
            Write-DataReport $CurrentData
        }
        $VerifyArgs = @("validate", $Baseline, $Current) + $Targets
        $VerifyArgs += @("--metadata", (Join-Path $Root "build\decomp-baseline-meta.json"))
        if ($AllowTargetRegression) { $VerifyArgs += "--allow-target-regression" }
        if ($MetaResolution) { $VerifyArgs += "--meta-resolution" }
        if ($Staged) { $VerifyArgs += "--staged" }
        if ($Mode) { $VerifyArgs += @("--mode", $Mode) }
        if ($Resource) { $VerifyArgs += @("--resource", $Resource) }
        if ($Mode -in @("data", "resource")) {
            $VerifyArgs += @(
                "--baseline-data"
                $BaselineData
                "--current-data"
                $CurrentData
            )
        }
        if ($AccountingCorrection) {
            $VerifyArgs += @("--accounting-correction", $AccountingCorrection)
        }
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") @VerifyArgs
        Assert-LastExit "Validating comparison results"
        if ($Mode -eq "resource") {
            & $VenvPython (Join-Path $Root "tools\decomp_campaigns.py") "resource-score" "--quiet"
            Assert-LastExit "Recording the first resource score"
        }
        if ($Staged) {
            & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_lint.py") --staged --warnings-as-errors
            Assert-LastExit "Validating staged source plausibility"
        }
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\ghidra_sync.py") check
        Assert-LastExit "Checking the function map"
        & git diff --check
        Assert-LastExit "Checking the working tree diff"
        if ($Mode -eq "data") {
            Stamp-FirstScore -Addresses $Targets -DataReport $CurrentData
        } else {
            Stamp-FirstScore -Addresses $Targets -Report $Current
        }
    }
    "experiment" {
        if ($CommandArgs.Count -lt 1) {
            throw "Usage: tools/decomp.ps1 experiment start|try|status|best|advise|report <address> [label] [options]"
        }
        $Action = $CommandArgs[0]
        if ($Action -eq "start") {
            if ($CommandArgs.Count -lt 2) {
                throw "Experiment start needs an address."
            }
            $Address = $CommandArgs[1]
            $Extra = @()
            if ($CommandArgs.Count -gt 2) {
                $Extra = $CommandArgs[2..($CommandArgs.Count - 1)]
            }
            $Plan = @(& $VenvPython (Join-Path $Root "tools\decomp_experiment.py") `
                begin-session $Address @Extra --format lines)
            Assert-LastExit "Preparing the experiment session"
            if ($Plan.Count -ne 7) {
                throw "The experiment session plan is incomplete."
            }
            $SessionId = $Plan[0]
            $SessionDirectory = $Plan[1]
            $BaselineReport = $Plan[2]
            $BaselineDataReport = $Plan[3]
            $CompilerContext = $Plan[4]
            $SourcePatch = $Plan[5]
            $BuildLock = $Plan[6]
            $LockStream = $null
            try {
                $LockStream = [IO.File]::Open(
                    $BuildLock,
                    [IO.FileMode]::OpenOrCreate,
                    [IO.FileAccess]::ReadWrite,
                    [IO.FileShare]::None
                )
                Build-Project
                Update-FunctionSizes
                Write-ComparisonReport $BaselineReport
                Write-DataReport $BaselineDataReport
                & $VenvPython (Join-Path $Root "tools\decomp_verify.py") metadata `
                    $CompilerContext --report $BaselineReport --data-report $BaselineDataReport
                Assert-LastExit "Recording the experiment baseline"
                & git diff --binary | Set-Content -Encoding utf8 $SourcePatch
                Assert-LastExit "Recording the experiment source patch"
                & $VenvPython (Join-Path $Root "tools\decomp_experiment.py") `
                    attach-baseline $Address --session $SessionId | Out-Null
                Assert-LastExit "Attaching the experiment baseline"
            } finally {
                if ($null -ne $LockStream) { $LockStream.Dispose() }
            }
            Write-Host "Started experiment $Address in $SessionDirectory."
        } elseif ($Action -eq "try") {
            if ($CommandArgs.Count -lt 3) {
                throw "Experiment try needs an address and a label."
            }
            $Address = $CommandArgs[1]
            $Label = $CommandArgs[2]
            $Extra = @()
            if ($CommandArgs.Count -gt 3) {
                $Extra = $CommandArgs[3..($CommandArgs.Count - 1)]
            }
            $TrialPlan = @(& $VenvPython (Join-Path $Root "tools\decomp_experiment.py") `
                reserve $Address $Label @Extra --format lines)
            Assert-LastExit "Reserving the experiment trial"
            if ($TrialPlan.Count -ne 8) {
                throw "The experiment trial plan is incomplete."
            }
            $SessionId = $TrialPlan[0]
            $TrialId = $TrialPlan[1]
            $TrialDirectory = $TrialPlan[2]
            $Report = $TrialPlan[3]
            $Diff = $TrialPlan[4]
            $SourcePatch = $TrialPlan[5]
            $CompilerContext = $TrialPlan[6]
            $BuildLock = $TrialPlan[7]
            $LockStream = $null
            $FailureStage = "build"
            try {
                $LockStream = [IO.File]::Open(
                    $BuildLock,
                    [IO.FileMode]::OpenOrCreate,
                    [IO.FileAccess]::ReadWrite,
                    [IO.FileShare]::None
                )
                Build-Project
                $FailureStage = "comparison"
                Write-ComparisonReport $Report
                $FailureStage = "normalize"
                & git diff --binary | Set-Content -Encoding utf8 $SourcePatch
                Assert-LastExit "Recording the experiment source patch"
                $FailureStage = "diff"
                Push-Location (Join-Path $Root "build")
                try {
                    & reccmp-reccmp --target TOY2 --no-color --verbose $Address |
                        Set-Content -Encoding utf8 $Diff
                    Assert-LastExit "Comparing the experiment"
                } finally {
                    Pop-Location
                }
                & $VenvPython (Join-Path $Root "tools\decomp_provenance.py") seal-diff `
                    $Diff --address $Address --report $Report | Out-Null
                Assert-LastExit "Sealing the experiment diff"
                $FailureStage = "normalize"
                & $VenvPython (Join-Path $Root "tools\decomp_verify.py") metadata `
                    $CompilerContext --report $Report
                Assert-LastExit "Recording the experiment compiler context"
                & $VenvPython (Join-Path $Root "tools\decomp_experiment.py") record `
                    $Address --session $SessionId --trial $TrialId
                Assert-LastExit "Recording the experiment trial"
            } catch {
                $FailureCode = if ($LASTEXITCODE) { $LASTEXITCODE } else { 1 }
                $FailureMessage = $_.Exception.Message
                if ($FailureMessage.Length -gt 2000) {
                    $FailureMessage = $FailureMessage.Substring(0, 2000)
                }
                & $VenvPython (Join-Path $Root "tools\decomp_experiment.py") fail `
                    $Address --session $SessionId --trial $TrialId --stage $FailureStage `
                    --exit-code $FailureCode --message $FailureMessage | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    Write-Warning "The experiment controller could not record the trial failure."
                }
                throw
            } finally {
                if ($null -ne $LockStream) { $LockStream.Dispose() }
            }
            Stamp-FirstScore -Addresses @($Address) -Report $Report
        } elseif ($Action -in @("status", "best", "advise", "report")) {
            if ($CommandArgs.Count -lt 2) {
                throw "Experiment $Action needs an address."
            }
            & $VenvPython (Join-Path $Root "tools\decomp_experiment.py") $Action `
                @($CommandArgs[1..($CommandArgs.Count - 1)])
            Assert-LastExit "Reading the experiment session"
        } else {
            throw "The experiment action must be start, try, status, best, advise, or report."
        }
    }
    "report" {
        if ($CommandArgs.Count -gt 1) {
            throw "Usage: tools/decomp.ps1 report [output.html]"
        }
        $Output = if ($CommandArgs.Count -eq 1) { $CommandArgs[0] } else { "build\decomp-report.html" }
        New-DecompReport $Output
    }
    "data" {
        Build-Project
        $DataReport = Join-Path $Root "build\decomp-current-data-report.json"
        Write-DataReport $DataReport
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_data.py") @CommandArgs
        Assert-LastExit "Showing global-data evidence"
        $DataTargets = @($CommandArgs | Where-Object { $_ -match '^0x[0-9A-Fa-f]{1,8}$' })
        Stamp-FirstScore -Addresses $DataTargets -DataReport $DataReport
    }
    "progress" {
        if ($CommandArgs.Count -gt 2) {
            throw "Usage: tools/decomp.ps1 progress [--json] [namespace]"
        }
        $ProgressArgs = @("decomp_utils.py", "--progress")
        $Namespace = @($CommandArgs | Where-Object { $_ -ne "--json" })
        if ($Namespace.Count -gt 1) {
            throw "Usage: tools/decomp.ps1 progress [--json] [namespace]"
        }
        if ($Namespace.Count -eq 1) { $ProgressArgs += $Namespace[0] }
        if ($CommandArgs -contains "--json") { $ProgressArgs += "--json" }
        & (Join-Path $VenvScripts "python.exe") @ProgressArgs
        Assert-LastExit "Calculating progress"
    }
    "run" {
        Ensure-Build
        & (Join-Path $Root "build\toy2.exe") @CommandArgs
        Assert-LastExit "Running toy2.exe"
    }
    "shell" { & $env:ComSpec /k }
}
