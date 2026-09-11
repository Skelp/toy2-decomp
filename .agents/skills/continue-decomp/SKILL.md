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
   take a coverage row. When `tools/decomp structure` marks a file split, one
   `--mode structure` campaign moves one block out of it, at most one in ten.
2. `tools/decomp campaigns start --mode MODE --address ADDRESS --subsystem NAME`,
   then `tools/decomp bc ADDRESS` once: attempt 1 is the baseline.
3. Reconstruct under the attempt budget (`decomp-expert`), inline or in
   fresh-context batches of four attempts. A batch opens with
   `tools/decomp bc ADDRESS --pack` and ends with the handoff as the writer's
   final message; save it with `tools/decomp handoff ADDRESS`. Run the next
   batch while the last one gained half a point and budget remains.
4. `tools/decomp campaigns finish --mode MODE --target ADDRESS --note TEXT
   --message FILE` validates, records and commits; then push. Commit every
   validated gain.
5. After each result read `tools/decomp campaigns summary`. Do not react to a
   low number with tool work; switch queue or family instead.

Without an orchestrating model: `tools/decomp campaigns run --count N` runs
steps 1-4 and the stop rule below as a script with headless `decomp-expert`
writers (Claude Code or Codex, `--harness auto`), prints one line per attempt,
batch and campaign, and logs to `build/decomp-runs/` (`--push` pushes each
commit). Run `--check --live` first, start the run in the background and read
`--status` instead of the logs; the last line names the exit code and the next
command. A writer that does no work stops the run without a `no-source` row.
Prefer it for unattended work: a script spends no tokens between batches, and
a model watching a campaign spends the most.

Stop rule: three consecutive `no-source` results across two different
families. Report the numbers and ask the user before continuing.

Meta work: only a `[T-nn]` tooling commit within the cap in AGENTS.md
"Tooling rule", and never as a reaction to a weak week.

## Session end

1. `tools/decomp report` and `tools/decomp sync` once.
2. Push. Report: last pushed commit, campaigns run, source results, effective
   and terminal byte deltas, new `FUNCTION` count, attempts per result, and
   any tooling commit made.
