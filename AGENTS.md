# Repository guidance

This file defines reconstruction quality and evidence rules. The
`continue-decomp` skill defines supervision. The `decomp-expert` skill defines
campaign procedure.

## Project goal

Reconstruct the Windows release of *Toy Story 2: Buzz Lightyear to the Rescue*
as readable C++. Prefer source that the original developers could maintain.
Use coherent types, descriptive names, natural control flow, and engine-level
concepts. Machine-code similarity is evidence. It is not the only objective.

A function is terminal only when all these conditions are true:

- reccmp classifies it as exact or effective.
- it has no unsuppressed source-debt finding.
- its C++ is plausible and evidence-backed.

Tool-only, provisional, `STUB`, and unstarted functions remain active work.

There is a document in this repository (`toy2_gameplay_context.md`) for more context.

## Evidence and names

Use evidence in this order:

1. Names quoted by the retail binary in asserts, logs, and source paths. Search
   them with `tools/decomp notes QUERY --source names`.
2. Names and types in the Wrath of Cortex DWARF dump under
   `/run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC`.
3. Role-based names confirmed by several uses.
4. `g_unkNNNNNN` only for a global with no supported role.

Toy Story 2 uses an older Nu3D version than Wrath of Cortex. Confirm borrowed
names and concepts against Toy Story 2 callers, disassembly, data, and
comparison output. Toy Story 2 uses an int16 fixed-point trig table and 12-bit
angles. Do not import later float-trig behavior.

`tools/Resources/functions_map.txt` is the committed function-start map.
Its addresses are authoritative. Its names are working hypotheses. The local
Ghidra project is disposable. Durable names go in the map and source.

## Select source work

Start with:

```sh
git status --short
tools/decomp progress --json
tools/decomp candidates --coverage --why
tools/decomp candidates --refine --why
tools/decomp data --limit 10
tools/decomp baseline
```

Use three campaign queues. A `COVERAGE` campaign reconstructs a `STUB` or an
unstarted function. A `REFINEMENT` campaign improves provisional source,
resolves a tool-only result, or removes source debt. A `DATA` campaign improves
typed evidence for initialized globals. Alternate all credible queues. Do not
force a queue when it has no credible target.

Rank work by unresolved retail bytes, evidence readiness, dependency impact,
and source debt. A provisional callee can support behavior reconstruction.
Treat it as a high-priority refinement prerequisite. Select one large function
or at most three related functions in one subsystem.
A data campaign can select at most three related initialized globals.

Use `tools/decomp evidence ADDRESS` before editing. Confirm most of these facts:

- subsystem and translation unit.
- calling convention, return type, and parameter roles.
- callers, callees, important constants, and side effects.
- global and structure accesses.
- a plausible source-level control-flow shape.

Use `tools/decomp data` for initialized globals. Use `tools/decomp data ADDRESS`
to inspect mismatched fields in one global. The report compares typed scalars,
arrays, pointers, and initialization state. A data difference can show an
incorrect type, layout, initializer, pointer target, or ownership boundary.
Confirm each conclusion with callers, retail data, or DWARF evidence.

Select data work with `tools/decomp data --limit 10`. Reject targets without
caller, retail, or DWARF evidence. Do not select BSS or unscored globals.

If the first target lacks evidence, pivot within the same subsystem. Make no
more than two pivots per campaign. A blocker is useful only when it identifies
evidence that can unlock an unfinished function. Record it with:

```sh
tools/decomp defer TARGET --blocked-by PREREQUISITE --kind layout \
  --reason "Needs the producer layout."
```

Omit `--blocked-by` only when no function can supply the evidence. Blockers are
advisory. Do not spend a campaign maintaining blocker prose. If two pivots
produce no supported source work, stop without a commit.

Use `tools/decomp discover` only when the normal queue has no credible target.
Confirm a result with `tools/decomp evidence --unmapped ADDRESS` before you add
it to the map.

## Reconstruct plausible source

Work from the outside in:

1. Establish the ABI from callers and the epilogue.
2. Identify globals and field layouts. Reuse supported repository types.
3. Recover branches, loops, state dispatch, and cleanup paths.
4. Apply supported names.
5. Check OpenCrashWOC for terminology and architecture.
6. Write the simplest natural C++ that explains the observed behavior.

Preserve update order, aliasing, integer widths, signedness, floating-point
evaluation, callbacks, ownership, and failure paths. Do not paste decompiler
temporaries or replace known fields with byte offsets. Do not add speculative
abstractions, casts that hide a wrong type, dummy locals, or expression churn
only to improve a score.

Headers are part of the reconstruction. Check all visible users after a type or
layout change. Add size and offset assertions when the project supports them.

## Reconstruct translation units

Treat file placement as source evidence. Do not assume that a current catch-all
file matches the original translation unit. `Toy2.cpp` is a watchlist item, not
a special case.

Before you add code to a large or mixed file, inspect these facts:

- nearby addresses and names in the function map.
- retail source paths, asserts, and log strings.
- OpenCrashWOC translation units and related terms.
- shared globals, private helpers, callers, and callees.
- the file's namespaces, includes, static data, and initialization order.

Keep a coherent subsystem in one `.cpp` file when evidence supports that
boundary. A separate state group, private helper set, or narrow dependency set
can support a split. File size or namespace count alone cannot support a split.

When evidence supports a split, move the complete coherent slice. Preserve
namespaces, linkage, address annotations, data order, and initialization order.
Add a header only for real cross-file use. Add each new source file to CMake.
Check all visible users and compare each moved function after the move.

A file move can accompany valid source progress. A structure-only campaign
must remove tracked source debt to count as refinement progress. Do not create
a cleanup-only success commit.

Keep a large initializer in a named `.inc` file beside its owning `.cpp` file.
A string is large at 160 decoded bytes. A non-string array is large at 32
elements and 256 initialized bytes. The `.inc` file contains initializer tokens
only and must be in the target's CMake source list.

## Use comparison output

Use this loop:

```text
evidence -> plausible source -> format -> build and compare -> explain -> revise
```

Run `tools/decomp bc ADDRESS` after each meaningful function-model change. It
saves the full diff and prints a bounded summary. For data work, rebuild and
run `tools/decomp data ADDRESS`. Search compiler guidance with `tools/decomp
notes QUERY --source codegen`. Do not read large note files.

The final similarity must be at least 50 percent unless reccmp marks the
function exact or effective. A low score often shows an incorrect source
model. Inspect structural differences. Keep a complete, evidence-backed,
readable model when the ABI, behavior, side effects, and data model are
supported. Do not keep an opaque or guessed body.

Use `tools/decomp experiment` for distinct source-form trials. Stop when trials
no longer test a concrete model question. Do not polish functions above about
90 percent when only symbols, register allocation, or scheduling differ.

## Validate and finish

Before a source commit, run:

```sh
tools/decomp validate --mode coverage --target ADDRESS --staged
tools/decomp validate --mode refinement --target ADDRESS --staged
tools/decomp validate --mode data --target ADDRESS --staged
tools/decomp check
git diff --check
```

Use the mode that matches the campaign. Validation rejects ABI, annotation,
source-quality, terminal-state, and untouched-function regressions. It also
rejects function targets below 50 percent. Normal campaigns must not use
`--allow-target-regression`. Use it only with `--meta-resolution` for an
explicit workflow repair.

Data validation requires a source change and improved typed bytes. It rejects
selected, unrelated, aggregate, and data-section regressions. Use
`--meta-resolution` with `--accounting-correction "REASON"` for an accounting
correction.

A successful coverage campaign must change C++, convert a target to
`FUNCTION`, and increase the implemented count. A successful refinement
campaign must improve similarity, reach terminal status, or remove source
debt while terminal status remains. A successful data campaign must improve
explained typed bytes without regressions. All three campaign types are source
progress. Use `tools/decomp progress --json` before and after each campaign.
Record target scores and global metrics. Do not create a metadata-only success
commit.

Run the full comparison, sync, and report once after a successful campaign.
Inspect the whole-file and data scores in the report. Explain a score decrease
when a corrected range or symbol replaces an estimated gap. Do not hide a real
data, resource, import, relocation, header, or debug-record regression.
Run tool unit tests only when tool code changed. Integrate current
`origin/agent/continuous`, then rebuild and validate the integrated tree.
Reject a push when an integrated function violates the 50 percent or terminal
gate. Commit the coherent source slice and push it.

Do not stage unrelated work. The existing `external/submodules/reccmp` pointer
drift is allowed only while it stays unchanged. Never add generated `build/`
files.

## Writing style

Use ASD-STE100 Simplified Technical English for documentation, notes, errors,
warnings, and C/C++ comments. Use the repository `ste-writing` skill. These
rules do not apply to code, identifiers, or command syntax.
