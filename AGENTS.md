# Repository guidance

This file is the whole operating contract for reconstruction work. Each rule
says why it exists. The `continue-decomp` skill covers a session; the
`decomp-expert` skill covers one campaign. Nothing else is mandatory.

## Goal and metric

Reconstruct the Windows release of *Toy Story 2: Buzz Lightyear to the Rescue*
as readable C++ that the original developers could have maintained: coherent
types, descriptive names, natural control flow, engine-level concepts. reccmp's
instruction diff against the retail `toy2.exe` is the only judge of a match.

A function is terminal when reccmp marks it exact or effective and it has no
unsuppressed source-debt finding. Provisional, `STUB` and unstarted functions
are open work. Progress is a validated source commit (`tools/decomp throughput`).

## The loop

```sh
git status --short && git pull --rebase origin agent/continuous
tools/decomp progress --json
tools/decomp candidates --refine --yield --why --limit 15   # or --coverage --why
tools/decomp campaigns start --mode MODE --address ADDRESS --subsystem NAME
tools/decomp evidence ADDRESS
# edit -> clang-format -i FILE -> tools/decomp bc ADDRESS (repeat)
tools/decomp campaigns finish --mode MODE --target ADDRESS --note TEXT --message FILE
git push origin agent/continuous
```

`campaigns start` saves the baseline that `validate` and `record` compare
against, so run it before the first edit. `bc` builds, compares one function,
logs the attempt and prints a region index; read regions with
`bc ADDRESS --hunk N` (no rebuild), never the raw or compact diff file;
`bc ADDRESS --pack` prints the orientation pack a fresh batch starts from.
`campaigns finish` stages `src` and the map, runs `validate` (the only gate:
rebuild, compare, lint, regression check), records the ledger row from fresh
reports and commits the source, the ledger and `tools/Resources/scoreboard.tsv`
together, because a commit is what preserves progress; push after every campaign.

## Select work

Take the top row of `candidates --refine --yield --why` or, every third
campaign, the top dependency-ready row of `candidates --coverage --why`. Skip a
row only for a reason you write into the record note. Skip a row whose last
source result retained under 25 bytes unless a `campaigns evidence` row names
new evidence for it. A row marked `map defect` goes to `tools/decomp discover`
and `evidence --unmapped` first, because reccmp scores a function across its
map gap. Large bodies and functions below 50 percent are allowed; they hold most
of the remaining bytes. One target, or up to three related functions in one
subsystem, or one family anchor plus siblings that share a source form. The
rank is unresolved retail bytes, then evidence readiness, dependency impact and
source debt; the estimate columns order the queue and are never a reason to stop.

## Budget

Budget by attempts, not by the clock, because a timer that the loop itself
shortens starves the writer. One attempt is one `bc` run. Default 12 attempts;
24 for a family anchor or while the last four attempts gained a point or more.
Stop after three attempts without a half-point gain. Two pivots per campaign via
`campaigns add-target`. Time is recorded for measurement and never enforced.
These numbers are provisional until 40 campaigns are logged.

Always commit the best compiled model that passes `validate`. Record
`no-source` only when no attempt raised the score; then restore the tree with
`git checkout -- src` and pass one `--model` per rejected model so the next
writer does not retest it.

## Evidence and names

Use evidence in this order, because a name the binary states beats any guess:

1. Names quoted by the retail binary in asserts, logs and source paths
   (`tools/decomp notes QUERY --source names`). Read the strings section of
   `evidence` first; it names structures, fields and translation units.
2. Names and types in the Wrath of Cortex DWARF dump under
   `/run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC`. Toy Story 2 uses
   an older Nu3D: int16 fixed-point trig, 12-bit angles. Confirm every borrowed
   name against Toy Story 2 callers and the diff.
3. Role-based names confirmed by several uses.
4. `g_unkNNNNNN` only for a global with no supported role.

`tools/Resources/functions_map.txt` is the committed function-start map. Its
addresses are authoritative; its names are hypotheses. Search
`tools/decomp notes QUERY --source models` before a model trial, because
`.notes/source-models.md` records what earlier campaigns ruled out.

## Reconstruct plausible source

Work from the outside in: ABI from callers and the epilogue; globals and field
layouts, reusing supported repository types; branches, loops, state dispatch
and cleanup paths; supported names; OpenCrashWOC terminology; then the simplest
natural C++ that explains the observed behavior.

Change one source-level idea per `bc` cycle so the diff confirms or rejects
that idea. When two forms fit the evidence, build the simpler one; the build
decides, not the reasoning. A small inconsistency in the evidence is normal in
retail code; record it and let the build test the reading.

Preserve update order, aliasing, integer widths, signedness, floating-point
evaluation, callbacks, ownership and failure paths. Do not paste decompiler
temporaries or state byte offsets where a name belongs; `tools/decomp lint`
rejects that, because an exact match full of offsets is a transliteration.
Do not add speculative abstractions, casts that hide a wrong type, dummy locals
or expression churn only to raise a score. Do not polish a function above about
90 percent when only symbols, register allocation or scheduling differ.
A type one function needs goes file-local; a shared-header edit moves other functions.

## Translation units

File placement is evidence. Before adding code to a large or mixed file, check
nearby map addresses, retail source paths and asserts, OpenCrashWOC units,
shared globals and helpers, and the file's includes and initialization order.
Keep a coherent subsystem in one `.cpp` file unless a separate state group or
private helper set supports a split; then move the complete slice, preserve
annotations and data order, add the file to CMake, and compare every moved
function. A move must accompany source progress. Keep a large initializer (a
160-byte string, or 32 elements and 256 bytes) in a named `.inc` file.

## Validate and finish

`validate --mode MODE --target ADDRESS --staged` rejects: a regression of any
untouched function, loss of terminal status, new lint debt, a coverage target
below 50 percent (a complete body over 2,048 bytes passes at a quarter of its
ceiling, as PROVISIONAL), a refinement that did not improve similarity, reach
terminal status or remove debt. A refinement that raises a sub-50 function is
accepted below 50. Typed-data deltas print as warnings. Coverage must
convert a `STUB` or unstarted function to `FUNCTION` and change C++; a
metadata-only commit is not progress. Run `tools/decomp check` after a map
edit; `tools/decomp report` and `tools/decomp sync` once per session. Never
stage `build/` output or the `external/submodules/reccmp` pointer drift.

## Tooling rule

Tool and contract changes are not campaigns and earn no ledger row. Each one is
a tagged experiment: a `[T-nn]` commit, a row in
`tools/Resources/tooling-experiments.tsv`, and a keep-or-revert decision from
`tools/decomp throughput --experiments` after ten source campaigns. Tooling time
stays under 15 percent of the trailing week and under one hour per day
(`tools/decomp throughput --cap-check`). A tool defect met mid-campaign is
worked around, then fixed in a separate commit of at most 30 minutes. A failing
number is never answered with a new gate, review step or estimator: the
permitted responses are revert the last tooling commit, switch family, or ask
the user. `tools/tests/test_normative_inputs.py` caps the size of this file.

## Measurement

`tools/decomp throughput` reads git and the committed scoreboard: functions,
terminal bytes and effective bytes per source active hour, tooling share and
attempts per result. `campaigns summary` is the ledger cross-check. Floor: a
quarter of the Sep 8 daytime rate in `tools/Resources/throughput-baseline.json`;
below it, stop tooling and ask the user. Run both once per session and for the
weekly checklist in `docs/decomp-agent.md`.

## Writing

Plain, direct English in this file, notes and comments; each rule states its
purpose. ASD-STE100 applies to C/C++ comments and `.notes/` entries only.
