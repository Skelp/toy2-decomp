# Toy Story 2 PC decompilation

This project reconstructs the Windows release of *Toy Story 2: Buzz Lightyear
to the Rescue* as readable C++ and measures the result against the original
executable. It is an incomplete research project, not a replacement for the
game.

The repository does not contain the game or any extracted game assets. To run
the comparison workflow, contributors must provide their own genuine copy of
the supported `toy2.exe`:

```text
SHA-256  023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0
```

No disc image is needed to compile or compare the program. Other files from an
installed copy may be needed only if you choose to run the recompiled game.

## Quick start

Clone the repository normally rather than downloading a source ZIP. The setup
uses a historical SDK snapshot already present in the Git history.

### Windows

Install Git, CMake 3.20 or newer, and 64-bit Python 3.11. Then run PowerShell
from the repository root:

```powershell
New-Item -ItemType Directory -Force original
Copy-Item "C:\path\to\your\toy2.exe" original\toy2.exe
powershell -ExecutionPolicy Bypass -File tools/setup-windows-decomp.ps1

powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 build
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 compare
powershell -ExecutionPolicy Bypass -File tools/decomp.ps1 report
```

Visual Studio 6 does not need to be installed globally. Setup provisions the
pinned compiler locally.

See [the complete Windows guide](docs/windows-decomp.md).

### Linux

Install Git, CMake 3.20 or newer, Ninja, Wine with 32-bit executable support,
and Python 3.11 with `venv`. Then run:

```sh
mkdir -p original
cp /path/to/your/toy2.exe original/toy2.exe
tools/setup-linux-decomp.sh

tools/decomp build
tools/decomp compare
tools/decomp report
```

See [the complete Linux guide](docs/linux-decomp.md).

## Daily commands

Windows commands use `tools/decomp.ps1`; Linux commands use `tools/decomp`.
Both expose the same workflow:

| Command | Purpose |
| --- | --- |
| `build` | Compile `toy2.exe` and `patcher.dll` with VC6 SP3 |
| `compare` | Run detailed reccmp machine-code comparison |
| `report` | Generate `build/decomp-report.html` |
| `progress [namespace]` | Count source annotations against the function map |
| `run` | Run the recompiled executable; original runtime data may be required |
| `shell` | Open a shell with the local compiler environment active |

The report is a single offline HTML file with project/source treemaps,
function filtering, accuracy distributions, and instruction-level diffs.

## Toolchain identity

The reference executable records Visual C++ 6 compiler build 8168 and Visual
Studio 6 SP3 product build 8447 in its Rich header. Both platform setup scripts
therefore use:

- `CL.EXE` 12.00.8168 and `LINK.EXE` 6.00.8168
- Visual Studio 6 SP3 build 8447 headers and libraries
- the DirectDraw/Direct3D-era SDK headers and import libraries used by the
  existing project
- reccmp 0.1.6 in a repository-local Python environment

All downloaded or reconstructed dependencies live in ignored directories.
Ghidra is useful for analysis but is not installed or configured by setup.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before sending changes. In particular,
never commit game executables, disc images, extracted installations, SDK output,
PDB files, or generated comparison reports.
