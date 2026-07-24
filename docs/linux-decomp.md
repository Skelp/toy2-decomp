# Linux decompilation setup

The Linux workflow runs the original 32-bit Windows compiler and linker through
Wine while CMake, Ninja, Python, and reccmp run natively.

## Requirements

- a normal, non-shallow Git clone
- Git and CMake 3.20 or newer
- Ninja
- Wine with support for 32-bit Windows executables (`wine`, `wineboot`,
  `wineserver`, and `winepath` on `PATH`)
- Python 3.11 with the `venv` module, exposed as `python3.11`
- standard Unix utilities including Bash, `awk`, `grep`, `realpath`, `sed`,
  `sha256sum`, and `timeout`
- network access during initial setup
- a genuine supported `toy2.exe`

The reference executable must have SHA-256:

```text
023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0
```

Distribution package names vary. Confirm the required commands before setup:

```sh
command -v git cmake ninja wine wineboot wineserver winepath python3.11 sha256sum
```

Modern Wine's WoW64 mode is supported; do not force `WINEARCH=win32`.

## Initial setup

Clone normally rather than downloading a source ZIP. The ImGui submodule is
needed only for optional Windows helper tools, but may be initialized now:

```sh
git clone --recurse-submodules <repository-url>
cd toy2-decomp
mkdir -p original
cp /path/to/your/toy2.exe original/toy2.exe
tools/setup-linux-decomp.sh
```

Setup:

1. verifies the ignored reference executable at `original/toy2.exe`;
2. creates a project-local Wine prefix;
3. downloads and pins VC6 compiler build 8168;
4. overlays VS6 SP3 build 8447 headers and libraries;
5. provisions ignored DirectX SDK files from this repository's history;
6. creates `.tooling/venv` with reccmp 0.1.6; and
7. creates the ignored reccmp user mapping.

It is safe to rerun with the same reference. No system Wine prefix or Python
environment is modified.

## Build and comparison

```sh
tools/decomp build
tools/decomp compare
tools/decomp report
tools/decomp progress
```

`build` configures CMake when needed, produces `build/toy2.exe`,
`build/toy2.pdb`, and `build/patcher.dll`, then registers the output with
reccmp. Compilation is serialized because concurrent Wine-hosted VC6 processes
are unreliable. Incremental Ninja builds remain enabled.

`compare` calculates per-function machine-code similarity. Inspect an
individual function with its original address:

```sh
tools/decomp compare --verbose 0x00401230
```

`report` writes `build/decomp-report.html` by default. It is a single offline
HTML file containing project/source treemaps, accuracy distribution, filtering,
and instruction-level assembly diffs. A different output path is optional:

```sh
tools/decomp report artifacts/toy2-report.html
```

`progress` counts annotations against `tools/Resources/functions_map.txt` and
can filter by namespace. It measures coverage, not machine-code accuracy:

```sh
tools/decomp progress Nu3D
```

### Exact metadata sync to Ghidra

`sync` builds a fresh reccmp/PDB manifest and accepts only raw 100% function
matches. Effective matches and stubs are reported but never applied. Preview a
function, apply selected functions, or audit already-synced metadata with:

```sh
tools/decomp sync diff --target 0x004a1bb0
tools/decomp sync apply --target 0x004a1bb0 --apply
tools/decomp sync verify --target 0x004a1bb0
```

`apply --all` handles every eligible exact function. Datatypes referenced by
the selected functions are imported recursively from the VC6 PDB; use
`--type-scope all` to import all project PDB aggregates. Apply is dry-run by
default and commits function metadata, datatypes, and source maps in one Ghidra
transaction. The CLI bridge is stopped for the headless transaction and
restarted afterward. The Ghidra project must contain the configured retail
executable and its SHA-256 must match `reccmp-project.yml`.

Function bodies remain the retail instructions in Ghidra. Exact instruction
pairs are associated with their PDB source lines through Ghidra source maps,
including a SHA-256 identity for each mapped source file. A targeted apply
replaces only that function's owned ranges; other source mappings are retained.

The compatibility command `python3.11 build.py --nl` also builds the game, but
new workflows should use `tools/decomp` directly.

## Reproducible runtime smoke test

Compilation and comparison need only `original/toy2.exe`. A startup test also
needs a complete, legitimately owned installed-game directory containing at
least `toy2.exe`, `validate.tta`, and `data/`. Do not put that directory inside
this repository.

After building, run:

```sh
tools/smoke-test-linux.sh /path/to/installed-game
```

The optional second argument changes the default 20-second timeout:

```sh
tools/smoke-test-linux.sh /path/to/installed-game 30
```

The helper verifies that the installation contains the supported retail
executable, stages a temporary copy under `/tmp`, replaces only the copy's
executable with `build/toy2.exe`, and temporarily writes the installer's
`path`/`cdpath` values to both Wine registry views. It succeeds when the game
reaches Direct3D driver selection without creating `toy2.err`. A game window
may appear briefly. Existing registry keys are backed up and restored, and the
temporary installation is removed on success, failure, or interruption.

The test uses the project-local Wine prefix created by setup. Harmless
`libEGL`/DRI warnings may still appear; the success check is based on the game's
own initialization output and fatal-error file.

## Environment and troubleshooting

Use `tools/decomp shell` or source `tools/linux-decomp-env.sh` to run individual
compiler/reccmp commands in the configured environment.

- Ghidra is not required to compile or compare and is not installed by setup.
- A reference hash mismatch indicates an unsupported release; do not bypass it.
- Python 3.12+ cannot replace Python 3.11 for this pinned reccmp environment.
- If setup reports that the SDK snapshot is missing, run
  `git fetch --unshallow` and retry.
- Harmless `libEGL` warnings may appear when Wine starts without a usable DRI
  display. They do not affect compilation or comparison.
- Do not reuse a `build/` directory configured by native Windows CMake.
- `tools/decomp run` only launches `build/toy2.exe`; it does not stage runtime
  assets or registry configuration. Prefer `tools/smoke-test-linux.sh` for the
  reproducible startup check.
