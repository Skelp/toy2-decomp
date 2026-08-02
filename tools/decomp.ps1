[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("configure", "build", "compare", "score", "bc", "candidates", "discover", "evidence", "notes", "defer", "undefer", "blockers", "baseline", "validate", "experiment", "lint", "report", "session-summary", "progress", "run", "shell", "help")]
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
$Vcvars = Join-Path $MsvcBase "VC98\Bin\VCVARS32.BAT"

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
    if (
        -not (Test-Path (Join-Path $Root "build\reccmp-build.yml")) -or
        -not (Test-Path (Join-Path $Root "build\toy2.exe")) -or
        -not (Test-Path (Join-Path $Root "build\toy2.pdb"))
    ) {
        Build-Project
    }
}

function Write-ComparisonReport([string] $Output) {
    Ensure-Build
    Push-Location (Join-Path $Root "build")
    try {
        & reccmp-reccmp --target TOY2 --silent --no-color --json $Output | Out-Null
        Assert-LastExit "Comparing binaries"
    } finally {
        Pop-Location
    }
}

function New-DecompReport([string] $Output = "build\decomp-report.html") {
    Ensure-Build
    $ReportJson = Join-Path $Root "build\decomp-report-data.json"
    $ReportSummary = Join-Path $Root "build\decomp-report-summary.txt"
    $FunctionSizes = Join-Path $Root "build\decomp-function-sizes.json"
    $DataReport = Join-Path $Root "build\decomp-data-report.json"
    if (-not [IO.Path]::IsPathRooted($Output)) {
        $Output = Join-Path $Root $Output
    }

    Push-Location (Join-Path $Root "build")
    try {
        & reccmp-reccmp --target TOY2 --silent --no-color --json $ReportJson |
            Tee-Object -FilePath $ReportSummary
        Assert-LastExit "Comparing binaries"
    } finally {
        Pop-Location
    }

    if (Get-Command ghidra -ErrorAction SilentlyContinue) {
        & ghidra function list --json --limit 0 --fields address,size | Set-Content -Encoding utf8 $FunctionSizes
        Assert-LastExit "Reading original function sizes"
    }

    & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\generate-decomp-data-report.py") `
        --original (Join-Path $Root "original\toy2.exe") `
        --recompiled (Join-Path $Root "build\toy2.exe") `
        --pdb (Join-Path $Root "build\toy2.pdb") `
        --source-root (Join-Path $Root "src") `
        --output $DataReport
    Assert-LastExit "Comparing global data"

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
  candidates [args] Rank reconstruction candidates
  discover [args]   Find credible Ghidra starts absent from the function map
  evidence <addr>   Collect bounded evidence for a mapped or discovered target
  notes <query>     Search bounded codegen, debt, and original-name notes
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
  run [args]        Run the recompiled toy2.exe
  shell             Start cmd.exe with the VC6 environment active

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
if ($Command -eq "lint") {
    & python (Join-Path $Root "tools\decomp_lint.py") @CommandArgs
    Assert-LastExit "Checking source plausibility"
    exit 0
}
if ($Command -eq "notes") {
    & python (Join-Path $Root "tools\decomp_notes.py") @CommandArgs
    Assert-LastExit "Searching reconstruction notes"
    exit 0
}
if ($Command -in @("discover", "evidence")) {
    $Script = if ($Command -eq "discover") { "tools\decomp_discover.py" } else { "tools\decomp_evidence.py" }
    & python (Join-Path $Root $Script) @CommandArgs
    Assert-LastExit "Reading decompilation evidence"
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
    & python (Join-Path $Root "tools\decomp_verify.py") session-summary `
        (Join-Path $Root "build\decomp-baseline-report.json") `
        (Join-Path $Root "build\decomp-report-data.json") @CommandArgs
    Assert-LastExit "Summarizing the session"
    exit 0
}
if ($Command -eq "candidates") {
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
        $Report = Join-Path $Root "build\decomp-score-report.json"
        Write-ComparisonReport $Report
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") score $Report @CommandArgs
        Assert-LastExit "Classifying comparison results"
    }
    "bc" {
        $Full = $false
        $BcArgs = @()
        foreach ($Argument in $CommandArgs) {
            if ($Argument -eq "--full") { $Full = $true } else { $BcArgs += $Argument }
        }
        $CommandArgs = $BcArgs
        if ($CommandArgs.Count -ne 1 -or $CommandArgs[0] -notmatch '^0x[0-9A-Fa-f]{8}$') {
            throw "Usage: tools/decomp.ps1 bc [--full] <address>"
        }
        Build-Project
        $DiffDirectory = Join-Path $Root "build\decomp-diffs"
        New-Item -ItemType Directory -Force $DiffDirectory | Out-Null
        $Diff = Join-Path $DiffDirectory "$($CommandArgs[0]).txt"
        Push-Location (Join-Path $Root "build")
        try {
            & reccmp-reccmp --target TOY2 --no-color --verbose $CommandArgs[0] | Set-Content -Encoding utf8 $Diff
            Assert-LastExit "Comparing the target"
        } finally {
            Pop-Location
        }
        $DiffArgs = @($Diff)
        if ($Full) { $DiffArgs += "--full" }
        & python (Join-Path $Root "tools\decomp_diff.py") @DiffArgs
        Assert-LastExit "Formatting the comparison"
    }
    "baseline" {
        Build-Project
        $Report = Join-Path $Root "build\decomp-baseline-report.json"
        Write-ComparisonReport $Report
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") metadata `
            (Join-Path $Root "build\decomp-baseline-meta.json") --report $Report
        Assert-LastExit "Recording baseline metadata"
    }
    "validate" {
        $Baseline = Join-Path $Root "build\decomp-baseline-report.json"
        if (-not (Test-Path $Baseline)) { throw "No baseline exists. Run tools/decomp.ps1 baseline before you edit." }
        Build-Project
        $Current = Join-Path $Root "build\decomp-current-report.json"
        Write-ComparisonReport $Current
        $Targets = @()
        $AllowTargetRegression = $false
        $Staged = $false
        for ($Index = 0; $Index -lt $CommandArgs.Count; $Index++) {
            if ($CommandArgs[$Index] -eq "--target" -and $Index + 1 -lt $CommandArgs.Count) {
                $Index++
                $Targets += $CommandArgs[$Index]
            } elseif ($CommandArgs[$Index] -eq "--allow-target-regression") {
                $AllowTargetRegression = $true
            } elseif ($CommandArgs[$Index] -eq "--staged") {
                $Staged = $true
            } else {
                throw "Unknown validate argument: $($CommandArgs[$Index])"
            }
        }
        if ($Targets.Count -eq 0) { throw "Validate needs at least one --target address." }
        $VerifyArgs = @("validate", $Baseline, $Current) + $Targets
        $VerifyArgs += @("--metadata", (Join-Path $Root "build\decomp-baseline-meta.json"))
        if ($AllowTargetRegression) { $VerifyArgs += "--allow-target-regression" }
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") @VerifyArgs
        Assert-LastExit "Validating comparison results"
        if ($Staged) {
            & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_lint.py") --staged --warnings-as-errors
            Assert-LastExit "Validating staged source plausibility"
        }
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\ghidra_sync.py") check
        Assert-LastExit "Checking the function map"
        & git diff --check
        Assert-LastExit "Checking the working tree diff"
    }
    "experiment" {
        if ($CommandArgs.Count -lt 2) {
            throw "Usage: tools/decomp.ps1 experiment start|try|report <address> [label]"
        }
        $Action = $CommandArgs[0]
        $Address = $CommandArgs[1]
        $Directory = Join-Path $Root "build\decomp-experiments\$Address"
        if ($Action -eq "start") {
            New-Item -ItemType Directory -Force $Directory | Out-Null
            Build-Project
            $Report = Join-Path $Root "build\decomp-baseline-report.json"
            Write-ComparisonReport $Report
            & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") metadata `
                (Join-Path $Root "build\decomp-baseline-meta.json") --report $Report
            Assert-LastExit "Recording experiment baseline"
            & git diff --binary | Set-Content -Encoding utf8 (Join-Path $Directory "baseline.patch")
            Copy-Item (Join-Path $Root "build\decomp-baseline-meta.json") (Join-Path $Directory "compiler-context.json")
        } elseif ($Action -eq "try") {
            if ($CommandArgs.Count -ne 3 -or $CommandArgs[2] -notmatch '^[A-Za-z0-9._-]+$') {
                throw "Give the experiment a label with letters, numbers, dots, dashes, or underscores."
            }
            $Label = $CommandArgs[2]
            New-Item -ItemType Directory -Force $Directory | Out-Null
            Build-Project
            $Report = Join-Path $Root "build\decomp-experiment-$Address-$Label.json"
            Write-ComparisonReport $Report
            & git diff --binary | Set-Content -Encoding utf8 (Join-Path $Directory "$Label.patch")
            Push-Location (Join-Path $Root "build")
            try {
                & reccmp-reccmp --target TOY2 --no-color --verbose $Address | Set-Content -Encoding utf8 (Join-Path $Directory "$Label.diff.txt")
                Assert-LastExit "Comparing the experiment"
            } finally {
                Pop-Location
            }
            Copy-Item $Report (Join-Path $Directory "$Label.report.json")
            Copy-Item (Join-Path $Root "build\decomp-baseline-meta.json") (Join-Path $Directory "$Label.compiler-context.json")
            & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") experiment `
                (Join-Path $Directory "$Label.report.json") $Address (Join-Path $Directory "$Label.normalized.json")
            Assert-LastExit "Recording normalized experiment data"
            & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_verify.py") classify `
                (Join-Path $Directory "$Label.report.json") $Address | Tee-Object -FilePath (Join-Path $Directory "$Label.summary.txt")
            Assert-LastExit "Classifying the experiment"
        } elseif ($Action -eq "report") {
            Get-ChildItem $Directory -Filter "*.summary.txt" | Sort-Object Name | ForEach-Object {
                Write-Host $_.FullName
                Get-Content $_.FullName -TotalCount 4
            }
        } else {
            throw "The experiment action must be start, try, or report."
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
        Ensure-Build
        $DataReport = Join-Path $Root "build\decomp-data-report.json"
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\generate-decomp-data-report.py") `
            --original (Join-Path $Root "original\toy2.exe") `
            --recompiled (Join-Path $Root "build\toy2.exe") `
            --pdb (Join-Path $Root "build\toy2.pdb") `
            --source-root (Join-Path $Root "src") `
            --output $DataReport
        Assert-LastExit "Comparing global data"
        & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\decomp_data.py") @CommandArgs
        Assert-LastExit "Showing global-data evidence"
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
