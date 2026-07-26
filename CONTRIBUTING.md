# Contributing

Thank you for your help. Contributions should improve the readable source. They
must also keep the ability to compare it with the supported retail executable.

## Before you start

1. Fork and clone the repository with normal Git history.
2. Follow either the [Windows setup](docs/windows-decomp.md) or
   [Linux setup](docs/linux-decomp.md).
3. Confirm that the unmodified branch builds and that `compare` produces a
   report before you change source.

The SHA-256 identifies the supported reference. Filenames, timestamps, and
disc labels do not suffice. Setup refuses a different executable.

## Working on a function

Function and global annotations associate reconstructed source with addresses
in the retail executable:

```cpp
// FUNCTION: TOY2 0x00401230
// GLOBAL: TOY2 0x00501230
```

Use `STUB` only when the current body intentionally stands in for unfinished
behavior. Preserve known addresses when you move code between files. Duplicate
annotations make progress data ambiguous. Fix them before you submit.

Use this feedback loop:

```text
build → compare → inspect the per-function diff → adjust source
```

On Linux:

```sh
tools/decomp build
tools/decomp compare --verbose 0x00401230
tools/decomp report
```

On Windows, substitute `tools/decomp.ps1` for `tools/decomp`.

Run `progress` to check annotation coverage. Its percentage is intentionally
different from reccmp accuracy. It measures how much of the function map has a
source annotation. It does not measure how closely the machine code matches.

## Submission checklist

- Format touched C/C++ files using the repository `.clang-format`.
- Build `toy2.exe` and `patcher.dll` successfully.
- Run reccmp. Describe relevant accuracy changes in the pull request.
- Open `build/decomp-report.html` when a change affects multiple functions.
- When a change affects startup, loading, rendering, or other runtime behavior,
  run the platform runtime check described below. Use your own installation.
- Run `git diff --check`.
- Keep the change focused. Do not mix generated files or unrelated cleanup
  into a function reconstruction.
- Verify that `git status` does not contain game media or local analysis files.

Compiler warnings already exist in partially reconstructed code. Avoid new
warnings unless you need them to reproduce original behavior. Explain any such
warning in the change.

The `toy2decomp` executable always compiles retail behavior for reccmp.
`patcher.dll` enables runtime convenience changes only when `APPLY_FIXES`
guards them. Do not define that macro globally or for the comparison
executable.

## Files that must remain local

Do not commit or redistribute:

- `toy2.exe`, disc images, or extracted installations
- other retail game data
- `.tooling/`, `original/`, or `build/`
- locally provisioned DirectX headers and libraries
- PDBs, reccmp user mappings, or generated HTML and JSON reports
- Ghidra, IDA, or other personal analysis databases

The ignore rules cover the standard locations. Still, review your staged files.

## Changing build infrastructure

Windows and Linux intentionally use the same compiler, SP3 overlay, reference
hash, SDK snapshot, reccmp version, and report generator. Keep both entry
points equivalent when you change the build:

- Windows: `tools/setup-windows-decomp.ps1` and `tools/decomp.ps1`
- Linux: `tools/setup-linux-decomp.sh` and `tools/decomp`

If you cannot exercise a change on both platforms, document which platform you
tested. Perform at least a syntax or static review of the other path.

## Runtime validation

Runtime testing is distinct from reccmp validation. It requires a complete,
legitimately owned installation. On Linux, use the bounded helper:

```sh
tools/smoke-test-linux.sh /path/to/installed-game
```

The helper verifies the retail executable hash. It copies the installation to
a temporary directory and substitutes the rebuilt executable in that copy. It
temporarily configures both Wine registry views. The test passes only after
the game reaches Direct3D driver selection without producing `toy2.err`. The
helper restores the registry state and deletes the temporary copy even when
the test fails.

On Windows, follow the runtime section in `docs/windows-decomp.md`. Do not
overwrite the installation's retail `toy2.exe`. Use a separately named copy of
the rebuilt executable.
