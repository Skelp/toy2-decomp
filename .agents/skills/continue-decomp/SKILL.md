---
name: continue-decomp
description: Supervise continuous Toy Story 2 source reconstruction through fresh serial expert agents. Use when a root session starts or continues the decompilation goal, including /goal continue.
---

# Continue decompilation

Act as the supervisor. Do not reconstruct functions yourself. Do not invoke a
Codex executable, start an external supervisor, use MCP rotation, or use Goal
mode inside a worker.

## Start

1. Read `AGENTS.md`.
2. Run `git status --short`, `git branch --show-current`, and
   `tools/decomp progress --json`.
3. Require branch `agent/continuous`. Preserve unrelated changes and the
   unchanged reccmp submodule pointer.
4. Record the initial implemented count and `HEAD`.

## Run campaigns

Spawn one fresh worker with `fork_turns: "none"` for each campaign. Tell it to
use `decomp-expert`. Give it the repository path, branch, current `HEAD`, the
baseline implemented count, and useful output from the prior campaign. Never
use `followup_task` to reuse a worker. Use a new task name such as
`campaign_001`, `campaign_002`, and so on.

Wait for completion with the longest practical wait interval. Frequent polling
wastes supervisor context and does not help the worker. Check status only after
a long wait or when the user asks.

After the worker returns:

1. Inspect its result and `git status --short`.
2. Confirm its commits are on `agent/continuous` and pushed.
3. Run `tools/decomp progress --json`.
4. Count success only when the campaign changed C++ and increased the
   implemented count.
5. If `close_agent` is available, close the worker. Otherwise, list agents and
   interrupt a worker that is still active. Require `Done` before you start its
   successor. A completed entry can remain visible.

Start the next fresh campaign after a success. Stop after three consecutive
campaigns that produce no source progress. Do not count metadata, notes,
blockers, or tool-only commits as reconstruction progress.

Resolve workflow failures. If a build, tool, or evidence problem prevents all
source work, assign a fresh expert a bounded meta-resolution campaign. Resume
source campaigns after the fix. Do not merely observe the failure.

## Stop

Stop immediately when the user asks. Interrupt the active worker and do not
start a successor. Report the last pushed commit, implemented-count delta,
addresses reconstructed, consecutive no-source count, and unresolved workflow
failures.
