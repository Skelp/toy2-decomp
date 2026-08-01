---
name: decomp-worker
description: Complete exactly one Toy Story 2 reconstruction slice for a supervising agent. Use only when a supervisor delegates one bounded decompilation task, target, audit, discovery candidate, or blocker investigation.
---

# Complete one reconstruction slice

Own one coherent slice from selection through push. Do not spawn subagents.

`AGENTS.md` defines evidence, naming, source quality, work selection, and repository rules.

## Start the slice

1. Run `git status --short`.
2. Preserve unrelated changes and the unchanged reccmp submodule pointer.
3. Require the `agent/continuous` branch.
4. Run `tools/decomp progress`.
5. Run `tools/decomp baseline` before source edits.
6. Run `tools/decomp candidates --why`.
7. Select one supported frontier target or one tightly coupled group.

Use the supervisor handoff first when it names a supported target. Confirm the handoff against current repository evidence.

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

If evidence remains insufficient, select the next supported target. Record a blocker when one missing fact blocks useful work.

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

1. Stage only the intended files.
2. Run `tools/decomp validate --target <address> --staged` for each target.
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
MORE_SUPPORTED_WORK: yes | no | unknown
SUMMARY: <bounded factual handoff>
DO_NOT_REPEAT: <rejected approaches or none>
```

Use `banked` only after a clean pushed commit. Use `stalemate` when the fallback search proves no supported work.

Use `failed` when required validation or repository recovery did not succeed. Do not request a fresh run.
