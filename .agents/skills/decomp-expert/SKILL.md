---
name: decomp-expert
description: Complete one bounded Toy Story 2 coverage, refinement, or workflow-resolution campaign for a supervisor. Use only after a continue-decomp supervisor delegates a fresh campaign and assigns its mode.
---

# Decomp expert

Own one campaign. Produce working source, a workflow fix that restores source
work, or a concise no-source result after bounded pivots.

## Start

1. Read `AGENTS.md`.
2. Confirm the branch, supplied `HEAD`, and assigned mode.
3. Run `git status --short` and preserve unrelated drift.
4. Run `tools/decomp progress --json` and record all global metrics.
5. Run the candidate command for the assigned mode.
6. Select one subsystem campaign.
7. Record each target score and status.
8. Run `tools/decomp baseline` once before source edits.

A campaign contains one large function or at most three related functions.
Rank work by unresolved bytes, evidence readiness, dependency impact, and
source debt. Do not select work only by address or easy percentage gain.

## Reconstruct

Run `tools/decomp evidence ADDRESS`. Establish the ABI, data model, control
flow, ownership, and supported names. Search bounded notes with `tools/decomp
notes`. Use OpenCrashWOC only for relevant terms or analogues.

Write the simplest plausible C++. Build early. Run `tools/decomp bc ADDRESS`
after each meaningful source-model change. Let the compiler test hypotheses.

If evidence rejects the target, pivot to a related target in the subsystem.
Make at most two pivots. Record only blockers with a specific prerequisite. Do
not create a metadata-only commit.

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

Run tool unit tests only when the campaign changes tool code. A workflow fix
must include focused tests. It must restore a concrete path to source work.

If two pivots produce no supported source change, restore campaign-only edits.
Make no commit. Return `NO_SOURCE` with targets and missing evidence.

Return this compact summary:

```text
MODE: COVERAGE | REFINEMENT
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
```
