# Running the reconstruction loop with an agent

Every harness reads the same contract: `AGENTS.md` (Claude Code imports it via
`CLAUDE.md`; Codex and pi read it directly). The skills under `.agents/skills`
order a session (`continue-decomp`) and one campaign (`decomp-expert`).

- Claude Code: start `claude` in the repository root and say "continue decomp".
- Codex: `/goal continue` with the `continue-decomp` skill.
- Subagents, worktrees and harness-specific control calls are optional.

Scripted runs (`tools/decomp campaigns run`; `--harness auto` picks the session):
1. `tools/decomp campaigns run --check --live`, then fix each `FAIL` line.
2. A run takes minutes per campaign, so start it in the background: a background
   Bash call in Claude Code; in Codex
   `nohup tools/decomp campaigns run --count N > build/decomp-runs/run.out 2>&1 &`.
3. Watch it with `tools/decomp campaigns run --status`, not by reading logs. The
   last line is `run: exit N (REASON); next: COMMAND`.
4. Stop it with `kill -TERM PID` (a background run ignores Ctrl-C; `kill -9`
   leaves the writer running). The open campaign is aborted; restore src with
   `git checkout HEAD -- src tools/Resources/functions_map.txt`. To keep the best
   model, then start a campaign, `git apply build/decomp-cache/best/ADDR.patch`, finish.

A Codex sandbox blocks the network of its commands, so no writer reaches its API:
set `sandbox_workspace_write.network_access = true` or run outside it. A codex
writer marks an untrusted checkout trusted in `~/.codex/config.toml`.

Progress is a validated source commit: `tools/decomp throughput` (git plus
`tools/Resources/scoreboard.tsv`), cross-checked by `tools/decomp campaigns summary`.

Harness settings that matter: medium or high reasoning effort (low effort wastes
builds); tool output of at least 30k characters (the codex writer sets 12k tokens);
network access for `git push`; write access to `build/`.

Weekly checklist for the repository owner:
1. `tools/decomp throughput --window-days 7` and `--experiments`.
2. `git log --since=7.days --stat -- tools AGENTS.md .agents docs` matches
   `tools/Resources/tooling-experiments.tsv`.
3. `tools/decomp score` on two random committed addresses matches their rows
   in `tools/Resources/scoreboard.tsv`.
4. `.tooling/venv/bin/python -m unittest discover -s tools/tests` is green.
