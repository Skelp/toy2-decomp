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
7. Inspect `tools/decomp candidates --coverage --yield --why`.
8. Inspect `tools/decomp candidates --refine --yield --why`.
9. Inspect `tools/decomp data --limit 10`.
10. Record the whole-file, code, data, resource, import, and relocation scores.
11. Inspect the largest source files and mixed-namespace files.
12. Keep a file-structure watchlist with the evidence for each suspected split.
13. Run `tools/decomp campaigns summary`.
14. Do not select a row that has the `map_defect` flag.

## Run campaigns

Assign each campaign as `coverage`, `refinement`, or `data`. Alternate all
credible queues. Do not force a queue when it has no credible target.

Use campaign history when you select work. Do not retry a zero-yield target
unless a new commit or new evidence changes its source model. State that new
evidence in the assignment. Record it with `tools/decomp campaigns evidence`
before you select the target again.

Use the candidate `EST B` value as the retained-byte estimate. If the tool shows
a range, use its lower bound for stop decisions. Record this estimate and the
`EST MIN` value before delegation. Prefer the highest credible retained-byte
rate. Do not use unresolved bytes as the only measure.

Compare the first retained gain with the retained-byte estimate. If a range is
available, compare the gain with its lower bound. Close source exploration when
the gain is less than 25 percent of the applicable value.
Accept a valid source gain, and deliver it without more model trials or polish.
Penalize that estimate model and target class during the next selection.

Apply these selection gates:

1. A coverage target needs a scored pilot or a compiled analogue.
2. The pilot must cover the ABI and one main control-flow path.
3. Read the score ceiling and ceiling-relative score from `tools/decomp bc`.
4. Stop when the first complete pilot has a relative score below 35 percent.
5. Permit one more model when the relative score is from 35 through 49 percent.
6. Repair the map before source work when the score ceiling is below 60 percent.
7. A refinement target needs a specific mismatch from the saved comparison.
8. The mismatch must affect a repeated region or at least 100 retail bytes.
9. A data target needs retail bytes and one caller or DWARF fact.

Set `B` to the target's `expected_minutes` value. Convert these intervals to
absolute deadlines from the campaign start time:

1. Require the evidence preflight by `5 * B / 12` minutes.
2. Require the first score by `2 * B / 3` minutes.
3. Stop source trials after `B` minutes.
4. Permit `5 * B / 4` minutes only for a retained estimate of at least 100 bytes.

Do not start a three-target bundle without a tested anchor. The anchor must
retain at least 100 bytes, or all bundle targets must already pass validation.

A family campaign can exceed three targets only for corresponding slots in
parallel dispatch tables. Each corresponding retail body size must differ by no
more than one percent. Test one anchor before you expand the family. Require a
comparison for every member. Start it with one anchor and `--family`.

For data work, give the worker target byte scores and aggregate initialized
bytes. Include known caller, retail, and DWARF evidence.

Run `tools/decomp campaigns start` with a lowercase mode and all initial target
addresses. Pass the selected `EST MIN` value with `--expected-minutes` and the
`EST B` value with `--expected-retained-bytes`. Include the subsystem when it is
known. Add `--family` for a family campaign. Do not spawn the worker until this
command succeeds. The command saves the baseline and reports the campaign start
time.

The campaign start time includes baseline creation and delegation overhead.
Run `tools/decomp campaigns status`. Use the absolute preflight, first-score,
stop, and extension deadlines that it reports.

Spawn one fresh worker with `fork_turns: "none"` for each campaign. Tell it to
use `decomp-expert`. Give it the repository path, branch, current `HEAD`, mode,
global baseline, useful prior output, `expected_minutes`, and all absolute
deadlines. The worker must not start a new clock after delegation. Never reuse
a worker. Use a new task name such as `campaign_001`.

If `spawn_agent` fails at the thread limit, recover one slot immediately. Call
`followup_task` for one completed worker with a cleanup-only task. Tell it not
to change the repository or do source work. Immediately call `interrupt_agent`
while the turn runs. Confirm the free slot with `list_agents`. Retry the spawn
one time.

Give the worker relevant file-structure evidence for its subsystem. Ask it to
check the likely original translation unit before it adds code. Do not direct a
split from file size or namespace count alone.

Wait with the longest practical interval. Check status after a long wait or
when the user asks.

After the worker returns, inspect its result and `git status --short`. Make sure
that the worker made no commit. Run `tools/decomp progress --json` and verify
each reported metric.

For a `source` result:

1. Confirm that the coherent source work is staged.
2. Rebuild and run the mode-specific validation independently.
3. Confirm that the worker ran the full report and sync once.
4. Inspect file placement, linkage, headers, and CMake entries.
5. Verify the whole-file section scores and explain each regression.
6. Record the result with `tools/decomp campaigns record --result source`.
7. Stage the ledger with the campaign files.
8. Inspect the complete staged diff and commit it once.
9. Integrate current `origin/agent/continuous` safely.
10. Rebuild and validate the integrated source tree.
11. Push the result.
12. Count the valid result as source progress.

For a `no-source` result:

1. Confirm that the worker restored all campaign source trials.
2. Confirm that no campaign source work is staged.
3. Add one `--model` option for each rejected source model.
4. Record the result with `tools/decomp campaigns record --result no-source`.
5. Stage only the ledger and the source-model note.
6. Inspect the audit-only diff and commit it once.
7. Integrate current `origin/agent/continuous` safely.
8. Rebuild only if the integration changes source.
9. Push the audit-only commit.

For a `meta-fix` result:

1. Confirm that the bounded workflow fix is staged.
2. Run the focused tool tests and the applicable validation.
3. Record the result with `tools/decomp campaigns record --result meta-fix`.
4. Stage the ledger with the workflow fix.
5. Inspect the complete staged diff and commit it once.
6. Integrate current `origin/agent/continuous` safely.
7. Repeat the focused tests after an integration change.
8. Push the result.
9. Do not count the result as source progress.

After each result, check the measured time and retained bytes in the record.
Check the retained-byte rate for the last ten campaigns. Update the
file-structure watchlist after accepted source work. Then complete the mandatory
worker cleanup.

Use this cleanup protocol after every campaign:

1. Do not give new source work to the old worker.
2. Call `followup_task` with a cleanup-only task for the completed worker.
3. Tell the worker not to change the repository or do source work.
4. Immediately call `interrupt_agent` while the cleanup turn runs.
5. Use `list_agents` to confirm that the worker no longer uses a slot.

Run the full report and sync exactly once for each source result. Do not
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
- time after the first score.
- zero-yield campaign rate.
- reverted model count.

Treat two zero-yield campaigns in five records as a selection failure. Switch
the queue or require stronger evidence. Use
`tools/decomp candidates --refine --yield --why` for all refinement selection.
Do not retry an address with zero yield without new evidence. Treat a median
retained result below 100 bytes as a bundling or target-selection failure.

Resolve workflow failures. Assign a fresh expert a bounded meta-resolution
campaign when a tool problem prevents all source work. Resume source campaigns
after the fix.

## Stop

Stop immediately when the user asks. Interrupt the active worker. Do not start
a successor. Abort the active campaign record with a concise reason. Report the
last pushed commit and changed addresses. Report all three queue failure counts.
Report implemented, terminal, effective-byte, and source-debt deltas. Report
unresolved workflow failures.
