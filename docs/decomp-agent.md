# Goal-mode decompilation supervision

Start Codex in the repository and run:

```text
/goal continue
```

The root agent uses `continue-decomp` as a supervisor. It does not reconstruct
functions. It starts one fresh `decomp-expert` subagent per campaign with
`fork_turns: "none"`. A campaign reconstructs one large function or at most
three related functions in one subsystem.

After each campaign, the supervisor verifies the branch, pushed commit,
worktree, and implemented-function delta. A campaign counts as progress only
when it changes C++ and increases the implemented count. Metadata-only commits
do not count.

The supervisor does not reuse workers. It closes a completed worker when the
interface provides `close_agent`. Otherwise, it confirms that the worker is
`Done` before it starts the next worker. Use `/agent` to inspect entries.

The supervisor stops after three consecutive no-source campaigns. A worker can
pivot twice within its subsystem before it returns no-source. The supervisor
assigns a bounded meta-resolution campaign only when a workflow failure blocks
all source work.

No repository tool starts Codex. This workflow does not use App Server, an MCP
server, or an external supervisor process.

## Function context packs

Each new function brief contains a bounded `evidence.context_pack`. The pack
uses schema version 1 and cached doctor artifacts. It does not start Ghidra.
It records the x86-32 instructions, control flow, memory operands, direct call
neighbors, and as many as three accepted-source examples. Each derived row has
a locator for its doctor artifact.

The pack records an unknown reason when evidence is missing or ambiguous. Do
not use an incomplete control-flow or memory section as proof. The builder does
not guess an indirect branch target or a jump table.

Use this read-only command to inspect and validate the pack:

```sh
tools/decomp context --brief build/decomp-cache/briefs/BRIEF.json
tools/decomp context --brief build/decomp-cache/briefs/BRIEF.json --json
```

The command checks the brief hash, pack hash, target, retail size, and current
repository inputs. It rejects linked, changed, large, or non-cache brief files.
The brief tools can still read historical briefs that do not contain a pack.

## Bounded source experiments

Use `tools/decomp experiment` to compare distinct source models for one
function. The Linux and Windows wrappers use the same experiment controller.

Start a session before the first source edit:

```sh
tools/decomp experiment start 0x00401000 \
  --brief build/decomp-cache/briefs/BRIEF.json \
  --max-trials 3 --max-non-improving 2
```

Each start creates a unique session directory. The
`current-session.json` file points to the active session. The session binds the
HEAD, campaign, deadline, brief, function map, tools, and compiler context.

For normal work, you can record each source model with `try`:

```sh
tools/decomp experiment try 0x00401000 signed-loop \
  --question "Does the loop use a signed bound?" \
  --parent baseline --route control-flow \
  --model "Use a signed loop counter."
```

The controller reserves the trial before the build starts. A failed build or
comparison consumes the reserved trial. The wrappers use one shared build lock.

A replay-eligible trial uses two steps. First, prepare the branch while the
source tree is an exact copy of the declared parent:

```sh
tools/decomp experiment branch 0x00401000 signed-loop \
  --session SESSION_ID \
  --question "Does the loop use a signed bound?" \
  --parent baseline --route control-flow \
  --model "Use a signed loop counter."
```

Save the returned trial ID. Then edit the source and measure the prepared
branch:

```sh
tools/decomp experiment measure 0x00401000 TRIAL_ID --session SESSION_ID
```

The `branch` command records the complete parent-source snapshot. The
`measure` command records the complete result snapshot. It rejects a changed
prepared parent. It does not restore or apply source files.

The controller selects all artifact paths before wrapper work. Each report and
verbose diff has a provenance sidecar. The controller rejects changed, large,
linked, escaped, stale, or incomplete artifacts.

Use these read-only commands to inspect the session:

```sh
tools/decomp experiment status 0x00401000
tools/decomp experiment best 0x00401000
tools/decomp experiment advise 0x00401000
tools/decomp experiment report 0x00401000 [label]
```

The baseline is always a ranked candidate. The rank uses only the target
status and the target score rounded to one millionth. An eligible trial must
be strictly better. A tie keeps the earlier incumbent and counts as a
non-improving trial. Repository totals and patch size are report data only.

Advice returns `finalize` for an exact or effective result. It returns `stop`
after the deadline or trial limit. It returns `get-evidence` when one mismatch
route does not improve through the configured limit. Otherwise, it returns
`repair`.

This `experiment advise` result is local to one source session. It does not use
the private benchmark certificate. The separate `route advise` command below
requires a current passing certificate.

Old flat experiment directories remain readable as `legacy-unverified`. Start
a bounded session before you use an old result. The experiment commands do not
apply patches, restore source, create worktrees, or change campaign records.

## Private replay benchmark

The private replay benchmark checks the fixed route-selection policy against
delivered experiment trajectories. Route advice is unavailable until the
current private suite has a passing certificate.

### Seal a delivered experiment

Use only `branch` and `measure` for a future replay case. A legacy `try` trial
is not replay eligible. A session uses these fixed limits:

- Three measured trials at most.
- Two consecutive non-improving trials at most.
- Two or three completed measurements in an eligible replay trajectory.

The trajectory must contain at least one strict eligible improvement and one
non-improving result. It must stop at a terminal incumbent, after two
consecutive misses, or at the three-trial limit. One parent must have two
outgoing edges with different route labels.

After the last measurement, seal the trajectory before campaign finalization:

```sh
tools/decomp experiment seal 0x00401000 --session SESSION_ID
```

This command writes the full trajectory to private storage. It attaches only
an opaque five-field commitment to the active campaign. Finalization, the
campaign record, and the delivery chain retain that commitment. Replay later
requires an exact match between the live session, the private seal, the
finalization receipt, and all delivery evidence.

Keep these immutable inputs:

- The experiment session and all pending and completed trial receipts.
- The doctor receipt, Ghidra artifacts, and target brief.
- The finalization receipt and all frozen finalization artifacts.
- The impact review and leaf-oracle receipts.
- The delivery receipt and the five campaign-ledger delivery rows.
- The recorded reccmp checkout commit and its local Git objects.

Keep the two raw scout report files unchanged through finalization and
delivery. The immutable brief contains their exact validated documents.
Historical replay uses these embedded documents. It does not reopen a generic
scout report filename after delivery.

The recorded reccmp checkout can differ from the superproject gitlink. Replay
fails closed if that exact commit or its blobs are no longer available in the
local reccmp object database. The superproject branch alone cannot recover
this dependency.

### Enroll private cases

Enrollment is one atomic batch. Start an active `meta`/`meta` campaign with
the exact subsystem `private-replay-enrollment`. Then run one command with one
or more case arguments:

```sh
tools/decomp replay enroll \
  --case 0x00401000,SESSION_ID \
  --case 0x00402000,SESSION_ID --json
```

The command validates all candidates before it publishes any case. It rejects
duplicate source campaigns and duplicate targets. It also requires the local
`agent/continuous` branch, `HEAD`, and
`refs/remotes/origin/agent/continuous` to identify the same commit.

The public manifest is
`tools/Resources/private-replay-manifest.json`. Each row contains only a
random case ID, a byte count, and a SHA-256 value. The full cases,
trajectories, certificates, pointer, lock, and transaction journal stay under
the root-anchored `.decomp-replay` directory. Directories use mode `0700` and
files use mode `0600`. This is the only replay-data root. Replay does not store
or migrate private data in `.decomp-local`. The POSIX wrapper can still read
`.decomp-local` as legacy build configuration before it dispatches a replay
command.

Private publication requires secure POSIX file operations and Linux
`renameat2` no-replace behavior. It fails closed on a platform that cannot
provide these operations. A stopped enrollment can leave a transaction
journal. The next enrollment completes or rolls back an exact journal state.
Certification and route advice stay unavailable while a journal is present.

### Run and certify the suite

Use these commands:

```sh
tools/decomp replay list --json
tools/decomp replay run --json
tools/decomp replay certify --json
tools/decomp replay verify --json
```

An empty public manifest returns an unavailable result. It does not create or
inspect private storage. A populated suite passes only when all current files,
Git identities, private objects, historical receipts, and source-delivery
links pass exact revalidation.

The fixed certificate thresholds are:

- 12 cases, 12 source campaigns, and 12 targets.
- Four canonical subsystems and four route labels.
- 24 replay edges.
- Three terminal oracle cases.
- No invalid or unsupported case.
- Positive candidate gain in every case.
- Aggregate candidate gain of at least 90 percent of aggregate
  exhaustive-oracle gain. The exact test is
  `sum(candidate_gain) * 10 >= sum(oracle_gain) * 9`.
- Recovery of every terminal result found by the oracle.

The candidate policy selects only an untried outgoing route from the current
incumbent. The exhaustive replay can select any unchosen edge whose parent is
already discovered. Both policies see an edge outcome only after they select
that edge. A terminal-oracle case is a case in which exhaustive replay reaches
an eligible `effective` or `exact` incumbent. The leaf differential oracle is
separate delivery provenance. It does not classify or count replay terminal
cases.

The certificate is current only on the symbolic `agent/continuous` branch
when `HEAD` equals the local origin-tracking reference. The manifest, ledger,
policy tools, wrappers, resources, index, and worktree must equal the captured
`HEAD`. Replay does not fetch, commit, stage, or push.

### Request route advice

The observation contains no target, session, campaign, path, hash, score, or
outcome identifier. Use this schema:

```json
{
  "schema_version": 1,
  "kind": "route-observation",
  "routes": ["control-flow", "integer"],
  "tried_routes": [],
  "frontier": [{"route": "control-flow", "count": 1}],
  "selected_trials": 0,
  "consecutive_non_improving": 0,
  "incumbent_terminal": false
}
```

Run:

```sh
tools/decomp route advise --observation OBSERVATION.json --json
```

Advice is withheld when the certificate is absent or stale, the observation
is invalid, the incumbent is terminal, three trials were selected, or two
consecutive trials did not improve. The selector reads only the incumbent
routes, tried routes, route counts, and stop state. It does not read hidden
child outcomes.

### Trust boundary

The benchmark detects changes made after delivery. It does not prove that a
malicious operator reported truthful evidence before finalization. There is no
external append-only service. The local Python interpreter, import machinery,
import cache, external entrypoints, and pre-finalization host are trusted. The
certificate still binds and revalidates the tracked source of every semantic
dependency and the immutable delivered evidence.

## Option population studies

The option-study registry is
`tools/Resources/decomp-options.json`. Its schema requires each option and the
route-training policy to be disabled. Activation receipt fields must be
`null`. The current registry has no options and no study preregistrations.
Thus, all options and route training are unavailable.

Use these commands to inspect the public state:

```sh
tools/decomp options status --json
tools/decomp options training-status --json
tools/decomp options require OPTION --json
tools/decomp study list
```

The `require` command always fails closed. The status and list commands read
only the tracked registry. An empty registry does not create, lock, or inspect
`.decomp-replay`.

### Preregister a study

A future meta campaign must add the complete preregistration to the tracked
registry before it collects a result. The preregistration fixes these items:

- The private population through opaque case, campaign, and target
  commitments.
- The baseline, all treatments, and each complete option map.
- The primary metric and its direction.
- Each protected metric and its direction.
- The exact per-case budget and the no-early-stop policy.
- The missing, error, invalid, and extra-row failure policy.
- Whether the declaration or corpus is synthetic.

The study ID is the SHA-256 value of the normalized preregistration. Do not put
an introduction commit in the preregistration. That value would create a Git
self-reference. Give the introduction commit to the ingest command instead.
The tool proves that this commit contains the exact preregistration. It also
proves that the commit is an ancestor of the current local and origin branch.
Its commit time must not be later than an observation time.

Each study uses the exact Cartesian product of the declared population and the
baseline plus all treatments. The certifier enumerates the one stored corpus.
It rejects a missing, extra, duplicate, conflicting, error, invalid, or
incomplete-budget row. It does not accept a selected result list. An exact
logical retry is idempotent. A different result corpus or rerun requires a new
preregistration.

### Store and inspect an unverified corpus

The input file must be a bounded, owner-only regular file with mode `0600` and
one link. The reader does not follow symbolic links. Use these commands only on
Linux or another POSIX system that provides the required secure file
operations:

```sh
tools/decomp study ingest STUDY_ID \
  --preregistration-commit COMMIT --input PRIVATE_OBSERVATIONS.json
tools/decomp study evaluate STUDY_ID --require-pass
tools/decomp study certify STUDY_ID --require-pass
tools/decomp study verify STUDY_ID RECEIPT_SHA256 --require-pass
```

The PowerShell wrapper has the same command dispatch and help. It provides
parity only. All option and study registry commands fail closed on native
Windows because the required descriptor-relative checks are unavailable. Run
the POSIX command in a trusted Linux or WSL environment.

The commands store private corpora and content-addressed receipts below
`.decomp-replay/studies`. Directories use mode `0700`, and files use mode
`0600`. Keep this ignored directory private. Public output contains only
opaque commitments, content descriptors, aggregate counts, and stable failure
codes. It does not contain individual rows, private names or paths, per-row
metric values or scores, or per-row labels. Aggregate primary values, route
counts, label counts, and win, tie, and loss counts can be public.

Each storage or receipt operation requires the symbolic `agent/continuous`
branch. `HEAD` must equal its local origin-tracking ref. The index must be
clean, and the trusted study tools, registry, and wrappers must exactly match
`HEAD`. The committed ignore rule must be root-anchored. The full `HEAD`
history must not contain the private root. Each receipt also binds and
revalidates the Git and Python runtime identities.

All command-line inputs have acquisition class `unverified-external`. A caller
cannot change this class with `synthetic: false` or with plausible hashes. The
`certify` command can issue only a failed receipt with
`unverified-acquisition`. Synthetic fixtures also fail. A future authoritative
controller can use the internal verified-evidence evaluator after its own
receipt design is complete. This campaign does not provide that controller.

### Study acceptance rule

A verified future study passes only when all these conditions are true:

- There are at least 20 distinct cases, 20 campaigns, and 20 targets.
- A treatment is eligible only if it has at least 10 non-tied pairs, zero
  primary losses, and zero protected-metric regressions.
- Exactly one treatment is eligible and has the unique best primary
  aggregate.
- Every declared treatment is complete and remains in the correction count.
- The exact one-sided sign-test value satisfies
  `sum(comb(n, i), i = wins..n) / 2**n <= 1 / (20 * N)`, where `N` is the
  number of declared treatments.

For one treatment, `n` excludes ties. The sign test uses integer combinations
and rational arithmetic. It does not use a floating-point approximation. A
tie for the best aggregate, a missing value, or an invalid value fails the
study.

### Route-policy data gate

The route corpus must name an exact, current, content-addressed, passing study
receipt. The gate reopens and revalidates that receipt. It does not accept a
caller status object. Store and inspect a route corpus with these commands:

```sh
tools/decomp study training-ingest STUDY_ID \
  --study-receipt STUDY_RECEIPT_SHA256 --input PRIVATE_ROUTES.json
tools/decomp study training-evaluate STUDY_ID --require-pass
tools/decomp study training-certify STUDY_ID --require-pass
tools/decomp study training-verify STUDY_ID GATE_RECEIPT_SHA256 --require-pass
```

A future verified data gate requires at least 200 valid rows, 50 campaigns,
and 50 targets. It also requires at least five canonical routes with 20 rows
each, 50 positive labels, and 50 negative labels. The train and test sets must
both be nonempty. Each campaign-and-target group must be wholly in one set.
Duplicate row commitments, commitment namespace overlap, group overlap,
unknown routes, unexpected fields, and invalid rows fail the gate.

The current route ingest path is also `unverified-external`. Therefore, it can
issue only a failed gate receipt. A synthetic route corpus also fails. The tool
does not train a model, change the route policy, or activate an option.
