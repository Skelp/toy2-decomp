---
name: continue-decomp
description: Supervise continuous Toy Story 2 source reconstruction through fresh serial expert agents. Use when a root session starts or continues the decompilation goal, including /goal continue.
---

# Continue decompilation

Act as the supervisor. Do not reconstruct functions yourself. Do not start an
external supervisor or use Goal mode inside a worker.

## Start

1. Read `AGENTS.md`.
2. Run `git status --short` and `git branch --show-current`.
3. Run `tools/decomp progress --json`.
4. Require branch `agent/continuous`.
5. Preserve unrelated changes and the unchanged reccmp pointer drift.
6. Record `HEAD`, implemented count, terminal count, and byte metrics.
7. Inspect `tools/decomp candidates --coverage --why`.
8. Inspect `tools/decomp candidates --refine --why`.
9. Inspect `tools/decomp data --limit 10`.
10. Record the whole-file, code, data, resource, import, and relocation scores.
11. Inspect the largest source files and mixed-namespace files.
12. Keep a file-structure watchlist with the evidence for each suspected split.
13. Run `tools/decomp campaigns summary`.
14. Use `tools/decomp candidates --yield --why` for coverage and refinement.

## Run campaigns

Assign each campaign as `COVERAGE`, `REFINEMENT`, or `DATA`. Alternate all
credible queues. Do not force a queue when it has no credible target.

Spawn one fresh worker with `fork_turns: "none"` for each campaign. Tell it to
use `decomp-expert`. Give it the repository path, branch, current `HEAD`, mode,
global baseline, and useful prior output. Never reuse a worker. Use a new task
name such as `campaign_001`.

Give the worker relevant file-structure evidence for its subsystem. Ask it to
check the likely original translation unit before it adds code. Do not direct a
split from file size or namespace count alone.

Use campaign history when you select work. Do not retry a zero-yield target
unless a new commit or new evidence changes its source model. State that new
evidence in the assignment.

Estimate retained bytes and elapsed minutes before you start a campaign.
Prefer the highest credible retained-byte rate. Do not use unresolved bytes as
the only measure.

Apply these selection gates:

1. A coverage target needs a scored pilot or a compiled analogue.
2. The pilot must cover the ABI and one main control-flow path.
3. Stop a coverage target when its first complete pilot is below 35 percent.
4. Permit one more model when the pilot is from 35 through 49 percent.
5. A refinement target needs a specific mismatch from the saved comparison.
6. The mismatch must affect a repeated region or at least 100 retail bytes.
7. A data target needs retail bytes and one caller or DWARF fact.

Use these time gates unless a scored result justifies more time:

1. Require the evidence preflight in five minutes.
2. Require the first score in eight minutes.
3. Stop source trials after twelve minutes.
4. Permit fifteen minutes only when the retained estimate is at least 100 bytes.

Do not start a three-target bundle without a tested anchor. The anchor must
retain at least 100 bytes, or all bundle targets must already pass validation.

For data work, give the worker target byte scores and aggregate initialized
bytes. Include known caller, retail, and DWARF evidence.

Wait with the longest practical interval. Check status after a long wait or
when the user asks.

After the worker returns:

1. Inspect its result and `git status --short`.
2. Confirm its commits are on `agent/continuous` and pushed.
3. Rebuild and run the mode-specific validation independently.
4. Run `tools/decomp progress --json` and verify each reported metric.
5. Count a valid coverage, refinement, or data result as source progress.
6. Close or interrupt the worker before the next campaign.
7. Confirm that the worker ran `tools/decomp report` after source progress.
8. Confirm that the worker ran `tools/decomp sync` after source progress.
9. Inspect changed file placement, linkage, headers, and CMake entries.
10. Update the file-structure watchlist after each accepted campaign.
11. Verify the whole-file section scores and explain each regression.
12. Record the result with `tools/decomp campaigns record`.
13. Record elapsed minutes, code bytes, data bytes, addresses, and the commit.
14. Check the retained-byte rate for the last ten campaigns.

Run the full report and sync exactly once for each successful campaign. Do not
repeat a successful worker report or sync. Check the report summary, file time,
section scores, and sync result. Repeat a command only when its evidence is
missing, stale, or failed.

Watch for growth in catch-all files such as `Toy2.cpp`. Use function-map
clusters, retail paths, DWARF units, private state, and call relationships as
boundary evidence. Ask a later worker to move a coherent slice when evidence
supports the split.

Do not count a structure-only commit as source progress unless it removes
tracked source debt. Prefer a supported file move as part of a valid coverage
or refinement campaign. Require comparisons for all moved functions.

Keep separate no-source counts for coverage, refinement, and data. Switch modes
after a failure. Reset all counts after source progress. Stop only when all
three counts reach three. Do not count metadata, notes, or blocker-only commits
as source progress.

Audit efficiency after every campaign. Review the last ten records after every
third campaign. Also review them after two zero-yield results. Change the
selection gate or worker contract when the same waste repeats.

Use these audit signals:

- retained code bytes per source-work minute.
- retained data bytes per source-work minute.
- time to the first score.
- zero-yield campaign rate.
- reverted model count.
- integration time after the source model is complete.

Treat two zero-yield campaigns in five records as a selection failure. Switch
the queue or require stronger evidence. Treat a median retained result below
100 bytes as a bundling or target-selection failure.

Resolve workflow failures. Assign a fresh expert a bounded meta-resolution
campaign when a tool problem prevents all source work. Resume source campaigns
after the fix.

## Stop

Stop immediately when the user asks. Interrupt the active worker. Do not start
a successor. Report the last pushed commit and changed addresses. Report both
queue failure counts. Report implemented, terminal, effective-byte, and
source-debt deltas. Report unresolved workflow failures.
