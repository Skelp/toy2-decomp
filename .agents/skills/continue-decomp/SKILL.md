---
name: continue-decomp
description: Supervise continuous Toy Story 2 reconstruction through serial worker subagents. Use when a root session starts or continues the decompilation goal, including `/goal continue`, or when asked to supervise continued reconstruction work.
---

# Supervise decompilation

Act only as the supervisor. Delegate reconstruction to serial worker subagents.

Do not select targets, inspect disassembly, edit source, build, commit, or push. Run Git commands only to enforce the handoff checks.

## Establish the run state

1. Run `git status --short`.
2. Require the `agent/continuous` branch.
3. Record the complete startup status.
4. If the reccmp submodule is dirty, record its current `HEAD`.
5. Require local `HEAD` to equal `origin/agent/continuous`.
6. Stop if any startup drift exists except the unchanged reccmp submodule pointer.

## Dispatch one worker

Spawn exactly one subagent for one coherent slice. Do not start another worker while this worker is active.

Use this worker prompt:

```text
Use $decomp-worker to complete exactly one coherent reconstruction slice.
Do not spawn subagents.
Preserve the supervisor's startup worktree state.
Validate, commit, synchronize, report, and push successful work.
Return the required worker result block.
```

Add a bounded handoff from the prior worker when one exists. Include useful addresses, completed work, and rejected approaches.

Wait for the worker to finish. Do not perform reconstruction work while it runs.

## Check the handoff

After each worker, run these checks:

1. Confirm that the branch is `agent/continuous`.
2. Confirm that the worktree matches the recorded startup status.
3. Confirm that the reccmp submodule `HEAD` did not change.
4. Confirm that local `HEAD` equals `origin/agent/continuous`.
5. Confirm that a banked result advanced `HEAD`.
6. Confirm that a stalemate result did not leave source edits.

If a check fails, send the same worker a corrective follow-up. Do not repair the worker's slice yourself.

Wait for that worker again. Stop the goal turn if the worker cannot restore the required state.

## Continue or stop

Spawn the next worker only when all these conditions are true:

- the prior result is `banked`
- the repository checks pass
- the worker reports more supported work
- the user did not request a stop
- the goal has enough remaining budget for another complete slice

Stop on `stalemate`. Do not ask another worker to repeat the same discovery or blocker search.

Stop on `failed` after one corrective follow-up. Report the repository state and the failure.

Before a planned goal-turn boundary, wait for the active worker. Do not leave a worker unobserved.

Return a concise supervisor summary. Include each commit, changed address, score, validation result, and final blocker state.
