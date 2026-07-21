[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("configure", "build", "compare", "report", "progress", "run", "shell", "help")]
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

function New-DecompReport([string] $Output = "build\decomp-report.html") {
    Ensure-Build
    $ReportJson = Join-Path $Root "build\decomp-report-data.json"
    $ReportSummary = Join-Path $Root "build\decomp-report-summary.txt"
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

    & (Join-Path $VenvScripts "python.exe") (Join-Path $Root "tools\generate-decomp-report.py") `
        --input $ReportJson `
        --summary $ReportSummary `
        --source-root (Join-Path $Root "src") `
        --functions-map (Join-Path $Root "tools\Resources\functions_map.txt") `
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
  compare [args]    Run reccmp against the reference and recompiled EXEs
  report [file]     Generate the self-contained HTML decompilation dashboard
  progress [scope]  Show annotation progress, optionally for a namespace
  run [args]        Run the recompiled toy2.exe
  shell             Start cmd.exe with the VC6 environment active
"@
}

Set-Location $Root
if ($Command -eq "help") {
    Show-Help
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
    "report" {
        if ($CommandArgs.Count -gt 1) {
            throw "Usage: tools/decomp.ps1 report [output.html]"
        }
        $Output = if ($CommandArgs.Count -eq 1) { $CommandArgs[0] } else { "build\decomp-report.html" }
        New-DecompReport $Output
    }
    "progress" {
        if ($CommandArgs.Count -gt 1) {
            throw "Usage: tools/decomp.ps1 progress [namespace]"
        }
        $ProgressArgs = @("decomp_utils.py", "--progress")
        if ($CommandArgs.Count -eq 1) { $ProgressArgs += $CommandArgs[0] }
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
