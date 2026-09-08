---
name: continue-decomp
description: Supervise continuous Toy Story 2 source reconstruction through read-only evidence scouts and one serial writer. Use when a root session starts or continues the decompilation goal, including /goal continue.
---

# Continue decompilation

Act as the supervisor. Do not reconstruct functions yourself. Use one writer at
a time. Run read-only scouts before writer work.

## Start

1. Read `AGENTS.md`.
2. Run `git status --short` and `git branch --show-current`.
3. Require branch `agent/continuous`.
4. Preserve unrelated changes and the unchanged reccmp pointer drift.
5. Fetch and integrate current `origin/agent/continuous` safely.
6. Record `HEAD` and run `tools/decomp progress --json`.
7. Run `tools/decomp campaigns summary --window 10 --json`.
8. Record a UTC selection start time.
9. Inspect `tools/decomp candidates --lane closure --why`.
10. Inspect `tools/decomp candidates --lane production --why`.
11. Inspect `tools/decomp candidates --lane research --why`.
12. Do not select a row that has the `map_defect` flag.
13. For non-meta work, select the mode, lane, and target before the doctor.
14. Export a closure or production forecast with
    `--for ADDRESS --prediction-features-out build/decomp-cache/prediction-handoff.json`.
15. For a production target, run `tools/decomp bc ADDRESS` before the doctor.

Use terminal retail bytes as the primary metric. Use effective bytes and
dependency impact as secondary metrics.

## Run campaigns

Use five closure campaigns and five production campaigns in each rolling ten
writer campaigns. A production slot can become data work only when one coherent
target predicts at least 16 retained bytes. Keep resource work dormant while
all resource leaves are exact.

Use closure for terminal conversions. Use production for conservative
retained-byte yield. Use evidence-only research to remove a named blocker
before the timer starts. A timed research campaign authorizes one source pilot.
Use coverage for a `STUB` or unstarted target with a specific blocker. Use
refinement for an implemented `FUNCTION` with a specific blocker or a valid
selector cooldown or circuit route. The brief and campaign start recompute this
route. Do not replace an empty lane with blocked coverage work.

Use the forecast median for `--expected-retained-bytes`. Use the lower bound for
stop decisions. Record the probability, sample count, lower estimate, median
estimate, and median minutes. Do not use unresolved bytes as a forecast.

Treat a gain below 25 percent of forecast as under-yield. Apply the no-source
cooldown when a gain is below 10 percent. Prefer fresh targets.

Permit one retry only after a linked evidence event. The event must name the
failed campaign and rejected model. It must also state a changed assumption or
evidence source. Keep the historical failure count.

Open a subsystem circuit breaker after three no-source or under-10-percent
results in its last five campaigns. Route that subsystem to research.

Apply these selection gates:

1. A closure target must be tool-only or non-terminal above 99 percent.
2. Closure succeeds only at exact or effective status with no source debt.
3. A production target must meet all production gates in `AGENTS.md`.
4. A coverage pilot must cover the ABI and one main control-flow path.
5. Keep renderer and collision coverage paused without a 50-percent pilot.
6. Repair the map before source work when the score ceiling is below 60 percent.
7. A refinement target needs a specific saved mismatch with current `HEAD`,
   source, build, and report provenance.
8. A data target needs retail bytes and one caller or DWARF fact.
9. A resource target needs one retail type, ID, and language tuple.

Production candidate selection reads the sealed canonical report. After target
selection, `bc` writes the target-local, hash-bound verbose mismatch sidecar.
The doctor and brief reject it when its `HEAD`, source, build, or report
provenance is stale. Do not use the new mismatch to change the completed
candidate selection.

For a non-meta campaign, set `B` to the target's `expected_minutes` value.
Convert these intervals to absolute deadlines from the campaign start time:

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

For one bounded workflow repair that blocks reliable source progress, start
`--mode meta --lane meta`. Skip candidate selection, the doctor, scouts, brief,
and source forecast. The wrapper records the repository-only meta baseline.
Give the worker a bounded repair scope and a specific stop condition. A meta
campaign has no forecast or source-work deadlines after campaign start. The
fault does not have to block every source queue.

For all other modes, run the doctor with the selected mode, lane, ordered
target list, and recorded selection start time. For example:

```sh
tools/decomp doctor --mode refinement --lane closure --target ADDRESS \
  --selection-started-at UTC_TIMESTAMP --json
```

Use `refinement` for closure and production. For research, use `coverage` for a
`STUB` or unstarted target with a specific blocker. Use `refinement` for an
implemented `FUNCTION` with a specific blocker or a valid selector cooldown or
circuit route. Use `data` for data and `resource` for resource. Choose the mode
from the target annotation and state. The brief and campaign start reject an
invalid or stale research route before costly work.

Use `--resource TYPE,ID,LANGUAGE` instead of `--target` for resource work. Stop
before campaign start when a required check fails. The doctor receipt expires
60 minutes after the doctor ends. Start the campaign before expiry, or run the
exact preflight again.

After the doctor passes, run two independent read-only scout audits in parallel.
Give each scout the target, lane, `HEAD`, and exact doctor receipt. Scout A
audits the retail ABI, control flow, and direct evidence. Scout B audits
callers, types, layouts, translation-unit evidence, and analogues. Either scout
can specialize in renderer or collision evidence when it applies. Scouts must
not edit, stage, build, start campaigns, write notes, update Ghidra, or change
tool state.

Each scout returns one JSON document to you. Save the documents as two different
files. Each document must contain only these schema-2 keys. Use this form for
the retail report:

```json
{
  "schema": 2,
  "scout_id": "retail-scout",
  "audit": "retail-abi-control-flow-evidence",
  "lane": "LANE",
  "target": "TARGET",
  "access": "read-only",
  "owns_mutations": false,
  "doctor_receipt": {
    "receipt_id": "DOCTOR_RECEIPT_ID",
    "sha256": "DOCTOR_RECEIPT_SHA256"
  },
  "findings": [
    {
      "category": "abi",
      "claim": "SUPPORTED_ABI_CLAIM",
      "evidence": [{"source": "SOURCE", "locator": "LOCATOR"}]
    },
    {
      "category": "control-flow",
      "claim": "SUPPORTED_CONTROL_FLOW_CLAIM",
      "evidence": [{"source": "SOURCE", "locator": "LOCATOR"}]
    },
    {
      "category": "retail-evidence",
      "claim": "SUPPORTED_RETAIL_EVIDENCE_CLAIM",
      "evidence": [{"source": "SOURCE", "locator": "LOCATOR"}]
    }
  ]
}
```

The context report uses a distinct scout ID and the
`callers-types-layout-translation-unit-analogue` audit. Its findings cover
exactly `callers`, `types-layout`, and `translation-unit-analogue`. Each finding
has only `category`, `claim`, and `evidence`. Each evidence list has one through
eight citations. Each citation has only `source` and `locator`. Claims, sources,
and locators must be nonempty. Match the lane and target. Use the receipt ID and
file SHA-256 of the canonical immutable doctor receipt.
Replace each uppercase placeholder with the actual value. A placeholder is not
evidence.

Keep the doctor artifacts, both report files, and the brief unchanged.
Finalization reads the cached Ghidra artifacts and report files again. It
rejects missing or changed evidence. It also rejects finalization or a pivot
when `HEAD` changed after the doctor. If `HEAD` changes, run the doctor, both
scout audits, and brief creation again. Do not reuse an old active receipt.

Run this command after both scouts return:

```sh
tools/decomp brief --lane closure --target ADDRESS \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --scout-report build/decomp-cache/retail-scout.json \
  --scout-report build/decomp-cache/context-scout.json --json
```

Reject a stale or incomplete brief. Keep only supported scout facts. Do not
run the brief before the exact doctor check passes.

Use a lowercase mode, the lane, and all initial targets. Use one `--resource
TYPE,ID,LANGUAGE` option for resource work. Pass the median minutes and median
retained-byte estimate. Also pass the prediction version, lower bound, features
JSON, exact doctor receipt, and one brief per ordered target. Include the
subsystem when known. Add `--family` only for a supported family.

The closure and production features JSON must include these fields:

- `success_probability`
- `cohort_sample_size`
- `median_retained_bytes`
- `median_minutes`

The two expected values must equal the two median values. A non-meta estimate
in another lane still needs a version, lower bound, and nonempty features JSON.
The start wrapper does not run the doctor or create a brief. It rejects a
non-meta start without the explicit receipt and all ordered briefs.

Export a closure or production forecast with this command:

```sh
tools/decomp candidates --lane closure --for ADDRESS --limit 1 \
  --prediction-features-out build/decomp-cache/prediction-handoff.json --why
```

The file has `schema_version` 2 and contains `generated_at`, `lane`, `mode`,
`address`,
`prediction_version`, `expected_retained_bytes`, `expected_minutes`,
`prediction_lower_bound_bytes`, the cohort features, and the selector-input
fingerprint in `selection_fingerprint` and `selection_fingerprint_sha256`.
Start validates it against current selector inputs and rejects it after 60
minutes. Pass it unchanged to `--prediction-handoff`. Do not recreate it with
`jq` or mix it with explicit forecast options. A closure or production bundle
needs one distinct selector export for each address. Repeat
`--prediction-handoff` in address order. Use explicit forecast options for
resource, data, or research work without a selector export. Add `--json` only
when another tool needs candidate output on stdout.

For multi-target source work, `features.per_target` has one entry for each
exact address and no other entry. Start creates this object from the repeated
selector handoffs for closure and production. For other lanes, supply it in
the features JSON. Each entry gives `median_retained_bytes` and
`lower_retained_bytes`. The target medians sum to the campaign median. If one
entry gives `median_minutes`, all entries give it and the values sum to the
campaign median minutes.

Always use `--for ADDRESS` after target selection. This prevents the export
from selecting another visible candidate. For work without an export, derive
the median and lower bound from completed comparable campaigns. Save a
nonempty features object that identifies the cohort and sample count. Pass it
with `--prediction-features`. Also pass `--expected-minutes`,
`--expected-retained-bytes`, `--prediction-version`, and
`--prediction-lower-bound-bytes`. Both expected values are medians.

For a manual research forecast, name the subsystem:

```sh
tools/decomp campaigns start --mode coverage --lane research \
  --address ADDRESS --subsystem SUBSYSTEM \
  --expected-minutes MEDIAN_MINUTES \
  --expected-retained-bytes MEDIAN_BYTES \
  --prediction-version cohort-v1 \
  --prediction-lower-bound-bytes LOWER_BYTES \
  --prediction-features build/decomp-cache/research-prediction.json \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --brief BRIEF_PATH
```

Use `--mode refinement` when the research target is an implemented `FUNCTION`.

For example, start a closure campaign with this command:

```sh
tools/decomp campaigns start --mode refinement --lane closure \
  --address ADDRESS \
  --prediction-handoff build/decomp-cache/prediction-handoff.json \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --brief BRIEF_PATH
```

The campaign start command starts the source-work timer. The all-in cycle
includes selection, doctor, and scout time. Run `tools/decomp campaigns status`.
Use the reported absolute deadlines.

Stamp the `preflight` phase by its deadline and get the first score by the
first-score deadline. Start finalization by the stop deadline. Use the extension
only when campaign status provides one for a forecast of at least 100 retained
bytes. A finalization step can finish after the cutoff when it started on time.
The finalizer rejects a late source result.

Spawn one fresh worker with `fork_turns: "none"` for each campaign. Tell it to
use `decomp-expert`. Give it the repository path, branch, `HEAD`, mode, lane,
and baseline. For non-meta work, also give it the forecast, brief, and all
absolute deadlines. The worker must not start a new clock. Never run a second
writer or writer worktree at the same time.

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

For a pivot, tell the worker to restore all trials for the current target.
Record a new selection time. Run the doctor for only the proposed address, then
run two independent read-only scout audits. Create a doctor-bound brief for the
new address. Add the target with this command before new source work:

```sh
tools/decomp candidates --lane production --for 0x004038E0 --limit 1 \
  --prediction-features-out build/decomp-cache/prediction-handoff.json --why
tools/decomp bc 0x004038E0
tools/decomp doctor --mode refinement --lane production --target 0x004038E0 \
  --selection-started-at PIVOT_SELECTION_UTC --json
# Run two independent read-only scout audits with this doctor receipt.
tools/decomp brief --lane production --target 0x004038E0 \
  --doctor-receipt RECEIPT \
  --scout-report build/decomp-cache/pivot-retail-scout.json \
  --scout-report build/decomp-cache/pivot-context-scout.json --json
tools/decomp campaigns add-target --address 0x004038E0 \
  --replace OLD_ADDRESS \
  --prediction-handoff build/decomp-cache/prediction-handoff.json \
  --doctor-receipt RECEIPT --brief BRIEF
```

Give the new brief and receipt to the worker. The state keeps the replaced
address for audit, but only active addresses drive final validation. Permit no
more than two pivots. Prediction events keep each target forecast. The original
absolute campaign deadlines do not reset.

For a family expansion, omit `--replace`. Run the exact doctor, scout, and brief
sequence for each new member before `add-target`. Pass its fresh forecast too.

After the writer returns, inspect its result and `git status --short`. Make sure
that it made no commit. Run `tools/decomp progress --json` and verify each
reported metric.

For a `source` result:

1. Confirm that the coherent source work is staged.
2. Confirm that no source input is a symbolic link or an untracked CMake source.
3. Require a valid content-addressed finalization receipt.
4. Review the staged diff and receipt without another build.
5. Inspect file placement, linkage, headers, and CMake entries.
6. Revalidate only when a relevant receipt input hash changed.
7. Record the result with `tools/decomp campaigns record --result source`.
8. Count the valid result as source progress.

For a `no-source` result:

1. Confirm that the worker restored all campaign source trials.
2. Confirm that no campaign source work is staged.
3. Require a hash-valid receipt that reuses the unchanged baseline.
4. Add one `--model` option for each rejected source model.
5. Record the result with `tools/decomp campaigns record --result no-source`.
6. Keep only the ledger and source-model note for the campaign commit.

For a `meta-fix` result:

1. Confirm that the bounded workflow fix is staged.
2. Require the `meta-fix` finalization receipt from the worker.
3. Review the staged diff and receipt without another validation run.
4. Record the result with `tools/decomp campaigns record --result meta-fix`.
5. Do not count the result as source progress.

Record delivery after the campaign record:

1. Record `delivery --campaign-id ID --status staged`.
2. Give the staged files and finalization receipt to an independent reviewer.
3. Record `delivery --campaign-id ID --status accepted` after acceptance.
4. Stage the finalized files, campaign row, and these two delivery entries.
5. Inspect the full staged diff and commit it once.
6. Fetch and rebase this commit onto current `origin/agent/continuous`.
7. Set `COMMIT` to the resulting `HEAD` and `BASE` to its first parent.
8. Run `delivery-verify` and keep the returned immutable receipt path.
9. Record `integrated`, then `committed`, with that receipt.
10. Push `COMMIT` to `origin/agent/continuous`.
11. Record `pushed` with the same receipt.
12. Stage the three post-commit events, commit them as telemetry, and push them.

Use these commands after the rebase:

```sh
COMMIT=$(git rev-parse HEAD)
BASE=$(git rev-parse HEAD^)
DELIVERY_RECEIPT=$(
  tools/decomp campaigns delivery-verify --campaign-id ID \
    --commit "$COMMIT" --base-commit "$BASE" |
    python -c 'import json, sys; print(json.load(sys.stdin)["path"])'
)
tools/decomp campaigns delivery --campaign-id ID --status integrated \
  --base-commit "$BASE" --commit "$COMMIT" \
  --delivery-receipt "$DELIVERY_RECEIPT"
tools/decomp campaigns delivery --campaign-id ID --status committed \
  --commit "$COMMIT" --delivery-receipt "$DELIVERY_RECEIPT"
# Push "$COMMIT" to origin/agent/continuous.
tools/decomp campaigns delivery --campaign-id ID --status pushed \
  --commit "$COMMIT" --delivery-receipt "$DELIVERY_RECEIPT"
```

The delivery receipt rejects a conflict on a finalized campaign path. It binds
the campaign row, finalized tree, current `HEAD`, first parent, validation
inputs, and validation artifacts. Source work
is rebuilt and validated. A meta fix runs its focused tests and applicable
checks. A no-source result checks the integrated tree and index and the exact
ledger and source-model-note append. Do not build, create a report, or run
Ghidra sync for a no-source result.

Keep the `integrated`, `committed`, and `pushed` entries outside the campaign
commit. Put them in one small telemetry-only follow-up commit. This follow-up
does not count as source progress. If a push race changes the campaign commit,
restore its uncommitted telemetry, rebase again, and create a new delivery
receipt for the new `COMMIT` and `BASE`. Do not reuse a receipt after `HEAD`,
the base, an input, or an artifact changes. Record
`rejected` when integration, validation, or push cannot complete.

After each result, check phase time, terminal bytes, effective bytes, source
debt, and forecast error. Update the file-structure watchlist after accepted
source work. Then complete the mandatory writer cleanup.

Use this cleanup protocol after every campaign:

1. Do not give new source work to the old worker.
2. Call `followup_task` with a cleanup-only task for the completed worker.
3. Tell the worker not to change the repository or do source work.
4. Immediately call `interrupt_agent` while the cleanup turn runs.
5. Use `list_agents` to confirm that the worker no longer uses a slot.

Generate the HTML report and run Ghidra sync after five accepted commits. Also
run them after a structural change or at session end. Repeat a command only
when its evidence is missing, stale, or failed.

Watch for growth in catch-all files such as `Toy2.cpp`. Use function-map
clusters, retail paths, DWARF units, private state, and call relationships as
boundary evidence. Ask a later worker to move a coherent slice when evidence
supports the split.

Do not count a structure-only commit as source progress unless it removes
tracked source debt. Prefer a supported file move as part of a valid coverage
or refinement campaign. Require comparisons for all moved functions.

Review the rolling summary after every result. During the first 20 campaigns,
use these canary limits:

- No tool aborts.
- No retry without linked evidence.
- At most 20 percent zero-yield results.
- At least 8 effective bytes per production minute.
- At least 65 percent of wall time in normal campaigns.
- Less than 10 percent of wall time in meta work and aborts.
- Less than 15 percent unattributed gap time.
- Actual yield from 50 through 150 percent of forecast.
- Five closure candidates terminal within 60 closure minutes.
- Three terminal conversions per 12 refinements.
- No evidence regression.

If ranking fails these limits, preserve telemetry and health checks. Revert only
the ranking policy. Use the recorded outcomes to recalibrate it.

Resolve workflow failures. Assign a fresh expert a bounded meta-resolution
campaign when a concrete tool problem blocks or slows source work. Resume
source campaigns after the fix.

## Stop

Stop immediately when the user asks. Interrupt the active worker. Do not start
a successor. Abort the active campaign record with a concise reason. Report the
last pushed commit and changed targets. Report lane counts, terminal-byte delta,
effective-byte delta, source-debt delta, and unresolved workflow failures.
