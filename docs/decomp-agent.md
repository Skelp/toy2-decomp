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
