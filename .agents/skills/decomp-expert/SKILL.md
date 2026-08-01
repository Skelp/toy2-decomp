---
name: decomp-expert
description: Complete one bounded Toy Story 2 source-reconstruction or workflow-resolution campaign for a supervisor. Use only after a continue-decomp supervisor delegates a fresh campaign.
---

# Decomp expert

Own one campaign. Think and act like a decompilation expert. The result must be
working source, a workflow fix that directly restores source work, or a concise
no-source result after bounded pivots.

## Start

1. Read `AGENTS.md`.
2. Confirm the branch and supplied `HEAD`.
3. Run `git status --short` and preserve unrelated drift.
4. Run `tools/decomp progress --json` and record the implemented count.
5. Run `tools/decomp candidates --why` and select one subsystem campaign.
6. Run `tools/decomp baseline` once before source edits.

A campaign is one large function or at most three related functions. Prefer a
target that unlocks callers or replaces a meaningful stub. Do not select work
by address order or easy percentage gain.

## Reconstruct

Run `tools/decomp evidence ADDRESS`. Establish the ABI, data model, control
flow, ownership, and supported names. Search bounded notes with `tools/decomp
notes`; search OpenCrashWOC only for relevant terminology or analogues.

Write the simplest plausible C++. Build early. Run `tools/decomp bc ADDRESS`
after each meaningful source-model change. Let the compiler test hypotheses.
Do not spend the campaign writing an account of why no form can work.

If evidence rejects the target, pivot to a related target in the same
subsystem. Make at most two pivots. Do not commit a blocker unless it names a
specific prerequisite that another campaign can reconstruct. Do not create a
metadata-only commit.

A score below 75 percent is advisory. Keep a complete readable source model
when its ABI, behavior, side effects, and data model are supported. Use
`--allow-target-regression` only when a clearer supported model explains a
target regression.

## Validate and deliver

For a source campaign:

1. Convert each completed `STUB` to `FUNCTION` and update the function map when
   its name changed.
2. Stage only the coherent campaign.
3. Run `tools/decomp validate --target ADDRESS --staged` for each target.
4. Confirm `tools/decomp progress --json` shows a larger implemented count.
5. Run the full comparison, sync, and report once.
6. Commit, integrate `origin/agent/continuous` safely, and push.

Run tool unit tests only when the campaign changes tool code. A workflow fix
must include focused tests and must restore a concrete path to source work.

If two pivots produce no supported source change, restore campaign-only edits,
make no commit, and return `NO_SOURCE` with the targets and missing evidence.

Return a compact machine-readable summary:

```text
RESULT: SOURCE | META_FIX | NO_SOURCE
BASE: <commit>
COMMITS: <commit list or none>
ADDRESSES: <address list or none>
PROGRESS_BEFORE: <implemented count>
PROGRESS_AFTER: <implemented count>
DELTA: <integer>
SCORES: <address=percent list or none>
PIVOTS: <integer>
NEXT: <best next subsystem or blocking fact>
```
