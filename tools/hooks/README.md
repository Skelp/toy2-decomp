# Git hooks

Enable once per clone with `git config core.hooksPath tools/hooks`.

`commit-msg` never checks source commits (anything staging `src/`, `functions_map.txt`, `campaign-ledger.jsonl` or `scoreboard.tsv`); a tooling-only commit must cite a task id such as `[T-12]` and pass `tools/decomp throughput --cap-check` plus `tools.tests.test_normative_inputs`. Tooling the user asked for is tagged `[T-nn][user]`; it skips the cap check, never the tests.
