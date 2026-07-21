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
- standard Unix utilities including Bash, `sed`, and `sha256sum`
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

The compatibility command `python3.11 build.py --nl` also builds the game, but
new workflows should use `tools/decomp` directly.

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
- Running the recompiled game may require data/configuration from a legitimate
  installation. These files are not required for compilation or comparison and
  must remain outside Git.
