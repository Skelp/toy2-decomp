# Contributing

Thank you for helping reconstruct Toy Story 2. Contributions should improve the
readable source while preserving the ability to compare it with the supported
retail executable.

## Before starting

1. Fork and clone the repository with normal Git history.
2. Follow either the [Windows setup](docs/windows-decomp.md) or
   [Linux setup](docs/linux-decomp.md).
3. Confirm that the unmodified branch builds and that `compare` produces a
   report before changing source.

The supported reference is identified by SHA-256; filenames, timestamps, and
disc labels are not sufficient. Setup refuses a different executable.

## Working on a function

Function and global annotations associate reconstructed source with addresses
in the retail executable:

```cpp
// FUNCTION: TOY2 0x00401230
// GLOBAL: TOY2 0x00501230
```

Use `STUB` only when the current body intentionally stands in for unfinished
behavior. Preserve known addresses when moving code between files. Duplicate
annotations make progress data ambiguous and should be fixed before submission.

The useful feedback loop is:

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
different from reccmp accuracy: it measures how much of the function map has a
source annotation, not how closely the machine code matches.

## Submission checklist

- Format touched C/C++ files using the repository `.clang-format`.
- Build `toy2.exe` and `patcher.dll` successfully.
- Run reccmp and describe relevant accuracy changes in the pull request.
- Open `build/decomp-report.html` when a change affects multiple functions.
- When a change affects startup, loading, rendering, or other runtime behavior,
  run the platform runtime check described below using your own installation.
- Run `git diff --check`.
- Keep the change focused; do not mix generated files or unrelated cleanup into
  a function reconstruction.
- Verify `git status` does not contain game media or local analysis files.

Compiler warnings already exist in partially reconstructed code. New warnings
should be avoided unless they are required to reproduce original behavior and
explained in the change.

The `toy2decomp` executable always compiles retail behavior for reccmp. Runtime
convenience changes guarded by `APPLY_FIXES` are enabled only in `patcher.dll`;
do not define that macro globally or for the comparison executable.

## Files that must remain local

Do not commit or redistribute:

- `toy2.exe`, disc images, or extracted installations
- other retail game data
- `.tooling/`, `original/`, or `build/`
- locally provisioned DirectX headers/libraries
- PDBs, reccmp user mappings, or generated HTML/JSON reports
- Ghidra, IDA, or other personal analysis databases

The ignore rules cover the standard locations, but contributors are still
responsible for reviewing staged files.

## Changing build infrastructure

Windows and Linux intentionally use the same compiler, SP3 overlay, reference
hash, SDK snapshot, reccmp version, and report generator. Build changes should
keep both entry points equivalent:

- Windows: `tools/setup-windows-decomp.ps1` and `tools/decomp.ps1`
- Linux: `tools/setup-linux-decomp.sh` and `tools/decomp`

If a change cannot be exercised on both platforms, document which platform was
tested and perform at least a syntax/static review of the other path.

## Runtime validation

Runtime testing is distinct from reccmp validation and requires a complete,
legitimately owned installation. On Linux, use the bounded helper:

```sh
tools/smoke-test-linux.sh /path/to/installed-game
```

The helper verifies the retail executable hash, copies the installation to a
temporary directory, substitutes the rebuilt executable in that copy, and
temporarily configures both Wine registry views. It passes only after the game
reaches Direct3D driver selection without producing `toy2.err`. Registry state
is restored and the temporary copy is deleted even when the test fails.

On Windows, follow the runtime section in `docs/windows-decomp.md`. Never
overwrite the installation's retail `toy2.exe`; use a separately named copy of
the rebuilt executable.
