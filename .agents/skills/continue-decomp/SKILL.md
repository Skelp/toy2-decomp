---
name: continue-decomp
description: Start or continue a Toy Story 2 decompilation session. Use at the start of a session or when asked to continue decompilation, pick the next function to decompile, make progress, or reconstruct a target. Establishes baseline, selects a target, works on the persistent agent/continuous branch (creating it from main if absent), and drives the validate-sync-report loop.
---

# Continue Decompilation

This is the **procedure**. The judgment rules (ABI recovery, struct layouts,
naming, anti-score-chasing) live in `AGENTS.md`. This file does not repeat them.

Run from the repository root: `/run/media/skelp/1TB/venvs/toy2-decomp`.

**The discipline: select fast, commit early, investigate on the branch.** The
most common failure mode is surveying candidates without ever starting. The
build-compare loop is the real validation. Pre-investigation is not.

## 1. Read this much, and no more

| Read | When |
| --- | --- |
| This file | Always. |
| `.notes/codegen-index.md` | Always. It is one line per known symptom. |
| `.notes/original-names.md` | Before you name any structure, field, or global. |
| `AGENTS.md` sections | On demand. Its table of contents says which. |
| `.notes/refactor-debt.md` | Always. Clear this list before new reconstruction. |
| `.notes/codegen-rules.md` §`FIX-nn` | A diff matches that index row. |
| `.notes/codegen-caps.md` §`CAP-nn` | A diff matches that index row. |
| `.notes/reccmp-mechanics.md` §`TOOL-nn` | An annotation or tool problem. |
| `.notes/shared-globals.md` | Your target reads an unfamiliar data address. |
| `.notes/README.md` | You want to add a note. |

Do **not** read the detail note files end to end. They are reference, indexed
by ID from `codegen-index.md`. Reading them in full costs about 20 000 tokens
and returns almost nothing you will use for one function.

## 2. Establish the baseline

```sh
git status --short                 # no overlapping unrelated local changes
tools/decomp check                 # functions_map.txt invariant, must exit 0
tools/decomp progress              # the denominator
tools/decomp baseline              # saved comparison and build identity
```

- If `check` fails, **fix the map first**. That is the prerequisite, not your
  target.
- If `build/decomp-report-data.json` is absent or stale, run `tools/decomp
  build` and `tools/decomp report`. `candidates` reads that file for the match
  percentages.

## 3. Select a target

```sh
tools/decomp audit --status                   # show the required audit scope
tools/decomp candidates --limit 15 --why      # show required pending audits
tools/decomp candidates --new-work --why      # use only for same-session verification
tools/decomp evidence 0x004XXXXX              # the single best candidate
```

**Complete the required audits before new work.** During the freeze, the
default list contains pending former CAP, sub-50 percent, and verified-code
debt audits. Use `--new-work` only if the function can become exact, effective,
or tool-equivalent in the same session.

For a source-debt audit, use the order in `.notes/refactor-debt.md`. The file
starts with the smallest supported fixes.

`candidates` implements the rubric in `AGENTS.md`: `STUB` first, then a small
unannotated function with reconstructed siblings, then a larger one, then an
implemented function still below a match. It ranks a function with lint errors
as real work even at 100%. It gives legacy CAP claims a higher audit rank. It
hides only verifier-confirmed rows from `tools/Resources/tool_artifacts.tsv`.
Useful filters are `--debt`, `--stubs`, `--leaves`, `--near`, `--max-size N`,
and `<namespace>`.

After an audit, replace the placeholder uncertainty and revisit trigger. Set
`audit-state` to `audited`. Then run `tools/decomp audit --refresh-ledger`.
The refresh preserves manual evidence and updates the measured state.

`evidence` returns the map neighbors, the annotation state and owning TU, the
decompilation, the callers, the callees, the referenced **strings**, and the
referenced data addresses in one call. Add `--disasm` only when the
decompilation looks wrong.

**Read the strings section first.** The retail build kept its assert text, which
quotes the developers' own expressions. A line such as `drawb->VerticeCount[i]`
hands you a structure name, a field name, and proof the member is an array. A
`C:\projects\...` path names the original translation unit. Those names outrank
anything you would invent, and missing them is how a function ends up stating
byte offsets.

**Gate.** Use the "Minimal candidate checklist" in `AGENTS.md` after the first
`evidence` call. Get more evidence when one focused query can answer a missing
item. Defer the target when its ABI, data model, or control flow stays unclear.
A correct deferral is useful work and does not require a commit.

## 4. Get on the branch — immediately after selection

Use the **persistent** branch `agent/continuous`. Reuse it if it exists; create
it from `main` only if it does not.

```sh
git rev-parse --verify agent/continuous >/dev/null 2>&1 \
  && git checkout agent/continuous \
  || { git checkout main && git checkout -b agent/continuous; }
```

- One shared branch across sessions. Push completed work before the session
  ends. Do **not** create a per-function branch.
- Make sure the working tree is clean before you start a new function. If `main`
  has advanced, `git rebase main` first.
- Session scope: one function, or one tightly coupled cluster plus the required
  header and layout changes. Choose the TU by subsystem ownership, not by
  address proximity.
- Make reasonably sized commits. A one- or two-line code change does not justify
  its own commit. Group small changes only when they form one coherent
  reconstruction slice in the same subsystem. Do not combine unrelated changes
  to inflate a commit.

## 5. Reconstruct — on the branch

Follow `AGENTS.md`, "Reconstructing plausible source" and "Using compiler
comparison productively".

```sh
clang-format -i <touched files>
tools/decomp bc 0x004XXXXX          # build, then the verbose comparison
tools/decomp score 0x004XXXXX ...   # percentages only, for a cluster check
tools/decomp experiment start 0x004XXXXX
tools/decomp experiment try 0x004XXXXX natural-form
```

- Change **one** source-level idea per cycle, so the result confirms or rejects
  that idea. If an idea does not move the diff toward a structural match,
  revert it. Do not accumulate speculative edits.
- Write the simplest supported form when the evidence is sufficient. Then use
  the comparison to test the source model. Do not write a complete body only to
  satisfy a time, tool-call, or commit target.
- **When two forms both fit the evidence, pick the simpler one, build it, and
  note the alternative in the final report.** Do not choose between them by thinking.
  If the simpler form regresses, that is your answer; `git restore` and take the
  other one. Two builds cost about 45 seconds.
- **A small inconsistency in the evidence is normal.** An array with one more
  entry than its loop bound, an oversized buffer, an unused slot: retail is full
  of these. Do not treat one as proof that your whole model is wrong. Record it,
  choose the reading that satisfies the arithmetic, and let the build test it.
- **Before you theorize about a diff, match it against `.notes/codegen-index.md`.**
  A `FIX-nn` row suggests a source form to test. A `CAP-nn` row is a legacy
  observation that requires an audit.
- Change one source-level idea in each experiment. Stop when another form no
  longer tests a source-model question. The number of attempts does not prove
  that a compiler quirk caused the mismatch.
- Do not add rows to `.notes/caps-registry.tsv`. A mismatch stays provisional
  unless it is exact, reccmp-effective, or a verified tool artifact.
- **An exact match is not the finish line.** `tools/decomp lint` must report no
  new error. A 100% match that states byte offsets where a name belongs, or
  carries a `fieldNN` parameter, is a matched transliteration. Declare the
  structure, or leave the function a `STUB` for a session that can. The score
  cannot see this, which is exactly why the lint exists.
- Keep the verification tag from the fresh report. Use `[MATCHED]` for exact
  clean source, `[EFFECTIVE]` for reccmp-effective clean source, `[TOOL]` for a
  verified tool artifact, and `[PROVISIONAL]` for all other `FUNCTION` bodies.
- If reconstruction shows the candidate was genuinely opaque, `git restore` and
  take the next candidate. Do not delete the shared branch.
- Stop a related cluster when two siblings remain below 50 percent under the
  same source model. Investigate the common layout, macro, or control flow
  before you implement another sibling.

**Debt items invert the order: write, build, compare, then judge.** A debt item
already matches the retail code, so you have a known-good baseline that ordinary
reconstruction lacks. Declare the type, build, and read the score. If it holds,
your layout is right. If it drops, it is wrong, and you know within a minute.
Do not settle a type design by reasoning when a build will decide it for you.
`.notes/refactor-debt.md` gives the layouts and states which form to use.

## 6. Validate and commit — per function

1. Format the touched files with the repo `.clang-format`.
2. Stage the intended source. Run `tools/decomp validate --target 0x004XXXXX
   --staged`.
   Validation rejects a target below 50 percent by default. Keep that target as
   a `STUB` unless a maintainer reviews it. A maintainer can set the ledger
   origin to `maintainer-review` and use `--allow-low-score`.
3. `tools/decomp compare` and inspect the target's differences.
4. `tools/decomp lint` — no new error. This is a gate, not advice.
5. `tools/decomp check` — only if a map entry or an annotation changed.
6. `tools/decomp progress` — only if annotations changed.
7. `git diff --check`, then review `git diff` and `git status --short`.
8. **Commit** to `agent/continuous` with a clear message
   (`Implement <Namespace>::<Function>`). Do not commit a one- or two-line code
   change by itself. Extend the coherent target or cluster until the commit has
   enough substantive reconstruction work to stand on its own.

## 7. Finish the session — once, not per commit

These three steps are per **session**. Running them after every commit spends
minutes and returns nothing.

1. `tools/decomp sync` — reccmp's headless importer pushes every matched
   function's name, signature, and types into Ghidra in one transaction. The
   `ghidra` CLI bridge stops for the import and restarts afterwards.
2. `tools/decomp report` — writes `build/decomp-report.html` and
   `build/decomp-report-data.json`. The next session's `candidates` reads the
   JSON, so this step is what keeps selection accurate.
3. `git push origin agent/continuous` (use `-u` on the first push; rebase first
   if the remote has advanced).

## Gotchas

- Use `candidates` and `evidence` before custom discovery work. Defer a target
  when focused evidence does not support its source model.
- **Do not polish near-matches.** Cycling functions above 90% for a
  source-fixable diff produces no reconstruction. The priority is `STUB`s and
  unannotated leaves.
- **The score is a metric, not the goal.** It is measured every cycle, so it is
  the easiest thing to optimize and the easiest trap. Source plausibility is
  measured only by `tools/decomp lint` and by your own judgment. A session of
  exact matches full of `g_unkNNNNNN` and byte offsets is a bad session.
- **The binary documents itself.** Assert strings carry field names, struct
  names, and original file paths. `.notes/original-names.md` has the extraction.
  Check it before inventing a name, and read the strings section of
  `tools/decomp evidence` before writing a byte offset.
- **`[MATCHED]` follows reconstruction, not precedes it.** Tag it only after
  *you* reconstructed the function to an exact match this session and confirmed
  it. Do not bulk-retrofit `[MATCHED]` onto functions you did not work on.
- **`agent/continuous` is shared.** Commit and push every completed task. Never
  leave uncommitted work on it between sessions.
- **`functions_map.txt` is hand-maintained, not generated.** Source annotations
  do not update it. When you reconstruct, rename, or newly discover a function,
  edit its map entry. `tools/decomp check` enforces the invariants.
- **A higher diff percent can be a meaningless register-allocation change.**
  Treat a gain as a better model only when the edit is credible original source
  explaining a real structural idea.
- **Decompiler output is not pasteable source.** `iVar1`, `puVar2`, and
  offset-only fields are analysis artifacts. Ghidra also under-detects MSVC
  functions, so its function set is not ground truth. Note that `ghidra
  function calls` returns **callers**, not callees.
- **Prefer the vendored SDK headers over hand-rolled COM vtables.** The repo
  ships the DirectX 6 and 7 SDK under `external/include/directx6/` and
  `external/include/directx7/` as gitignored local inputs. Include the real
  header and use the C++ method syntax it declares. Do not rebuild a partial
  vtable struct of `void*` slots.
- **Map names are RE hypotheses, not original symbols.** No PDB ships and there
  are no PE exports. Strong leads, not proof.
- **The build must stay serial.** `TOY2_BUILD_JOBS` exists but only 1 works:
  VC6 shares one `vc60.pdb` per target. See `docs/linux-decomp.md`.
- **The tools are quiet now.** No `msvc600` banner and no `libEGL` warnings.
  Do not add noise filters to your commands; filter only for what you want.
