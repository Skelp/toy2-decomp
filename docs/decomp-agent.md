# Running the reconstruction loop with an agent

Every harness reads the same contract: `AGENTS.md` (Claude Code imports it via
`CLAUDE.md`; Codex and pi read it directly). The skills under `.agents/skills`
order a session (`continue-decomp`) and one campaign (`decomp-expert`).

- Claude Code: start `claude` in the repository root and say "continue decomp".
- Codex: `/goal continue` with the `continue-decomp` skill.
- No harness-specific control call is required; subagents and worktrees are
  optional.

Progress is a validated source commit. The numbers come from
`tools/decomp throughput` (git plus `tools/Resources/scoreboard.tsv`) and are
cross-checked by `tools/decomp campaigns summary` (the ledger).

Harness settings that matter: use medium or high reasoning effort for the
writer (a byte-matching loop at low effort wastes builds); allow tool output of
at least 30k tokens so diffs are not truncated; allow network access for
`git push`; give the agent write access to `build/`.

Weekly checklist for the repository owner:

1. `tools/decomp throughput --window-days 7` and `--experiments`.
2. `git log --since=7.days --stat -- tools AGENTS.md .agents docs` matches
   `tools/Resources/tooling-experiments.tsv`.
3. `tools/decomp score` on two random committed addresses matches their rows
   in `tools/Resources/scoreboard.tsv`.
4. `.tooling/venv/bin/python -m unittest discover -s tools/tests` is green.
