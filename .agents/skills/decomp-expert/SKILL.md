---
name: decomp-expert
description: Complete one Toy Story 2 reconstruction campaign (coverage, refinement or data) under the attempt budget in AGENTS.md. Use for a single assigned target or family anchor.
---

# Decomp expert

Own one campaign in the canonical tree. Follow `AGENTS.md`; this skill is the
order of operations.

## Procedure

1. Confirm the branch, the assigned mode and addresses, and that
   `tools/decomp campaigns status` shows the campaign started. The contract is
   already in your context; do not re-read `AGENTS.md` or this file.
2. `tools/decomp evidence ADDRESS` (once; `--full` only for a named gap). Read
   the strings section first, then callers, callees, globals and the
   decompilation. Search `tools/decomp notes QUERY --source models`.
3. Establish the ABI, data model, control flow, ownership and supported names
   before writing the body. Choose the translation unit from evidence.
4. `tools/decomp bc ADDRESS` for the baseline. It prints the score and a region
   index (one line per mismatch region with its source lines). Read
   `build/decomp-diffs/ADDRESS.compact.txt` once, in one call; never page
   through the raw diff. After each later `bc`, read only the index and
   `tools/decomp bc ADDRESS --hunk N` for the regions you are working on.
5. Write the simplest plausible C++; format only the changed lines
   (`clang-format -i --lines=A:B FILE`). One source-level idea per `bc`. Batch
   independent reads into one command. Keep the best model; the best-scoring
   patch is saved under `build/decomp-cache/best/`.
6. Stop at the budget line, at five attempts without a half-point gain, or when
   no trial tests a concrete model question. For data work rebuild and run
   `tools/decomp data ADDRESS` instead of `bc`.
7. Pivot at most twice inside the subsystem with
   `tools/decomp campaigns add-target --address NEW`.

## Finish

Source result: convert each completed `STUB` to `FUNCTION`; update the map
when a name changes; `git add src tools/Resources/functions_map.txt`;
`tools/decomp validate --mode MODE --target ADDRESS --staged`;
`tools/decomp campaigns record --result source`; commit source, ledger and
`tools/Resources/scoreboard.tsv` in one commit; push.

No-source result: `git checkout -- src`; `tools/decomp campaigns record
--result no-source --model "..."` once per rejected model; commit the ledger and
`.notes/source-models.md` entry; push.

Return this summary:

```text
MODE: coverage | refinement | data
RESULT: source | no-source
COMMIT: <sha or none>
ADDRESSES: <list>
MATCH_BEFORE: <address=percent/status>
MATCH_AFTER: <address=percent/status>
ATTEMPTS: <count>
BEST_ATTEMPT: <index>
MINUTES: <all-in minutes>
NEXT: <best next target or blocking fact>
```
