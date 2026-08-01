# Goal-mode decompilation supervision

Start Codex in the repository. Then continue the persistent reconstruction goal:

```text
/goal continue
```

The root agent uses the `continue-decomp` skill as a supervisor. It does not reconstruct functions itself.

The supervisor starts one `decomp-worker` subagent at a time. Each worker completes one bounded reconstruction or meta-resolution slice.

After each worker, the supervisor checks the branch, worktree, submodule pointer, commit, and remote state. It starts another worker after these checks pass.

A worker stalemate starts meta-resolution work. The supervisor delegates distinct blocker, discovery, evidence, metadata, or tooling tasks.

The supervisor stops only after all applicable routes require unavailable external evidence or new user authority.

Use the Codex subagent controls to inspect the active worker. In the CLI, use `/agent` to view agent threads.

No repository tool starts Codex. This workflow does not use App Server, an MCP server, or an external supervisor process.
