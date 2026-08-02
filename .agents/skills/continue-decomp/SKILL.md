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
9. Inspect the largest source files and mixed-namespace files.
10. Keep a file-structure watchlist with the evidence for each suspected split.

## Run campaigns

Assign each campaign as `COVERAGE` or `REFINEMENT`. Alternate modes when both
queues have credible work. Continue the available mode when one queue has no
credible work.

Spawn one fresh worker with `fork_turns: "none"` for each campaign. Tell it to
use `decomp-expert`. Give it the repository path, branch, current `HEAD`, mode,
global baseline, and useful prior output. Never reuse a worker. Use a new task
name such as `campaign_001`.

Give the worker relevant file-structure evidence for its subsystem. Ask it to
check the likely original translation unit before it adds code. Do not direct a
split from file size or namespace count alone.

Wait with the longest practical interval. Check status after a long wait or
when the user asks.

After the worker returns:

1. Inspect its result and `git status --short`.
2. Confirm its commits are on `agent/continuous` and pushed.
3. Rebuild and run the mode-specific validation independently.
4. Run `tools/decomp progress --json` and verify each reported metric.
5. Count a valid coverage or refinement result as source progress.
6. Close or interrupt the worker before the next campaign.
7. Run `tools/decomp report` after source progress.
8. Run `tools/decomp sync` after source progress.
9. Inspect changed file placement, linkage, headers, and CMake entries.
10. Update the file-structure watchlist after each accepted campaign.

Watch for growth in catch-all files such as `Toy2.cpp`. Use function-map
clusters, retail paths, DWARF units, private state, and call relationships as
boundary evidence. Ask a later worker to move a coherent slice when evidence
supports the split.

Do not count a structure-only commit as source progress unless it removes
tracked source debt. Prefer a supported file move as part of a valid coverage
or refinement campaign. Require comparisons for all moved functions.

Keep separate no-source counts for coverage and refinement. Switch modes after
a failure. Reset both counts after source progress. Stop only when both counts
reach three without intervening progress. Do not count metadata, notes, or
blocker-only commits as source progress.

Resolve workflow failures. Assign a fresh expert a bounded meta-resolution
campaign when a tool problem prevents all source work. Resume source campaigns
after the fix.

## Stop

Stop immediately when the user asks. Interrupt the active worker. Do not start
a successor. Report the last pushed commit and changed addresses. Report both
queue failure counts. Report implemented, terminal, effective-byte, and
source-debt deltas. Report unresolved workflow failures.
