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

Record each source model with a unique label:

```sh
tools/decomp experiment try 0x00401000 signed-loop \
  --question "Does the loop use a signed bound?" \
  --parent baseline --route control-flow \
  --model "Use a signed loop counter."
```

The controller reserves the trial before the build starts. A failed build or
comparison consumes the reserved trial. The wrappers use one shared build lock.

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

The baseline is always a ranked candidate. The rank uses terminal status,
target score, repository score, patch size, and trial order. Only eligible
trials can replace the baseline.

Advice returns `finalize` for an exact or effective result. It returns `stop`
after the deadline or trial limit. It returns `get-evidence` when one mismatch
route does not improve through the configured limit. Otherwise, it returns
`repair`.

Old flat experiment directories remain readable as `legacy-unverified`. Start
a bounded session before you use an old result. The experiment commands do not
apply patches, restore source, create worktrees, or change campaign records.
