# Repository guidance

This file is the normative reference for **judgment**: what good reconstructed
source looks like, how to choose work, and what the evidence rules are. The
`continue-decomp` skill holds the **procedure**. The two do not repeat each
other.

## How to read this file

Read "Project goal", "Autonomous work selection", and "Reconstructing plausible
source" before you start. Read the rest when the task reaches it. Each section
is independent.

| Section | Read it when |
| --- | --- |
| Project goal | Always. One paragraph. |
| Reference material | You want a name or a structure from OpenCrashWOC. |
| Studio and engine context | You must judge whether an idiom is period-plausible. |
| Writing style | You write prose, a note, or a comment. |
| Decompilation workflow | You need the command list. |
| Autonomous work selection | Always, before you pick a target. |
| Function map and Ghidra project | You rename, add a map entry, or wonder which name source wins. |
| Ghidra CLI | You need a query beyond `tools/decomp evidence`. |
| Naming and typing precedence | Always, before you name anything. |
| Reconstructing plausible source | Always, before you write source. |
| Using compiler comparison productively | You are in the build-compare loop. |
| Ghidra sync | A sync fails or you change the vendored reccmp. |
| Validation | Before every handoff. |
| Repository hygiene | You add a file or touch build configuration. |

## Project goal

This project reconstructs the Windows release of *Toy Story 2: Buzz Lightyear
to the Rescue* as readable C++. It measures the generated VC6 code against the
supported retail executable. Prefer source that resembles what the original
developers likely maintained: coherent types, descriptive names, natural
control flow, and engine-level abstractions. Machine-code similarity is
evidence, not the sole objective.

## Naming and typing precedence

The retail binary keeps its assert and log text, and that text quotes the
expressions the original developers wrote. It therefore hands over real names.
`.notes/original-names.md` collects them. Regenerate it with `tools/decomp
names`.

Use this order. A higher source always wins.

1. **A name the retail binary states in a string.** `drawb->VerticeCount[i]`
   gives a structure pointer, a field name, the element type's role, and proof
   the member is an array. `d3dappi.lpFrontBuffer` names a global and a member.
   Never invent a name for something the binary already names.
2. **A name from the Wrath of Cortex DWARF dump**, subject to the version
   caveat in "Studio and engine context".
3. **A role-based name inferred from behavior** confirmed across several uses.
   Prefer a modest accurate name over a specific speculative one.
4. **`g_unkNNNNNN`**, allowed only for a global whose role is genuinely
   unexplained. It is never acceptable for a function parameter, and it is a
   defect in a structure field whose role you already know.

A name must state the value's **role**, not the arithmetic that produced it.
`g_destRectWidthTimes1024Minus1` and `g_destRectHalfWidthCopy` describe a
computation. Say what reads the value instead.

The binary also names its own translation units. A
`Logger::GetErrorHandler(file, line)` site holds the original path, for example
`C:\projects\nu3d\objload.c`, and the cross-references to that string identify
every function in that unit. `.notes/original-names.md` tabulates them. When a
function's destination file is in question, that table is evidence, so prefer it
over a judgment call.

**An exact machine-code match is not sufficient.** A function that matches at
100% while stating byte offsets where a name belongs is a matched
transliteration, not a reconstruction. Run `tools/decomp lint`. Declare the
structure, or leave the function a `STUB` for a session that can.

## Reference material

- A related Nu3D codebase is available locally at
  `/run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC`.
- Use OpenCrashWOC to infer likely Nu3D terminology, function families, data
  roles, and source-level structure.
- Toy Story 2 likely uses a different Nu3D version. Do not copy behavior
  blindly. Do not assume layouts, signatures, constants, ordering, or
  implementation details are identical. Confirm conclusions against Toy Story 2
  disassembly, callers, data, and reccmp output.
- Preserve this repository's established names when they are already well
  supported. Adopt reference names when they materially improve accuracy or
  readability and fit Toy Story 2 evidence.

## Studio and engine context

Traveller's Tales developed *Toy Story 2: Buzz Lightyear to the Rescue* (1999)
for Activision on PlayStation, Windows, Nintendo 64, and Dreamcast. Do **not**
assume the studio co-targeted the Nu3D engine itself for PSX.

The studio came from 16-bit and early-32-bit console software 3D (*Leander*,
*Mickey Mania*, *Toy Story*, *Sonic 3D Blast*, *Sonic R*). This is why Nu3D math
favors integer LUT lookups, manual round-toward-zero shifts, and IEEE 754 bit
manipulation over CRT calls. Keep those idioms as period-plausible. Do not
replace them with modern `float` or `<cmath>` equivalents unless Toy Story 2
disassembly shows the retail did so.

Nu3D is the studio's proprietary 3D engine, also used in *Crash Bandicoot: The
Wrath of Cortex* (2001) and *Finding Nemo* (2003). The engine changed between
releases. Toy Story 2's `numath` uses an **int16 fixed-point** trig LUT with
12-bit angles (`& 0xFFF`, `+0x400` for cosine). Wrath of Cortex uses a **float**
LUT with 16-bit angles (`0x10000` range). Treat OpenCrashWOC as a later,
related but different version.

No original source was ever released. The closest artifact is the Wrath of
Cortex alpha DWARF dump at
`OpenCrashWOC/code/src/dump_alphaNGCport_DWARF.txt`. It preserves original
symbol and type names (`numtx_s`/`NUMTX`, `NuTrigTable`, `NuMtx*`, `NuTrig*`).
Use it to corroborate names and types, subject to the version difference above.

## Writing style

Write notes under `./.notes/`, edits to `README.md` or `CONTRIBUTING.md`, and
C/C++ code comments in ASD-STE100 Simplified Technical English. Follow the
`ste-writing` skill at `./.pi/skills/ste-writing/SKILL.md`. Use the STE-flavored
mode for README and CONTRIBUTING prose, and the strict mode for procedure
steps and error or warning text. The skill rules apply to prose and comments
only. They do not apply to code, identifiers, or command syntax.

## Decompilation workflow

Use the repository tools from its root. Run `tools/decomp help` for the full
list.

```sh
tools/decomp candidates              # ranked targets; no Ghidra, no reccmp run
tools/decomp candidates Nu3D --stubs --why
tools/decomp candidates --for 0x00401230 --why # dependency frontier for one goal
tools/decomp defer 0x00401230 --blocked-by 0x00405670 --reason "needs the producer layout"
tools/decomp blockers                # show local blockers
tools/decomp undefer 0x00401230      # clear local blockers for one target
tools/decomp audit --legacy-caps --why # review old mismatch claims
tools/decomp audit --status             # show freeze-audit completion
tools/decomp audit --refresh-ledger     # refresh scores and preserve audit notes
tools/decomp evidence 0x00401230     # one bounded evidence bundle
tools/decomp evidence 0x00401230 --disasm
tools/decomp build
tools/decomp baseline                # build and save the pre-edit comparison
tools/decomp bc 0x00401230           # build, then the verbose comparison
tools/decomp score 0x00401230 ...    # exact/effective/tool/provisional verdicts
tools/decomp experiment start 0x00401230
tools/decomp experiment try 0x00401230 natural-loop
tools/decomp lint                    # source plausibility; the compare cannot see it
tools/decomp lint --staged           # only what you are about to commit
tools/decomp names                   # re-extract .notes/original-names.md
tools/decomp compare --verbose 0x00401230
tools/decomp compare                 # whole-binary summary
tools/decomp progress
tools/decomp check
tools/decomp sync
tools/decomp report
tools/decomp session-summary 0x00401230 ...
tools/decomp validate --target 0x00401230 --staged
```

During the audit freeze, `tools/decomp candidates` shows only required pending
audits. Run `tools/decomp audit --status` to measure the remaining scope. A
placeholder uncertainty or a general revisit instruction does not complete an
audit. Use `tools/decomp audit --refresh-ledger` after a fresh report. This
command updates scores and source debt, but preserves manual audit evidence.
Remove `tools/Resources/audit-freeze.txt` only when `tools/decomp audit --status
--check` succeeds.

Start selection with `candidates` and evidence with `evidence`. Candidate
selection reads the retail binary, committed files, and build reports. It does
not call Ghidra or reccmp. The evidence command makes bounded Ghidra queries.
Do not replace these commands with map greps or repeated decompiler calls.

The tools print no environment banner and no driver warning. When you filter
their output, filter for what you want, and do not add noise filters.

## Autonomous work selection

Do not choose work by address order, function size alone, or the largest
apparent progress gain. Choose a coherent dependency-frontier function. The
repository must contain enough evidence to recover plausible source for it.

At the start of a session:

1. Run `git status --short`. Do not overlap unrelated local changes.
2. Run `tools/decomp progress`. Use `tools/decomp progress <namespace>` when
   you evaluate a subsystem.
3. Read `tools/Resources/functions_map.txt`, the relevant headers and source,
   and the neighboring map entries. Search an address in `src/` before you
   assume it is not started.
4. If a current comparison exists, generate or open `build/decomp-report.html`.
   The dashboard is a convenient index. It is not a substitute for
   disassembly.
5. Establish a baseline before you edit. Run `tools/decomp baseline`. This
   saves the build identity and the full comparison report that `validate`
   uses to detect regressions.

Useful discovery searches include:

```sh
rg -n "Toy2::Camera::" tools/Resources/functions_map.txt
rg -n "0x00401230|StepEventTrack" src tools/Resources/functions_map.txt
rg -n "Camera|Cutscene" /run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC
```

Treat map and reference names as strong leads. They are not proof of a full
signature or behavior. They are maintainer RE labels. No PDB ships with the
binary, and there are no PE exports. Two functions that share a name stem do
not necessarily have identical signatures.

Before editing, keep a short working evidence summary. It should contain the
candidate address, ownership, inferred ABI, callers and callees, relevant
fields, reference analogue, and unresolved questions. Keep it in agent working
notes. Do not add speculative analysis files to the repository.

Build a candidate list with `tools/decomp candidates`. For new work, the tool
extracts direct calls and tail jumps from the retail binary. It combines these
edges with source state and declared prerequisites. It collapses recursive
groups before it finds the dependency frontier.

The new-work ranking implements this order:

1. A dependency-frontier function that immediately unlocks unfinished callers.
2. A frontier function that contributes to several large unfinished callers.
3. A `STUB`, then an unannotated function, with stronger dependency evidence first.
4. A smaller body when two candidates have the same dependency impact.

`--new-work` has no function-size limit. A large function becomes eligible when
its unresolved function dependencies are complete. Use `candidates --for
<address> --why` to inspect one goal. The command returns its recursive
dependency frontier, or the goal itself when it is ready.

The graph contains direct function dependencies only. It reports indirect
calls and jumps as uncertainty. Use evidence and explicit blockers for type,
global, and indirect-dispatch dependencies. A provisional dependency below 75
percent is weak evidence, but it is not an unresolved function.

The tool cannot decide whether a source model is plausible. Use `--why`, then
confirm the top candidate with `tools/decomp evidence`. A legacy CAP claim does
not prove that a mismatch is acceptable. Only a verified tool-artifact row can
suppress a mismatch.

Prefer candidates with several of these properties:

- The function name and likely destination TU are already supported by the
  function map and surrounding source.
- Callers and callees are known. At least one side of the call boundary has
  readable source.
- Data accesses can be expressed using established structs or a layout that
  you can confirm from several functions.
- Strings, imports, constants, or a close OpenCrashWOC analogue reveal intent.
- The control flow is bounded and has few unresolved indirect calls.
- When you finish the function, it unlocks neighboring functions or replaces a
  widely used stub.

Defer a candidate when its behavior depends on an unknown structure, indirect
dispatch, runtime machinery, or unnamed callees. If one function can resolve
the problem, record its address with `--blocked-by`. The candidate system then
promotes that prerequisite and reactivates the target after it becomes a
`FUNCTION`.

Use a manual blocker only when no function address represents the missing
evidence. Do not fill an opaque body with guessed fields to replace a `STUB`.

Record a function prerequisite with `tools/decomp defer <target> --blocked-by
<prerequisite> --reason <text>`. You can repeat `--blocked-by`. The command
writes to the ignored local blocker record under `build/`.

A reason without `--blocked-by` creates a manual blocker. Use `tools/decomp
undefer <target>` to clear it. Use `--include-blocked` only to inspect blocked
targets. `--include-deferred` remains as a compatibility alias.

Choose the TU by subsystem and ownership, not simply by address proximity. Use
the namespace and name in `functions_map.txt`, existing declarations, callers,
and neighboring source. Extend an established TU when it owns the same
abstraction. Create or populate a separate TU only when the repository
organization clearly supports it. A useful session-sized scope is one function
or a tightly coupled group with the required header or layout changes. Avoid
mixing unrelated easy functions from several TUs.

Treat a source-file move as a reconstruction claim. Use retail path strings as
the strongest ownership evidence. Then use a coherent namespace, shared state,
call relationships, and contiguous address clusters. Use later Nu3D source only
as supporting evidence. Keep level-specific code in the matching file under
`src/Toy2/` when these sources agree. Keep shared game code in an established
core TU. Do not split a file because of its line count or namespace-block count
alone. Multiple namespaces can belong in one TU when they share proven
ownership. Preserve every address annotation when you move a definition.

Keep large constant initializers out of `.cpp` files. A string is large when
its decoded payload contains at least 160 bytes, excluding its terminating null
byte. A non-string array is large when it contains at least 32 elements and its
initialized object contains at least 256 bytes. Put each large initializer in
one named `.inc` file beside its owning `.cpp` file. Keep the declaration,
address annotation, type, linkage, and initializer braces in the `.cpp` file.
The `.inc` file must contain initializer tokens only. List the `.inc` file in
the owning target's CMake source list. Keep short text, small lookup tables, and
arrays of a few string pointers in source unless stronger source evidence says
otherwise.

When multiple candidates remain, choose the one with the best evidence and the
fewest unresolved dependencies. Accuracy percentage is only a tie-breaker. A
short near-match can be useful when its diff exposes a shared type problem. A
random low match or tiny cosmetic gain usually is not.

### Minimal candidate checklist

Before editing, you should be able to answer the following **mostly**. The
threshold is "good enough to start writing", not "fully understood". The
build-compare loop resolves the rest.

- What subsystem and TU own this function?
- What are the calling convention, return type, and parameter roles?
- Which global or structure fields does it access? What evidence supports
  their types and offsets?
- What are its callers, callees, important constants, and observable side
  effects?
- What source-level control flow best explains the branches?
- Which facts are confirmed? Which names or types remain hypotheses?

One `tools/decomp evidence` call answers most of these. If the answers are
mostly unknown after that call, skip the function. Do not improvise.

A supported blocker is a valid session result. Do not implement an opaque body
to meet a time, tool-call, progress, or commit target. Record a prerequisite
address when one exists.

You do **not** need every field type confirmed, every callee resolved, or the
complete struct layout before you start. Surveying candidates without
committing to one is the most common way to spend a session and produce
nothing.

If the preferred new work is not supported, use this fallback order:

1. Select the next evidence-backed dependency-frontier target.
2. Fix a supported type, layout, or lint debt item.
3. Record a function or manual blocker.
4. Stop when every frontier target has a supported blocker.

Do not use provisional-score polishing as a fallback. Select a dependency-ready
large function when its evidence checklist passes. Complete that function in
the same uninterrupted session. Keep it as a `STUB` until the body is complete.

## Function map and Ghidra project

`tools/Resources/functions_map.txt` and the local Ghidra project are **two
distinct name sources. Do not confuse them.**

- `functions_map.txt` is the **committed, hand-maintained** address-to-name
  ledger. It originated as an IDA function-address dump, and maintainers apply
  its names as reverse-engineering labels. These names are not original
  symbols. No PDB ships with the binary. There are no PE exports. The names do
  not appear in the Wrath of Cortex DWARF dump. Its **addresses are
  authoritative**: real function starts that cover the game and engine `.text`
  section, minus deliberately excluded CRT and imports. Its **names are working
  hypotheses** that change as understanding deepens. There is no generator.
  When you reconstruct, rename, or newly discover a function, update its map
  entry to match the established sorted, deduplicated convention. Source
  annotations alone do not update the map.
- The **Ghidra project is local-only, untracked, and reconstructable** from
  committed inputs (`original/toy2.exe` + source annotations + reccmp
  comparison). It is a scratch analysis surface, not a source of truth.
  Durable naming happens in the map and, when matched, in source annotations.
  Names applied only to a local Ghidra session are lost on the next
  reconstruction. Naming flows source/map → Ghidra, never the reverse.
- Consequently the map is **necessarily richer** than synced Ghidra state. It
  holds every function address. This includes STUBs and addresses reccmp has
  not yet paired. `sync` only imports matched functions. Run `tools/decomp
  check` after you edit the map. This verifies that the map stays sorted and
  deduplicated. It also verifies that every address is named and falls within
  the retail executable's `.text` section.

## Ghidra CLI

Treat Ghidra output as evidence, not source to paste.

`tools/decomp evidence <addr>` already bundles the decompilation, the map
neighbors, the callers, the callees, and the referenced data addresses. Prefer
it. Add `--disasm` when the decompilation looks wrong. Reach for the `ghidra`
CLI directly only for a query the bundle does not cover:

```sh
ghidra decompile 0x00401230 --with-params --with-vars
ghidra function get 0x00401230
ghidra function disasm 0x00401230
ghidra x-ref to 0x00501230
ghidra x-ref from 0x00401230
```

Note that `ghidra function calls <addr>` reports **callers**, not callees. Take
the callee list from the disassembly, as the evidence bundle does.

Use `ghidra <command> --help` if a query needs filtering or JSON output.
Inspect both callers and callees. The decompiler frequently guesses
signedness, pointer depth, array shapes, calling conventions, and `this`
incorrectly. A field interpretation is much stronger when the same offset has
the same role across several functions. Check raw disassembly whenever the
decompilation has odd casts, merged variables, suspicious `goto`s, or an
implausible signature.

## Reconstructing plausible source

Work from the outside in:

1. Establish the ABI from call sites and the epilogue. Identify the calling
   convention, argument order and widths, return value, and whether a hidden
   `this` is present.
2. Identify globals and layout accesses. Reuse established repository types.
   Change a struct only after you check every visible use of the affected
   offsets and add size and offset assertions when the project convention
   supports them.
3. Recover the control-flow skeleton from branches and side effects before you
   name every local. Separate early exits, loops, state dispatch, and cleanup.
4. Assign names using the precedence in "Naming and typing precedence". Check
   `.notes/original-names.md` before you invent anything: the retail strings
   may already name the structure you are about to describe with offsets.
5. Compare against related OpenCrashWOC code for terminology and architecture.
   Then confirm every borrowed conclusion against Toy Story 2.
6. Implement the simplest natural C++ that explains all observed behavior.
   Build it. Use the machine-code diff to test the model.

Decompiler temporaries such as `iVar1`, `puVar2`, and fields named only by an
offset are analysis placeholders. They are not acceptable final source.
Replace them with role-based locals and types when the role is supported.
Prefer structured `if`, `switch`, and loop constructs that match the branch
graph. Retain a `goto`, raw offset access, or unknown field only when the
binary genuinely requires it or the evidence is insufficient for a safer
abstraction. Isolate it. Explain the uncertainty briefly.

Preserve exact observable behavior. This includes update order, aliasing,
integer width and signedness, floating-point evaluation, callback order,
ownership, and failure paths. Do not "fix" retail bugs in the comparison
build. Avoid inventing abstractions, enums, inheritance, or helper functions
that repeated usage or known source boundaries do not support. Conversely, do
not leave repeated, well-supported engine concepts as raw pointer arithmetic
just because that superficially resembles the disassembly.

Headers are part of the reconstruction. Keep declarations, definitions, and
shared types coherent. Inspect downstream users after you change them. Do not
use casts to hide a wrong signature or layout. A compile failure or widespread
comparison regression after a type change is evidence that you must revisit
the model.

### Using compiler comparison productively

Use this loop on the selected function or cluster:

```text
inspect evidence -> write plausible source -> format -> build -> compare the
target address -> explain the diff -> revise the source model
```

Run `tools/decomp bc <address>` after each meaningful revision. It builds and
then prints the verbose comparison for that address. Use `tools/decomp score
<address>...` when you only need the percentages, for example to confirm that a
header change did not regress a cluster. Re-run the full `tools/decomp compare`
after header or layout changes. They can affect many functions.

Change one source-level idea at a time. Use `tools/decomp experiment` to retain
the patch, compiler identity, report, and result for each attempt. Stop an
experiment when the tested forms no longer answer a source-model question.
The number of attempts is not evidence that the mismatch is a compiler quirk.

The loop is the instrument. Use it instead of deliberation. If you have reasoned
at length about one function and written nothing, write the simplest form that
fits the evidence and build it. When two forms both fit, take the simpler one,
build it, and record the alternative. A build answers a design question in under
a minute, and it answers it with evidence rather than preference.

Expect small inconsistencies in the evidence. An array with one more entry than
its loop bound, an oversized buffer, or an unused slot is ordinary in retail
code. Such a detail is not proof that a layout is wrong. Choose the reading that
satisfies the arithmetic, note the oddity, and let the comparison test it.

Before you theorize about a diff, check `.notes/codegen-index.md`. It has one
line per known symptom. A `FIX-nn` row suggests a source form to test. A
`CAP-nn` row is a legacy observation, not a proof or an exemption. Use
`tools/decomp audit --legacy-caps` to review those claims.

Start with semantic and structural agreement. Then improve code generation.
Common diff signals include:

- Wrong stack cleanup, argument loads, or call setup: recheck the signature
  and calling convention.
- Signed versus unsigned conditional branches: recheck field and local types.
- Repeated wrong field offsets or strides: recheck the containing layout and
  array element type.
- Different calls or side-effect ordering: recheck control flow, short-circuit
  expressions, and cleanup paths.
- A very different stack frame or register lifetime: look for wrong variable
  scopes, reused temporaries, aliased pointers, or an incorrect high-level
  structure.
- Isolated scheduling or register differences with otherwise identical
  behavior: consider them weak evidence unless a natural source rewrite
  explains them.

Do not perform unexplained expression shuffling, redundant assignments,
strange casts, or unnatural branch inversions solely to raise a score. The
same source compiles many ways. A higher diff percentage can come from
semantically meaningless register-allocation changes. Treat a score gain as
evidence of a better *model* only when the edit is credible original source
that explains a real structural idea. It must not be a coincidental regalloc
win. A matching trick is acceptable only when it remains credible original
source and does not hide the data model. If several plausible forms exist,
prefer the clearer form until comparison or reference evidence distinguishes
them.

After a correct readable implementation is in place, you may stop short of
100% when remaining differences appear compiler-incidental. Record the
remaining mismatch and evidence in the handoff. Do not degrade the source to
chase the last percentage. Never add `[MATCHED]` based on visual confidence
alone.

Do not add new rows to `.notes/caps-registry.tsv`. Record unsuccessful natural
source forms with `tools/decomp experiment`. A mismatch remains provisional
unless it is exact, reccmp-effective, or a verifier-confirmed tool artifact.

Do not cycle functions that already sit above about 90% looking for a
source-fixable diff. When the remaining differences are import-thunk naming,
data labels, or register allocation, that work produces no reconstruction.
Switch to a `STUB` or an unannotated leaf instead.

Stop a related cluster when two siblings remain below 50 percent with the same
source model. Treat the common low score as evidence against the shared layout,
macro expansion, or control flow. Resolve that model before another sibling.

Validation rejects a new target below 75 percent by default. Leave the target
as a `STUB` unless a maintainer accepts the complete behavior and data model.
Scores below 50 percent remain exceptional and need the same explicit review.
For an accepted exception, set the audit-ledger origin to `maintainer-review`.
Record the current score, tested natural forms, measured scores, specific
uncertainty, and a conditional revisit trigger. Then use `tools/decomp validate
--allow-low-score`.

Each new provisional target needs a complete audit-ledger row during
validation. An `initial-audit` row or a general placeholder does not pass.

## Ghidra sync

`tools/decomp sync` drives upstream reccmp's headless Ghidra importer
(`reccmp-ghidra-import`). It imports every matched function, global, vftable,
and PDB type into the local Ghidra project in a single transaction. reccmp is
vendored as a pinned submodule at `external/submodules/reccmp`. It is
installed editable so the local patches in `tools/patches/` take effect:

- `reccmp-0.1.6-want-curly-fix.patch` — the reccmp parser's `WANT_CURLY` state
  silently gets stuck on one-line function bodies. This patch is still
  required on upstream master.
- `reccmp-union-write.patch` — upstream only dereferences existing unions and
  aborts otherwise. This patch implements the union write path so the importer
  can create PDB unions.
- `reccmp-variadic-functions.patch` — upstream rejects PDB signatures that use
  a trailing `T_NOTYPE` variadic marker. This patch imports the fixed
  parameters and sets Ghidra's variadic flag.
- `reccmp-containing-global-refresh.patch` — upstream removes data only when
  its start matches the new global. This patch also removes stale generated
  data that contains a corrected global's start address.

```sh
tools/decomp sync          # import all matched entities into Ghidra
tools/decomp sync --debug   # forward extra arguments to reccmp-ghidra-import
```

The `ghidra` CLI configuration holds the Ghidra project location
(`ghidra_install_dir`, `ghidra_project_dir`, `default_project`,
`default_program`). Only one process can open a local project at a time.
Therefore `sync` stops the interactive `ghidra` CLI bridge for the duration
of the import. It restarts the bridge afterwards.

reccmp imports *every* matched function, not only exact matches. Effective and
partial matches receive their PDB name and signature too. A small number of
upstream limitations can surface as non-fatal failures. The import transaction
still commits the rest. Do not
manually push speculative names or types into the Ghidra project. The map is
the persistence layer for names. The reconstructable, local-only Ghidra
project is a scratchpad. Exploring a speculative name in your own local Ghidra
session is fine. To bank a durable name, write it to the map and, once
matched, to source annotations.

For reconstructed symbols, retain the retail address annotations:

```cpp
// FUNCTION: TOY2 0x00401230
// GLOBAL: TOY2 0x00501230
```

Each `FUNCTION` must have one verification tag:

- `[MATCHED]` means exact binary code and clean source.
- `[EFFECTIVE]` means a reccmp-effective result and clean source.
- `[TOOL]` means a verified symbol or relocation-label artifact and clean source.
- `[PROVISIONAL]` means that binary fidelity or source quality is not verified.

- Use `STUB` only for an intentionally unfinished body.
- Add `[MATCHED]` only after a fresh comparison confirms exact code and lint
  confirms clean source.
- Avoid duplicate annotations.
- Use caller and callee behavior, neighboring functions, data layout,
  reference code, and compiler output together when you reconstruct a function.
- Do not contort otherwise plausible source solely for a small similarity gain
  without supporting evidence.
- Change `STUB` to `FUNCTION` only when the body implements the complete retail
  behavior. This includes important error and cleanup paths.

## Validation

Run these before every handoff. The `continue-decomp` skill holds the full
per-session order, including the commit, sync, and report steps.

1. Format touched C/C++ files with the repository `.clang-format`.
2. Stage the intended source. Run `tools/decomp validate --target <address>
   --staged` for each changed function.
   It builds both targets, checks the saved full-report baseline, runs staged
   lint with warnings as errors, checks the map, and checks the diff.
   It also rejects an unreviewed target below 50 percent.
3. Run `tools/decomp compare`. Inspect relevant per-function differences.
4. Run `tools/decomp lint`. It must report no new error. An exact match with a
   raw offset cast or a placeholder parameter is not finished work.
5. Run `tools/decomp progress` when annotations change.
6. Run `git diff --check`. Review `git diff` and `git status --short`.
7. If the function map or any annotation changed, run `tools/decomp check`.
   This verifies that the map is sorted and deduplicated. It also verifies
   that every address is named and falls within the `.text` section.
8. In the handoff, identify the functions and addresses changed, the evidence
   used, the build result, the per-function comparison result, the lint result,
   the types you declared, any offsets left raw, any full-report regression,
   and the remaining uncertainties. State explicitly when runtime testing was
   not required or could not be performed.

Some steps are per session, not per commit. `tools/decomp report` and
`tools/decomp sync` are needed once, before the handoff. `tools/decomp check`
is needed only after a map or annotation edit. Running them after every commit
spends time and returns nothing.

Existing legacy warnings may remain. Avoid new warnings unless you need them
to reproduce original behavior.

## Repository hygiene

- Keep changes focused. Preserve unrelated user work.
- Never commit retail executables, game data, extracted installations, SDK
  output, PDBs, personal reverse-engineering databases, or generated reports.
- Treat `.tooling/`, `original/`, `build/`, and local reference repositories as
  local-only inputs or outputs.
- `toy2decomp` must retain retail behavior for comparison. Runtime
  conveniences belong behind `APPLY_FIXES` in `patcher.dll`. Do not enable
  that macro globally.
- Follow `CONTRIBUTING.md` for submission and runtime-testing requirements.
