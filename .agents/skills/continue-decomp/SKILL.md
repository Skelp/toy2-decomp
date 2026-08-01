---
name: continue-decomp
description: Start or continue one Toy Story 2 reconstruction slice. Select evidence-backed work, reconstruct it, validate it, and bank it.
---

# Continue decompilation

Use this procedure for one coherent reconstruction slice. `AGENTS.md` defines source quality, evidence precedence, work selection, and repository rules.

## Start the slice

1. Run `git status --short`.
2. Preserve unrelated changes. The unchanged `external/submodules/reccmp` pointer can remain dirty.
3. Confirm the current branch. For agent runs, use `agent/continuous`.
4. Run `tools/decomp progress`.
5. Run `tools/decomp baseline` before source edits.
6. Run `tools/decomp candidates --why`.
7. During the audit freeze, complete one required audit.
8. Otherwise, select one dependency-frontier target or one tightly coupled group.

Use bounded commands first:

```sh
tools/decomp candidates --new-work --why
tools/decomp candidates --for 0x00401230 --why
tools/decomp evidence 0x00401230
tools/decomp notes QUERY --source codegen
tools/decomp notes QUERY --source debt
tools/decomp notes QUERY --source names
```

Use `--limit 0`, `--all`, or `--full` only when the bounded result omits required evidence.

## Confirm the target

Before an edit, record a short working summary outside the repository. Include these facts:

- the target address, subsystem, and translation unit
- the calling convention, return type, and parameter roles
- callers, callees, globals, fields, and important constants
- the applicable OpenCrashWOC analogue
- confirmed facts, hypotheses, and unresolved questions

Read `AGENTS.md` before you name a symbol or change a type. Retail strings have the highest naming priority.

If evidence remains insufficient, select the next supported target. Record a blocker when one function or missing fact blocks useful work.

## Reconstruct and compare

1. Recover the ABI and observable behavior.
2. Reuse supported repository types.
3. Write the simplest plausible C++ control flow.
4. Preserve a `STUB` annotation until the full body is complete.
5. Format each changed C or C++ file.
6. Run `tools/decomp bc <address>` after each meaningful source-model change.
7. Use `tools/decomp bc --full <address>` only when the concise mismatch windows omit required data.
8. Change one source-level idea in each comparison attempt.
9. Use `tools/decomp experiment` for competing natural source forms.

For a body larger than 1000 bytes, compare one representative region first. Stop if the frame or control flow disproves the source model.

Do not chase register allocation or labels after the behavior and structure agree. Keep clear source when the remaining difference is compiler-incidental.

## Validate the slice

1. Stage only the intended files.
2. Run `tools/decomp validate --target <address> --staged` for each target.
3. Run `tools/decomp compare`.
4. Run `tools/decomp lint`.
5. Use `tools/decomp lint --show all` only to inspect the legacy backlog.
6. Run `tools/decomp progress` when annotations changed.
7. Run `tools/decomp check` when the function map or annotations changed.
8. Run `git diff --check`.
9. Review the staged diff and working-tree state.

Do not mark a function as matched without a fresh exact comparison and clean lint result.

## Bank the slice

1. Commit the validated slice with a focused message.
2. Run `tools/decomp sync` once after the final commit.
3. Run `tools/decomp report` once after the final commit.
4. Push `agent/continuous` to `origin` during an agent run.
5. Confirm that `HEAD` equals `origin/agent/continuous`.
6. Confirm that the working-tree state matches the slice startup state.
7. Report the addresses, evidence, scores, validation, types, raw offsets, regressions, and uncertainties.

## Fresh-run lifecycle

The foreground runner gives each thread exactly one root turn. Do not use Goal mode for unattended work.

Call `request_fresh_run` only after a coherent slice is committed, synchronized, reported, and pushed. Request a new run only when supported work remains.

Do not request a new run for a stalemate. End the turn with the supported blockers instead.

After the tool accepts a request, provide the final response immediately. Do not call another tool.

At a safe-boundary stop request, bank recoverable work or preserve the current state. Do not request a successor.

Use `context_pressure_after_bank` only after you bank a complete slice. Never use context pressure to hand off an uncommitted edit.
