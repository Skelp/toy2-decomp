---
name: decomp-worker
description: Complete one Toy Story 2 reconstruction or meta-resolution slice for a supervising agent. Use only when a supervisor delegates a bounded target, audit, discovery, blocker, evidence, metadata, or tooling task.
---

# Complete one delegated slice

Own one coherent reconstruction or meta-resolution slice through push. Do not spawn subagents.

The supervisor does not pass its conversation history. Treat the bounded
handoff and the repository as the complete input. Do not reconstruct missing
history from old Codex transcripts.

`AGENTS.md` defines evidence, naming, source quality, work selection, and repository rules.

## Select the slice mode

Use reconstruction mode when the handoff names a supported target or requests normal selection.

Use meta-resolution mode when the handoff names a blocking class, proof target, discovery question, or tool blind spot.

Do not replace a specific meta-resolution task with the general fallback sequence.

## Start the slice

1. Run `git status --short`.
2. Preserve unrelated changes and the unchanged reccmp submodule pointer.
3. Require the `agent/continuous` branch.
4. Run `tools/decomp progress`.
5. Run `tools/decomp baseline` before source edits.
6. In reconstruction mode, run `tools/decomp candidates --why`.
7. In meta-resolution mode, run only the commands that test the assigned proof target.
8. Select one supported frontier target, one tightly coupled group, or one meta-resolution result.

Use the supervisor handoff first when it names a supported target. Confirm the handoff against current repository evidence.

Before you edit a selected target, compare its candidate readiness with its
committed blocker record. Stop reconstruction if the candidate tool reports
readiness while an unresolved blocker applies. Diagnose the contradiction as a
meta-resolution slice. Correct the causal model, metadata, or tool. Do not
bypass the blocker.

Use bounded commands before full output:

```sh
tools/decomp candidates --new-work --why
tools/decomp candidates --for 0x00401230 --why
tools/decomp evidence 0x00401230
tools/decomp notes QUERY --source codegen
tools/decomp notes QUERY --source debt
tools/decomp notes QUERY --source names
```

Use `--limit 0`, `--all`, or `--full` only when omitted evidence is necessary.

## Confirm the target

Before an edit, record these facts in working memory:

- the target address, subsystem, and translation unit
- the calling convention, return type, and parameter roles
- callers, callees, globals, fields, and important constants
- the applicable OpenCrashWOC analogue
- confirmed facts, hypotheses, and unresolved questions

Read the applicable `AGENTS.md` sections before you name a symbol or change a type.

If reconstruction evidence remains insufficient, select the next supported target. Record a blocker when one missing fact blocks useful work.

## Resolve a meta-issue

Test the assigned cause, not the general symptom. Useful meta-resolution results include:

- a confirmed evidence-provider function
- a supported discovery name, owner, boundary, or contract
- a corrected blocker dependency or semantic state
- a type, name, lint, or ownership fix that unlocks source work
- a candidate or evidence-tool fix that exposes supported work
- proof that one required fact is external and unavailable

Do not return a command transcript as a result. Explain what the evidence changed.

Bank useful metadata and tooling changes with the same validation and push discipline as source changes.

A fingerprint, timestamp, or reworded reason is not useful by itself. A
material metadata result must correct the causal model or repository behavior.
Examples include a dependency, blocker kind, semantic state, supported
conclusion, map fact, or tool behavior. If current evidence only confirms the
existing record, leave the repository unchanged. Return a stalemate with the
next distinct proof target.

Group related stale records when they share one evidence provider, blocker
class, or tool defect. Review two to four in the same slice. Commit one coherent
correction. Do not make one commit for each refreshed fingerprint.

## Reconstruct and compare

1. Recover the ABI and observable behavior.
2. Reuse supported repository types.
3. Write the simplest plausible C++ control flow.
4. Preserve a `STUB` annotation until the full body is complete.
5. Format each changed C or C++ file.
6. Run `tools/decomp bc <address>` after each meaningful source-model change.
7. Change one source-level idea in each comparison attempt.
8. Use `tools/decomp experiment` for competing natural source forms.

For a body larger than 1000 bytes, compare one representative region first. Stop if this checkpoint disproves the source model.

Keep clear source when the remaining difference is compiler-incidental.

## Validate and bank

For a meta-resolution slice without a function target, run the focused tool tests and applicable repository checks. Do not invent a target address.

1. Stage only the intended files.
2. Run `tools/decomp validate --target <address> --staged` for each function target.
3. Run `tools/decomp compare`.
4. Run `tools/decomp lint`.
5. Run `tools/decomp progress` when annotations changed.
6. Run `tools/decomp check` when the function map or annotations changed.
7. Run `git diff --check`.
8. Review the staged diff and worktree state.
9. Commit the validated slice with a focused message.
10. Run `tools/decomp sync` once after the final commit.
11. Run `tools/decomp report` once after the final commit.
12. Push `agent/continuous` to `origin`.
13. Confirm that local `HEAD` equals `origin/agent/continuous`.
14. Confirm that the worktree matches the slice startup state.

Do not commit an opaque implementation. A supported blocker or stalemate is a valid result.

## Report to the supervisor

End with one result block:

```text
WORKER_RESULT: banked | stalemate | failed
COMMIT: <full SHA or none>
ADDRESSES: <comma-separated addresses or none>
SCORES: <address and verdict or none>
MATERIAL_CHANGE: source | map | blocker-model | tooling | none
MORE_SUPPORTED_WORK: yes | no | unknown
BLOCKING_CLASS: <specific class or none>
NEXT_META_ACTION: <one untried action or none>
SUMMARY: <bounded factual handoff>
DO_NOT_REPEAT: <rejected approaches or none>
```

Use `banked` only after a clean pushed commit with a material change. Use
`stalemate` when the assigned proof target cannot produce useful work, including
when it only produces a fingerprint refresh.

For `stalemate`, name the blocking class and one untried meta-action. Use `none` only after you test all applicable routes.

Use `failed` when required validation or repository recovery did not succeed. Do not request a fresh run.
