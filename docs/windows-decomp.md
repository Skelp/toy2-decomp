# Windows decompilation setup

This is the supported native Windows workflow. It uses a repository-local copy
of the original compiler rather than a machine-wide Visual Studio installation.

## Requirements

- 64-bit Windows 10 or 11
- Git for Windows
- CMake 3.20 or newer, available on `PATH`
- 64-bit Python 3.11 with the `py` launcher and `venv`
- network access during initial setup
- a genuine supported `toy2.exe`

The reference executable must have SHA-256:

```text
023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0
```

Use a normal Git clone. A source ZIP and a shallow clone omit the historical
DirectX SDK snapshot used by setup. The ImGui submodule is needed only for the
optional helper tools, but cloning it up front is convenient:

```powershell
git clone --recurse-submodules <repository-url>
cd toy2-decomp
```

## Initial setup

From PowerShell in the repository root:

```powershell
New-Item -ItemType Directory -Force original
Copy-Item "C:\path\to\your\toy2.exe" original\toy2.exe
powershell -ExecutionPolicy Bypass -File tools/setup-windows-decomp.ps1
```

Setup performs the following local operations:

1. verifies the reference hash at ignored `original/toy2.exe`;
2. downloads and pins the VC6 build 8168 compiler;
3. overlays the VS6 SP3 build 8447 headers and libraries;
4. provisions ignored DirectX 6/7 SDK files from this repository's history;
5. creates `.tooling/venv` with reccmp 0.1.6; and
6. writes the ignored reccmp user mapping.

The operation is safe to rerun. Provisioned files are beneath `.tooling/`,
`external/include/directx*`, and `external/libs/directx*`. Setup reads but does
not replace the contributor-owned file in `original/`.

## Build and comparison

```powershell
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 build
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 compare
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 report
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 progress
```

Outputs include:

- `build/toy2.exe` and `build/toy2.pdb`
- `build/patcher.dll` and its PDB
- `build/decomp-report.html`, a self-contained offline comparison dashboard

To inspect one function in the terminal:

```powershell
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 compare `
  --verbose 0x00401230
```

The compatibility command `python build.py --nl` also builds the game, but new
workflows should use `tools/decomp.ps1` directly.

## Runtime startup test

Runtime testing requires a complete, legitimately installed copy of the game;
`original/toy2.exe` alone is not sufficient. The original installer creates
`HKLM\Software\TravellersTalesToyStory2` with `path` and `cdpath` values, which
the game checks during startup.

Build first, then copy the rebuilt executable beside the installed retail
executable under a different name:

```powershell
$GameDir = "C:\Games\Toy Story 2"
Copy-Item build\toy2.exe (Join-Path $GameDir "toy2-decomp-smoke.exe")
Push-Location $GameDir
try {
    .\toy2-decomp-smoke.exe
} finally {
    Pop-Location
    Remove-Item (Join-Path $GameDir "toy2-decomp-smoke.exe") -ErrorAction SilentlyContinue
}
```

Confirm that the game reaches display/Direct3D initialization and does not
write a new `toy2.err`. Close the game after startup. This preserves the retail
`toy2.exe` and uses the runtime assets and registry configuration created by
the contributor's own installation. An extracted directory that has never
been installed must first be configured by its legitimate installer; do not
guess or globally distribute registry files.

## Optional helper tools

The injected/debugging utilities under `tools/` are separate from the matching
game build. They require the ImGui submodule, Ninja, and a modern 32-bit-capable
Clang installation:

```powershell
git submodule update --init --recursive
python build.py --tools --nl
```

They are not required for reccmp work.

## Notes and troubleshooting

- Ghidra is not required to compile or compare and is not installed by setup.
- A hash mismatch means the executable is a different release. Do not bypass
  the check; addresses and comparison results would be invalid.
- If PowerShell blocks scripts, the commands above use a process-local
  `-ExecutionPolicy Bypass` and do not change system policy.
- Python 3.12+ cannot replace Python 3.11 for the pinned reccmp environment.
- If the clone is shallow, run `git fetch --unshallow` before setup.
- Do not reuse a `build/` directory configured on Linux. Platform generators
  are different; move or remove the generated build directory before switching.
- `tools/decomp.ps1 run` launches `build/toy2.exe` directly and assumes runtime
  assets and registry configuration are already available. The procedure above
  is safer for validating against an installed copy because it also makes the
  installation-local compatibility DLLs available without replacing the
  retail executable.
