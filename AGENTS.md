# Repository guidance

This file defines reconstruction quality and evidence rules. The
`continue-decomp` skill defines supervision. The `decomp-expert` skill defines
campaign procedure.

## Project goal

Reconstruct the Windows release of *Toy Story 2: Buzz Lightyear to the Rescue*
as readable C++. Prefer source that the original developers could maintain.
Use coherent types, descriptive names, natural control flow, and engine-level
concepts. Machine-code similarity is evidence. It is not the only objective.

A function is terminal only when all these conditions are true:

- reccmp classifies it as exact or effective.
- it has no unsuppressed source-debt finding.
- its C++ is plausible and evidence-backed.

Tool-only, provisional, `STUB`, and unstarted functions remain active work.

There is a document in this repository (`toy2_gameplay_context.md`) for more context.

## Evidence and names

Use evidence in this order:

1. Names quoted by the retail binary in asserts, logs, and source paths. Search
   them with `tools/decomp notes QUERY --source names`.
2. Names and types in the Wrath of Cortex DWARF dump under
   `/run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC`.
3. Role-based names confirmed by several uses.
4. `g_unkNNNNNN` only for a global with no supported role.

Toy Story 2 uses an older Nu3D version than Wrath of Cortex. Confirm borrowed
names and concepts against Toy Story 2 callers, disassembly, data, and
comparison output. Toy Story 2 uses an int16 fixed-point trig table and 12-bit
angles. Do not import later float-trig behavior.

`tools/Resources/functions_map.txt` is the committed function-start map.
Its addresses are authoritative. Its names are working hypotheses. The local
Ghidra project is disposable. Durable names go in the map and source.

## Select source work

Fetch and integrate current `origin/agent/continuous` before candidate
selection and the doctor. The doctor binds the resulting `HEAD`.

Start with:

```sh
git status --short
tools/decomp progress --json
tools/decomp campaigns summary --window 10 --json
tools/decomp candidates --lane closure --why
tools/decomp candidates --lane production --why
tools/decomp candidates --lane research --why
tools/decomp candidates --lane closure --for ADDRESS --limit 1 \
  --prediction-features-out build/decomp-cache/prediction-handoff.json --why
tools/decomp doctor --mode refinement --lane closure --target ADDRESS \
  --selection-started-at UTC_TIMESTAMP --json
```

Record `UTC_TIMESTAMP` before candidate selection. Select the lane and target,
then run `doctor` with that exact mode, lane, and ordered target list. Use
`--resource TYPE,ID,LANGUAGE` instead of `--target` for resource work. Do not
start the campaign when a required check fails. The selection and doctor time
is part of all-in cycle time. It is not part of the source-work timer. The
doctor receipt expires 60 minutes after the doctor ends. Start the campaign
before expiry, or run the exact preflight again.

For non-meta work, use only these lane-mode pairs:

- `closure` and `production`: `refinement`
- `research`: `coverage` for a `STUB` or unstarted target with a specific
  blocker; `refinement` for an implemented `FUNCTION` with a specific blocker
  or a valid selector cooldown or circuit route
- `data`: `data`
- `resource`: `resource`

Choose the research mode from the target annotation and state. The research
brief binds the blocker, cooldown, or circuit evidence. The brief and campaign
start recompute this route and reject arbitrary research work.

For closure or production, use the exported version, median bytes, median
minutes, and lower bound. Export one file for each source target. The file has
`schema_version` 2 and contains `generated_at`, `lane`,
`mode`, `address`,
`prediction_version`, `expected_retained_bytes`, `expected_minutes`,
`prediction_lower_bound_bytes`, the cohort features, and the selector-input
fingerprint in `selection_fingerprint` and `selection_fingerprint_sha256`.
The command validates these fields against current selector inputs. The
handoff expires after 60 minutes. Pass it unchanged. Do not recreate it with
`jq`. Add `--json` only when another tool needs candidate output on stdout.

Always export with `--for ADDRESS` after target selection. This prevents the
handoff from selecting a different row. For an initial bundle, use a distinct
file for each address. Pass one `--prediction-handoff` for each ordered
`--address`. Use the proposed new address for a pivot.

Production candidate selection reads the sealed canonical report. After you
select a production target, run `tools/decomp bc ADDRESS` before the doctor.
This command creates the target-local, hash-bound verbose mismatch that the
doctor and brief use. Do not use this new mismatch to change the completed
candidate selection.

A meta campaign skips candidate selection, the doctor, the scouts, and the
brief. Start it with `--mode meta --lane meta`. Use it only for one bounded
workflow repair that blocks reliable source progress. It does not have a
forecast or source-work deadlines after campaign start. The fault does not
have to block every source queue.

Use three selection lanes:

- `closure` converts a non-terminal function to a terminal function.
- `production` delivers conservative effective-byte yield.
- `research` removes a specific evidence blocker for downstream work.

Use terminal retail bytes as the primary project metric. Use effective bytes
and dependency impact as secondary metrics. Do not use unresolved bytes as a
forecast.

Run five closure campaigns and five production campaigns in each rolling ten
writer campaigns. A production slot can become a data campaign only when one
coherent target predicts at least 16 retained bytes. Keep resource work dormant
while all retail resource leaves are exact. Do not substitute blocked coverage
work for an empty lane.

Closure candidates include tool-only functions and non-terminal functions above
99 percent. A closure result succeeds only when each target is exact or
effective and has no unsuppressed source debt.

A production candidate must meet all these gates:

- Its match is from 50 through 90 percent.
- Its retail body is from 300 through 3000 bytes.
- It has at least 100 unresolved bytes.
- It has at most two weak dependencies.
- It has no active blocker.
- Its saved comparison has an actionable mismatch.

The candidate estimate must show the cohort sample count, success probability,
lower retained-byte estimate, median estimate, and median minutes. Use the
median estimate as `--expected-retained-bytes`. Use the lower estimate for stop
decisions. Back off to a broader cohort when a cohort has fewer than eight
completed results.

Penalize a positive result that retains less than 25 percent of its forecast.
Apply the same cooldown as a no-source result when it retains less than 10
percent. Prefer a fresh target over a cooled target.

One retry can follow new evidence. The evidence record must identify the failed
campaign and rejected model. It must also identify at least one changed
assumption or new evidence source. A generic note does not clear a cooldown.
Never remove the earlier result from history.

Open a subsystem circuit breaker after three no-source or under-10-percent
results in its last five campaigns. Use evidence-only research to get the
missing evidence before more writer work in that subsystem.

Keep renderer and collision coverage paused. Resume a target only when its
brief is complete and a compiled pilot reaches at least 50 percent.

The immutable brief must match the current `HEAD`, target, report, function
map, and tool hashes. Reject a stale brief. It must include the ABI, control
flow, callers, callees, globals, layouts, DWARF evidence, mismatch classes,
history, cooldown, file evidence, and a readiness decision.

Each new function brief contains a schema-1 `evidence.context_pack`. The pack
uses the cached doctor artifacts. It does not start Ghidra. It binds the
decoder, retail image, repository inputs, and accepted-source receipts. An
unknown reason means that the related part is not complete. Do not infer a
jump table or a memory access from an incomplete part.

Inspect a pack with this read-only command:

```sh
tools/decomp context --brief BRIEF_PATH [--json]
```

The command rejects a changed brief, a changed context pack, and a stale
repository input. Historical briefs can still be read by the brief tools, but
they do not have a context pack.

Use one supervisor, two read-only scouts, and one writer. Run two independent
scout audits in parallel before writer work. Scout A audits the retail ABI,
control flow, and direct evidence. Scout B audits callers, types, layouts,
translation-unit evidence, and analogues. Either scout can specialize in
renderer or collision evidence when it applies. Scouts must not edit, stage,
build, or change tool state.

Use one canonical writer tree. Do not run concurrent writer worktrees. Merge
the scout findings into the brief before the writer starts.

Give both scouts the exact doctor receipt. Each scout returns one JSON document
to the supervisor. The supervisor saves the documents as two different files.
Each file must have only these schema-2 keys. This is the retail report form:

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
and locators must be nonempty. The lane and target must match the brief. The
receipt ID and SHA-256 must match the canonical immutable doctor receipt.
Replace each uppercase placeholder with the actual value. A placeholder is not
evidence.

Keep the doctor artifacts, both scout report files, and the brief immutable.
Finalization reads the cached Ghidra artifacts and both report files again. It
rejects changed or missing evidence. It also rejects finalization or a pivot
when `HEAD` changed after the doctor. If `HEAD` changes, run the doctor, both
scout audits, and brief creation again. Do not reuse an old active receipt.

After both scouts return, create the doctor-bound brief:

```sh
tools/decomp brief --lane closure --target ADDRESS \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --scout-report build/decomp-cache/retail-scout.json \
  --scout-report build/decomp-cache/context-scout.json --json
```

Reject a stale or incomplete brief. Keep only facts that the scouts support.
Do not run either scout or the brief before the exact doctor check passes.

Evidence-only research occurs before the source-work timer starts. A timed
research campaign authorizes one source pilot in its brief. Use coverage mode
for a `STUB` or unstarted target. Use refinement mode for an implemented
`FUNCTION` target routed to research by a cooldown or circuit breaker. Do not
start the pilot until the brief marks it ready.

For a research target without a selector export, use the manual forecast
handoff and name its subsystem:

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

Use `--mode refinement` instead when the research target is an implemented
`FUNCTION`.

Start the selected campaign with a lowercase mode and its lane:

```sh
tools/decomp campaigns start --mode refinement --lane closure \
  --address ADDRESS \
  --prediction-handoff build/decomp-cache/prediction-handoff.json \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --brief BRIEF_PATH
```

The command reports the start time and saves the baseline. Add one `--address`
option for each initial target. Add one `--brief` in the same order. The
prediction features must include `success_probability`, `cohort_sample_size`,
`median_retained_bytes`, and `median_minutes` for closure and production. The
expected values must equal the two median values. Do not mix
`--prediction-handoff` with explicit forecast options. A closure or production
bundle needs one exact selector handoff for each ordered target. Use the
explicit version, median, lower-bound, and features options for resource,
data, or research work without a selector export. The source-work timer includes
baseline creation and delegation overhead. Use the absolute deadlines from
campaign status. Do not start a new clock after delegation.

Stamp the `preflight` phase by its deadline and get the first score by the
first-score deadline. Start finalization by the stop deadline. Campaign status
shows an extension only when the forecast is at least 100 retained bytes. A
finalization step can finish after its cutoff when it started on time. The
finalizer rejects a source result that missed one of these gates.

For a multi-target source campaign, `features.per_target` must have one entry
for each exact address and no other entry. Start creates this object from the
repeated selector handoffs for closure and production. For other lanes, supply
it in the features JSON. Each entry gives
`median_retained_bytes` and `lower_retained_bytes`. The target medians must sum
to the campaign median. If one entry gives `median_minutes`, all entries must
give it and the values must sum to the campaign median minutes.

The start wrapper does not run the doctor or create a brief. It rejects a
non-meta start without an explicit receipt and all ordered briefs.

Run `tools/decomp campaigns status` for active state. The summary shows completed
history only. If a campaign cannot continue, run `tools/decomp campaigns abort
--reason "REASON"`.

A `resource` campaign reconstructs one retail PE resource leaf. Use one numeric
type, ID, and language tuple. Do not add an address target.

```sh
tools/decomp doctor --mode resource --lane resource --resource 2,127,2057 \
  --selection-started-at UTC_TIMESTAMP --json
# Run two independent read-only scout audits with the exact doctor receipt.
tools/decomp brief --lane resource --target 2,127,2057 \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --scout-report build/decomp-cache/retail-scout.json \
  --scout-report build/decomp-cache/context-scout.json --json
tools/decomp campaigns start --mode resource --resource 2,127,2057 \
  --lane resource --expected-minutes MEDIAN_MINUTES \
  --expected-retained-bytes MEDIAN_BYTES \
  --prediction-version resource-v1 \
  --prediction-lower-bound-bytes LOWER_BYTES \
  --prediction-features build/decomp-cache/resource-prediction.json \
  --doctor-receipt build/decomp-cache/doctor/latest.json \
  --brief RESOURCE_BRIEF
tools/decomp finalize --result source --mode resource \
  --resource 2,127,2057 --staged
```

The selected leaf must improve. Stage each changed resource source file that
the build used. The finalizer rejects unrelated evidence regressions. A
non-meta estimate also needs a version, lower bound, and nonempty features JSON.
For resource, data, or research work without a selector export, derive the
median and lower bound from completed comparable campaigns. Save a nonempty
JSON object that identifies that cohort and its sample count. Pass the object
with `--prediction-features`. Also pass `--expected-minutes`,
`--expected-retained-bytes`, `--prediction-version`, and
`--prediction-lower-bound-bytes`. The two expected values are the forecast
medians.
Select one large function or at most three related functions in one subsystem.
A data campaign can select at most three related initialized globals.

A family campaign can contain more than three functions only when retail data
shows parallel dispatch tables. The functions must use corresponding slots.
The retail body sizes for each corresponding slot must differ by no more than
one percent. Test one anchor before you expand the family. Use one shared source
model, and compare every family member. Start this campaign with one anchor and
the `--family` option.

Do not select a target when `candidates` marks it as a map defect. Recover the
missing function starts first. Confirm each start with retail code, relocation
data, or Ghidra evidence before you change the function map.

Use the target brief before editing. Run `tools/decomp evidence ADDRESS` only
to get evidence that is absent from a valid brief. Confirm most of these facts:

- subsystem and translation unit.
- calling convention, return type, and parameter roles.
- callers, callees, important constants, and side effects.
- global and structure accesses.
- a plausible source-level control-flow shape.

Use `tools/decomp data` for initialized globals. Use `tools/decomp data ADDRESS`
to inspect mismatched fields in one global. The report compares typed scalars,
arrays, pointers, and initialization state. A data difference can show an
incorrect type, layout, initializer, pointer target, or ownership boundary.
Confirm each conclusion with callers, retail data, or DWARF evidence.

Select data work with `tools/decomp data --limit 10`. Reject targets without
caller, retail, or DWARF evidence. Do not select BSS or unscored globals.

If the first target lacks evidence, pivot within the same subsystem. Restore
all source trials for the current target first. Run the doctor for only the new
address. Run two independent read-only scout audits, then create the
doctor-bound brief for the new address. Add the pivot before new source work:

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

The state keeps all attempted addresses for audit. Only active addresses drive
final source validation. A family expansion omits `--replace`, but each new
member still needs a fresh receipt, brief, and forecast. Prediction events keep
each target forecast. A pivot does not reset the original campaign deadlines.
Make no more than two pivots per campaign.
Stop when the campaign clock expires or the available evidence is exhausted. A
blocker is useful only when it identifies evidence that can unlock an unfinished
function. Record it with:

```sh
tools/decomp defer TARGET --blocked-by PREREQUISITE --kind layout \
  --reason "Needs the producer layout."
```

Omit `--blocked-by` only when no function can supply the evidence. Blockers are
advisory. Do not spend a campaign maintaining blocker prose. If the campaign
produces no supported source work, restore all source trials. Record each
rejected source model with the no-source result. This applies when the clock
expires or evidence is exhausted within the pivot cap. Search these notes with
`tools/decomp notes QUERY --source models` before a new model test.

Use `tools/decomp discover` only when the normal queue has no credible target.
Confirm a result with `tools/decomp evidence --unmapped ADDRESS` before you add
it to the map.

## Reconstruct plausible source

Work from the outside in:

1. Establish the ABI from callers and the epilogue.
2. Identify globals and field layouts. Reuse supported repository types.
3. Recover branches, loops, state dispatch, and cleanup paths.
4. Apply supported names.
5. Check OpenCrashWOC for terminology and architecture.
6. Write the simplest natural C++ that explains the observed behavior.

Preserve update order, aliasing, integer widths, signedness, floating-point
evaluation, callbacks, ownership, and failure paths. Do not paste decompiler
temporaries or replace known fields with byte offsets. Do not add speculative
abstractions, casts that hide a wrong type, dummy locals, or expression churn
only to improve a score.

Headers are part of the reconstruction. Check all visible users after a type or
layout change. Add size and offset assertions when the project supports them.

## Reconstruct translation units

Treat file placement as source evidence. Do not assume that a current catch-all
file matches the original translation unit. `Toy2.cpp` is a watchlist item, not
a special case.

Before you add code to a large or mixed file, inspect these facts:

- nearby addresses and names in the function map.
- retail source paths, asserts, and log strings.
- OpenCrashWOC translation units and related terms.
- shared globals, private helpers, callers, and callees.
- the file's namespaces, includes, static data, and initialization order.

Keep a coherent subsystem in one `.cpp` file when evidence supports that
boundary. A separate state group, private helper set, or narrow dependency set
can support a split. File size or namespace count alone cannot support a split.

When evidence supports a split, move the complete coherent slice. Preserve
namespaces, linkage, address annotations, data order, and initialization order.
Add a header only for real cross-file use. Add each new source file to CMake.
Check all visible users and compare each moved function after the move.

A file move can accompany valid source progress. A structure-only campaign
must remove tracked source debt to count as refinement progress. Do not create
a cleanup-only success commit.

Keep a large initializer in a named `.inc` file beside its owning `.cpp` file.
A string is large at 160 decoded bytes. A non-string array is large at 32
elements and 256 initialized bytes. The `.inc` file contains initializer tokens
only and must be in the target's CMake source list.

## Use comparison output

Use this loop:

```text
evidence -> plausible source -> format -> build and compare -> explain -> revise
```

Run `tools/decomp bc ADDRESS` after each meaningful function-model change. It
saves the full diff and a hash-bound mismatch sidecar. The sidecar records its
`HEAD`, source, build, and report provenance. Candidate selection and brief
creation reject a stale mismatch sidecar. The command also prints the score
ceiling and the ceiling-relative score. For data work, rebuild and run
`tools/decomp data ADDRESS`. Search compiler guidance with `tools/decomp notes
QUERY --source codegen`. Do not read large note files.

The final similarity must be at least 50 percent unless reccmp marks the
function exact or effective. A low score often shows an incorrect source
model. Inspect structural differences. Keep a complete, evidence-backed,
readable model when the ABI, behavior, side effects, and data model are
supported. Do not keep an opaque or guessed body.

Use `tools/decomp experiment` for distinct source-form trials. Stop when trials
no longer test a concrete model question. Do not polish functions above about
90 percent when only symbols, register allocation, or scheduling differ.

## Validate and finish

Stage the coherent campaign before finalization. The finalizer rejects
symbolic-link inputs and configured non-generated sources that are absent from
the staged Git tree. Run one applicable command:

```sh
tools/decomp finalize --result source --mode coverage --target ADDRESS --staged
tools/decomp finalize --result source --mode refinement --target ADDRESS --staged
tools/decomp finalize --result source --mode data --target ADDRESS --staged
tools/decomp finalize --result source --mode resource \
  --resource TYPE,ID,LANGUAGE --staged
```

Repeat `--target` for every active address in its recorded order.

Finalization can run one build, one code comparison, one data comparison, one
source scan, and one validation. It writes a content-addressed receipt under
`build/decomp-cache`. It must fail when an input hash or artifact hash does not
match. Do not accept a partial receipt.

The worker stages and finalizes its campaign. The supervisor reviews the staged
diff and receipt without another build. Revalidate only when a relevant input
hash changes. The campaign record must use the exact receipt artifacts.

Use the mode that matches the campaign. Validation rejects ABI, annotation,
source-quality, terminal-state, and unrelated regressions. It also rejects a
function below 50 percent. Do not use `--allow-target-regression` in a normal
campaign. Use it only with `--meta-resolution` for a workflow repair.

Data validation requires a source change and improved typed bytes. It rejects
selected, unrelated, aggregate, and data-section regressions. Use
`--meta-resolution` with `--accounting-correction "REASON"` for an accounting
correction.

A successful coverage campaign must change C++, convert a target to
`FUNCTION`, and increase the implemented count. A successful refinement
campaign must improve similarity, reach terminal status, or remove source
debt while terminal status remains. A successful data campaign must improve
explained typed bytes without regressions. All three campaign types are source
progress. Use `tools/decomp progress --json` before and after each campaign.
Record target scores and global metrics. Do not create a metadata-only success
commit.

After successful finalization, run `tools/decomp campaigns record --result
source`. The record command reads the valid receipt and does not rebuild.

For a no-source result, restore all source trials before record creation. Add
one `--model` option for each rejected source model. Finalize with the unchanged
baseline when all relevant hashes match. Do not build, run a full report, or
run Ghidra sync. Stage only the ledger and source-model note. An audit-only
memory commit does not count as source progress.

Pass all active targets to no-source finalization in their recorded order:

```sh
tools/decomp finalize --result no-source --mode MODE \
  --target ADDRESS --staged
```

Repeat `--target` for every active address. Use `--resource TYPE,ID,LANGUAGE`
instead for a resource campaign.

For a meta-fix result, run the focused tool tests and applicable validation.
Stage the bounded workflow fix, then run
`tools/decomp finalize --result meta-fix --mode meta --staged`. Record the
result with `--result meta-fix`. Stage the ledger with the workflow fix. Do not
count this commit as source progress.

Do not supply estimated times or byte counts to the record command.

After the record command, add the `staged` delivery event. An independent
reviewer then examines the staged files and the finalization receipt. Add the
`accepted` event only after this review passes. Stage the finalized campaign
files, the campaign row, and the `staged` and `accepted` entries. Commit this
set once. For a no-source result, the commit contains only the exact ledger and
source-model-note append. Post-commit delivery telemetry must not be in this
campaign commit.

```sh
tools/decomp campaigns delivery --campaign-id CAMPAIGN_ID --status staged
# Complete the independent acceptance review.
tools/decomp campaigns delivery --campaign-id CAMPAIGN_ID --status accepted
```

Fetch current `origin/agent/continuous` and rebase the campaign commit onto it.
If the upstream branch changed a finalized campaign path, reject the delivery
and repeat the campaign from the integrated base. Do not resolve and attest a
same-path conflict. Then set `COMMIT` to the resulting `HEAD` and `BASE` to its
first parent. Create the post-integration receipt:

```sh
COMMIT=$(git rev-parse HEAD)
BASE=$(git rev-parse HEAD^)
DELIVERY_RECEIPT=$(
  tools/decomp campaigns delivery-verify --campaign-id CAMPAIGN_ID \
    --commit "$COMMIT" --base-commit "$BASE" |
    python -c 'import json, sys; print(json.load(sys.stdin)["path"])'
)
```

The command saves the returned immutable receipt path in `DELIVERY_RECEIPT`.
The receipt rejects a conflict on a finalized campaign path. It binds the
campaign row, finalized tree, current `HEAD`, first parent, validation inputs,
and validation artifacts.

For source work, the verification rebuilds and validates the integrated source.
For a meta fix, it runs the focused tool tests and applicable checks. For a
no-source result, it verifies the integrated tree and index and the exact ledger
and source-model-note append. Do not run a report, build, or Ghidra sync for a
no-source result.

Use the same receipt for the three post-commit delivery events. Record and
deliver them in this exact order:

```sh
tools/decomp campaigns delivery --campaign-id CAMPAIGN_ID --status integrated \
  --base-commit "$BASE" --commit "$COMMIT" \
  --delivery-receipt "$DELIVERY_RECEIPT"
tools/decomp campaigns delivery --campaign-id CAMPAIGN_ID --status committed \
  --commit "$COMMIT" --delivery-receipt "$DELIVERY_RECEIPT"
# Push "$COMMIT" to origin/agent/continuous.
tools/decomp campaigns delivery --campaign-id CAMPAIGN_ID --status pushed \
  --commit "$COMMIT" --delivery-receipt "$DELIVERY_RECEIPT"
```

The `pushed` command verifies that the remote contains `COMMIT`. Stage the
three new entries only after that command passes. Put them in one small
telemetry-only follow-up commit, then push that commit. This follow-up does not
count as source progress.

If a push race changes the integrated commit, restore the uncommitted delivery
telemetry. Rebase the campaign commit again, set the new `COMMIT` and `BASE`,
and create a new delivery receipt. Do not reuse a receipt after `HEAD`, the
base, an input, or an artifact changes. A `rejected`
event ends delivery when integration, validation, or push cannot complete.

Generate the HTML report and run Ghidra sync after five accepted commits. Also
run them after a structural change or at session end. Inspect the whole-file
and data scores. Explain each score decrease. Do not hide a data, resource,
import, relocation, header, or debug-record regression.

Outside a meta campaign, run tool unit tests only when tool code changed. A
meta fix runs its focused tool tests. Reject a push when an integrated function
violates the 50 percent or terminal gate.

Do not stage unrelated work. The existing `external/submodules/reccmp` pointer
drift is allowed only while it stays unchanged. Never add generated `build/`
files.

## Writing style

Use ASD-STE100 Simplified Technical English for documentation, notes, errors,
warnings, and C/C++ comments. Use the repository `ste-writing` skill. These
rules do not apply to code, identifiers, or command syntax.
