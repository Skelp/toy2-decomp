---
name: decomp-expert
description: Complete one bounded Toy Story 2 coverage, refinement, data, or workflow-resolution campaign for a supervisor. Use only after a continue-decomp supervisor delegates a fresh campaign and assigns its mode.
---

# Decomp expert

Own one campaign. Produce working source, a workflow fix that restores source
work, or a concise no-source result after bounded pivots.

## Start

1. Read `AGENTS.md`.
2. Confirm the branch, supplied `HEAD`, and assigned mode.
3. Run `git status --short` and preserve unrelated drift.
4. Run `tools/decomp progress --json` and record all global metrics.
5. Run the candidate command for the assigned mode. Use `tools/decomp data
   --limit 10` for data work.
6. Select one subsystem campaign.
7. Record each target score and status.
8. Run `tools/decomp baseline` once before source edits.
9. Inspect the target file's namespaces, includes, globals, and private helpers.
10. Check nearby function-map entries and source-path evidence for file boundaries.
11. Run `tools/decomp data --limit 10` and inspect relevant global differences.
12. Run `tools/decomp campaigns summary` and inspect the assigned addresses.

A function campaign contains one large function or at most three related
functions. A data campaign contains at most three related initialized globals.
Rank work by unresolved bytes, evidence readiness, dependency impact, and
source debt. Also use the estimated retained-byte rate. Do not select work only
by address or easy percentage gain.

Do not retry a zero-yield address unless the supervisor gives new evidence.
Confirm that evidence before you edit source.

## Reconstruct

Run `tools/decomp evidence ADDRESS`. Establish the ABI, data model, control
flow, ownership, and supported names. Search bounded notes with `tools/decomp
notes`. Use OpenCrashWOC only for relevant terms or analogues.

Run `tools/decomp data GLOBAL_ADDRESS` when the target reads or writes a scored
global. Use mismatched scalar fields as layout and initializer evidence. Do not
change an unrelated global only to increase the whole-file score.

For a data campaign, inspect each target with `tools/decomp data ADDRESS`.
Confirm each change with callers, retail bytes, or DWARF evidence. Preserve
storage class, pointer targets, layout, and initialization order.

Choose the likely original translation unit before you edit source. Do not add
new code to a catch-all file only because the file already exists. Use retail
paths, address clusters, DWARF units, private state, helpers, and dependencies
as boundary evidence.

If evidence supports a split, move the complete coherent slice. Preserve
namespaces, linkage, annotations, data order, and initialization order. Add a
header only for cross-file use. Add each new `.cpp` file to CMake. Compare all
moved functions and check all visible users.

Do not split a file from size or namespace count alone. If evidence is weak,
keep the current placement and report the missing boundary evidence. A file
move must accompany valid campaign progress unless it removes tracked debt.

Write the simplest plausible C++. Build early. For function work, run
`tools/decomp bc ADDRESS` after each meaningful model change. For data work,
rebuild and run `tools/decomp data ADDRESS` after each meaningful change.

Send the supervisor a preflight result within five minutes. Include the ABI,
source model, affected byte range, file boundary, and expected retained bytes.

Get the first score within eight minutes. Stop source trials after twelve
minutes unless a score shows at least 100 likely retained bytes.

For coverage, build a complete scored pilot before you refine the body. The
pilot must include the ABI and one main control-flow path. Stop when the pilot
is below 35 percent. Permit one more source model when the pilot is from 35
through 49 percent.

For refinement, name the saved-diff mismatch before the first edit. The first
model must test that mismatch. Do not call register allocation or instruction
scheduling a source model.

Do not add targets to a bundle until the anchor retains 100 bytes. You can use
a bundle without an anchor when all targets already pass validation.

If evidence rejects the target, pivot to a related target in the subsystem.
Make at most two pivots. Record only blockers with a specific prerequisite. Do
not create a metadata-only commit.

Stop after two failed source models. Restore each failed model before the next
test. Do not run the full report or sync for a no-source result.

The final score must be at least 50 percent unless reccmp marks the target
exact or effective. Keep a readable model when evidence supports its ABI,
behavior, side effects, and data model. Do not use
`--allow-target-regression` in a source campaign.

## Validate and deliver

For a source campaign:

1. Convert each completed `STUB` to `FUNCTION`.
2. Update the function map when a name changes.
3. Stage only the coherent campaign.
4. Run `tools/decomp validate --mode MODE --target ADDRESS --staged`.
5. Confirm the result meets the assigned mode contract.
6. Run the full comparison, sync, and report once.
7. Commit the source slice.
8. Integrate current `origin/agent/continuous` safely.
9. Rebuild and validate the integrated tree.
10. Push the result.

Coverage must increase the implemented count. Refinement must increase a
score, reach terminal status, or remove debt while terminal status remains.
Data must improve typed bytes for each target without data regressions.
Use `--accounting-correction "REASON"` only with `--meta-resolution`. This path
does not count as source progress.

Run tool unit tests only when the campaign changes tool code. A workflow fix
must include focused tests. It must restore a concrete path to source work.

If two pivots produce no supported source change, restore campaign-only edits.
Make no commit. Return `NO_SOURCE` with targets and missing evidence.

Return this compact summary:

```text
MODE: COVERAGE | REFINEMENT | DATA
RESULT: SOURCE | META_FIX | NO_SOURCE
BASE: <commit>
COMMITS: <commit list or none>
ADDRESSES: <address list or none>
IMPLEMENTED_BEFORE: <count>
IMPLEMENTED_AFTER: <count>
TERMINAL_BEFORE: <count>
TERMINAL_AFTER: <count>
MATCH_BEFORE: <address=percent/status>
MATCH_AFTER: <address=percent/status>
EFFECTIVE_BYTES_DELTA: <bytes>
SOURCE_DEBT_DELTA: <count>
PIVOTS: <integer>
NEXT: <best next subsystem or blocking fact>
FILE_STRUCTURE: <kept, moved, or deferred with brief evidence>
DATA_EVIDENCE: <affected global scores or none>
DATA_BYTES_BEFORE: <address=explained/scored or none>
DATA_BYTES_AFTER: <address=explained/scored or none>
INITIALIZED_DATA_DELTA: <explained bytes>
WHOLE_FILE_SECTIONS: <whole-file and section scores or none>
ELAPSED_MINUTES: <total worker minutes>
PREFLIGHT_MINUTES: <minutes to the evidence decision>
FIRST_SCORE_MINUTES: <minutes to the first compiled score>
MODEL_TRIALS: <tested models, retained models, reverted models>
RETAINED_BYTES_PER_MINUTE: <code plus data bytes divided by source-work minutes>
```
