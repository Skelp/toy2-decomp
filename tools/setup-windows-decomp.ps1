[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Tooling = Join-Path $Root ".tooling"
$MsvcBase = Join-Path $Tooling "msvc600-8168"
$MsvcSp3 = Join-Path $Tooling "msvc600-8447"
$ReferenceHash = "023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0"
$MsvcBaseCommit = "93d5bbe2582aa5b78a3a8dddd9498530fdd74061"
$MsvcSp3Commit = "0bc2b2684140e1516567a818baed13e8add1ff5f"

function Assert-LastExit([string] $Action) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Action failed with exit code $LASTEXITCODE"
    }
}

function Assert-Command([string] $Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command is missing from PATH: $Name"
    }
}

function Install-OverlayDirectory([string] $Source, [string] $Destination) {
    $Backup = "$Destination-8168"
    if (-not (Test-Path $Backup)) {
        if (-not (Test-Path $Destination)) {
            throw "Expected VC6 directory is missing: $Destination"
        }
        Move-Item $Destination $Backup
        New-Item -ItemType Directory -Force $Destination | Out-Null
    }
    Copy-Item (Join-Path $Source "*") $Destination -Recurse -Force
}

foreach ($Command in @("git", "cmake")) {
    Assert-Command $Command
}

$PythonLauncher = $null
$PythonPrefix = @()
if (Get-Command "py" -ErrorAction SilentlyContinue) {
    & py -3.11 -c "import sys; assert sys.version_info[:2] == (3, 11)"
    Assert-LastExit "Python 3.11 detection"
    $PythonLauncher = "py"
    $PythonPrefix = @("-3.11")
} elseif (Get-Command "python" -ErrorAction SilentlyContinue) {
    & python -c "import sys; assert sys.version_info[:2] == (3, 11)"
    Assert-LastExit "Python 3.11 detection"
    $PythonLauncher = "python"
} else {
    throw "Python 3.11 is required. Install it from python.org with the py launcher."
}

$ReferencePath = Join-Path $Root "original\toy2.exe"
if (-not (Test-Path $ReferencePath -PathType Leaf)) {
    throw "Reference executable not found: original\toy2.exe. Copy your genuine supported toy2.exe there before setup."
}
$ReferencePath = (Resolve-Path $ReferencePath).Path
$ActualHash = (Get-FileHash -Algorithm SHA256 $ReferencePath).Hash.ToLowerInvariant()
if ($ActualHash -ne $ReferenceHash) {
    throw "Reference hash mismatch. Expected $ReferenceHash, got $ActualHash"
}

New-Item -ItemType Directory -Force $Tooling | Out-Null

if (-not (Test-Path (Join-Path $MsvcBase ".git"))) {
    & git clone https://github.com/isledecomp/MSVC600-8168.git $MsvcBase
    Assert-LastExit "Cloning MSVC600-8168"
    & git -C $MsvcBase checkout --detach $MsvcBaseCommit
    Assert-LastExit "Pinning MSVC600-8168"
} else {
    $Head = (& git -C $MsvcBase rev-parse HEAD).Trim()
    Assert-LastExit "Reading MSVC600-8168 revision"
    if ($Head -ne $MsvcBaseCommit) {
        throw "Unexpected MSVC600-8168 revision in $MsvcBase. Remove it and rerun setup."
    }
}

if (-not (Test-Path (Join-Path $MsvcSp3 ".git"))) {
    & git clone https://github.com/isledecomp/MSVC600-8447.git $MsvcSp3
    Assert-LastExit "Cloning MSVC600-8447"
    & git -C $MsvcSp3 checkout --detach $MsvcSp3Commit
    Assert-LastExit "Pinning MSVC600-8447"
} else {
    $Head = (& git -C $MsvcSp3 rev-parse HEAD).Trim()
    Assert-LastExit "Reading MSVC600-8447 revision"
    if ($Head -ne $MsvcSp3Commit) {
        throw "Unexpected MSVC600-8447 revision in $MsvcSp3. Remove it and rerun setup."
    }
}

Install-OverlayDirectory (Join-Path $MsvcSp3 "VC98\include") (Join-Path $MsvcBase "VC98\Include")
Install-OverlayDirectory (Join-Path $MsvcSp3 "VC98\lib") (Join-Path $MsvcBase "VC98\Lib")
Install-OverlayDirectory (Join-Path $MsvcSp3 "VC98\atl\include") (Join-Path $MsvcBase "VC98\ATL\Include")
Install-OverlayDirectory (Join-Path $MsvcSp3 "VC98\mfc\include") (Join-Path $MsvcBase "VC98\MFC\Include")
Install-OverlayDirectory (Join-Path $MsvcSp3 "VC98\mfc\lib") (Join-Path $MsvcBase "VC98\MFC\Lib")

$Venv = Join-Path $Tooling "venv"
$VenvPython = Join-Path $Venv "Scripts\python.exe"
if ((Test-Path $Venv) -and -not (Test-Path $VenvPython)) {
    throw "The existing .tooling/venv is not a Windows Python environment. Move it aside and rerun setup."
}
if (-not (Test-Path $VenvPython)) {
    & $PythonLauncher @PythonPrefix -m venv $Venv
    Assert-LastExit "Creating Python environment"
}
& $VenvPython -m pip install reccmp==0.1.6 colorama==0.4.6
Assert-LastExit "Installing reccmp"

& $VenvPython (Join-Path $Root "tools\provision-directx.py") --root $Root
Assert-LastExit "Provisioning DirectX SDK files"

Push-Location $Root
try {
    & (Join-Path $Venv "Scripts\reccmp-project.exe") detect --search-path original
    Assert-LastExit "Registering reference executable"
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Windows decompilation environment is ready."
Write-Host "Build with:   powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 build"
Write-Host "Compare with: powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 compare"
