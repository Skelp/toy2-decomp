---
name: continue-decomp
description: Run or continue a Toy Story 2 reconstruction session on agent/continuous with the loop in AGENTS.md. Use when asked to continue decompilation, make progress, or pick the next function.
---

# Continue decompilation

Follow `AGENTS.md`. This skill only orders a session. It works in any harness:
run campaigns inline, or give one campaign to a fresh subagent that uses
`decomp-expert` when your harness provides one. Never run two writers on the
same tree at the same time.

## Start

1. `git status --short`; require branch `agent/continuous`; keep the unchanged
   `external/submodules/reccmp` pointer drift unstaged.
2. `git pull --rebase origin agent/continuous`.
3. `tools/decomp progress --json` and `tools/decomp campaigns summary` for the
   starting numbers; `tools/decomp throughput --window-days 7` for the rate.
4. `tools/decomp candidates --refine --yield --why --limit 15` and
   `tools/decomp candidates --coverage --why --limit 10`.

## Run campaigns

Repeat until the user stops you or the stop rule fires:

1. Pick the top credible row (AGENTS.md "Select work"); every third campaign
   take a coverage row.
2. `tools/decomp campaigns start --mode MODE --address ADDRESS --subsystem NAME`.
3. Reconstruct under the attempt budget (`decomp-expert`).
4. `validate`, `campaigns record`, commit, push. Commit every validated gain.
5. After each result read `tools/decomp campaigns summary`. Do not react to a
   low number with tool work; switch queue or family instead.

Stop rule: three consecutive `no-source` results across two different
families. Report the numbers and ask the user before continuing.

Meta work: only a `[T-nn]` tooling commit within the cap in AGENTS.md
"Tooling rule", and never as a reaction to a weak week.

## Session end

1. `tools/decomp report` and `tools/decomp sync` once.
2. Push. Report: last pushed commit, campaigns run, source results, effective
   and terminal byte deltas, new `FUNCTION` count, attempts per result, and
   any tooling commit made.
