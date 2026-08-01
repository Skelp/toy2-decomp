# Goal-mode decompilation supervision

Start Codex in the repository. Then continue the persistent reconstruction goal:

```text
/goal continue
```

The root agent uses the `continue-decomp` skill as a supervisor. It does not reconstruct functions itself.

The supervisor starts one `decomp-worker` subagent at a time. Each worker completes one bounded reconstruction slice.

After each worker, the supervisor checks the branch, worktree, submodule pointer, commit, and remote state. It starts another worker only after these checks pass.

The supervisor stops when work reaches a supported stalemate. It also stops when the user requests a stop or the goal lacks enough budget.

Use the Codex subagent controls to inspect the active worker. In the CLI, use `/agent` to view agent threads.

No repository tool starts Codex. This workflow does not use App Server, an MCP server, or an external supervisor process.
