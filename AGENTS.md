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
tools/decomp baseline
```

Use two campaign queues. A `COVERAGE` campaign reconstructs a `STUB` or an
unstarted function. A `REFINEMENT` campaign improves provisional source,
resolves a tool-only result, or removes source debt. Alternate the queues when
both have credible work. Continue the available queue when the other queue has
no credible work.

Rank work by unresolved retail bytes, evidence readiness, dependency impact,
and source debt. A provisional callee can support behavior reconstruction.
Treat it as a high-priority refinement prerequisite. Select one large function
or at most three related functions in one subsystem.

Use `tools/decomp evidence ADDRESS` before editing. Confirm most of these facts:

- subsystem and translation unit.
- calling convention, return type, and parameter roles.
- callers, callees, important constants, and side effects.
- global and structure accesses.
- a plausible source-level control-flow shape.

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

Keep a large initializer in a named `.inc` file beside its owning `.cpp` file.
A string is large at 160 decoded bytes. A non-string array is large at 32
elements and 256 initialized bytes. The `.inc` file contains initializer tokens
only and must be in the target's CMake source list.

## Use comparison output

Use this loop:

```text
evidence -> plausible source -> format -> build and compare -> explain -> revise
```

Run `tools/decomp bc ADDRESS` after each meaningful source-model change. It
saves the full diff and prints a bounded summary. Search compiler guidance with
`tools/decomp notes QUERY --source codegen`. Do not read large note files.

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
tools/decomp check
git diff --check
```

Use the mode that matches the campaign. Validation rejects ABI, annotation,
source-quality, terminal-state, and untouched-function regressions. It also
rejects targets below 50 percent. Normal campaigns must not use
`--allow-target-regression`. Use it only with `--meta-resolution` for an
explicit workflow repair.

A successful coverage campaign must change C++, convert a target to
`FUNCTION`, and increase the implemented count. A successful refinement
campaign must improve similarity, reach terminal status, or remove source
debt while terminal status remains. Both campaign types are source progress.
Use `tools/decomp progress --json` before and after each campaign. Record the
target score and the global metrics. Do not create a metadata-only success
commit.

Run the full comparison, sync, and report once after a successful campaign.
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
