# Fresh-thread decompilation agent

The agent runner supervises `codex app-server` in the foreground. Each reconstruction cycle uses one turn in a new thread.

Start a run on the `agent/continuous` branch:

```sh
tools/decomp agent doctor
tools/decomp agent run
```

The default run permits 32 cycles. A cycle ends the run when the agent does not request a successor.

Use these commands from another terminal:

```sh
tools/decomp agent status
tools/decomp agent log
tools/decomp agent stop
tools/decomp agent stop --force
```

The first stop request asks the active turn to stop at a safe boundary. A forced stop interrupts the active turn.

The runner stores structured logs under `build/decomp-agent/<run-id>/`. Git ignores this directory.

The runner starts App Server with `danger-full-access` and an approval policy of `never`. It registers only the run-scoped rotation MCP server.

The runner inherits the configured model, reasoning effort, and service tier. Use `--model`, `--effort`, or `--service-tier` for an explicit override.

The rotation tool accepts one bounded handoff. It rejects a request unless all repository checks pass.

- The branch must be `agent/continuous`.
- `HEAD` must advance during the cycle.
- The working-tree state must match the startup state.
- Only an unchanged reccmp submodule pointer can exist at startup.
- `HEAD` must equal `origin/agent/continuous`.

The runner starts the successor before it archives the predecessor. It retries successor startup three times.

`agent doctor` checks Codex 0.146, authentication, the repository, the MCP handshake, and required App Server methods. It does not start a model turn.
