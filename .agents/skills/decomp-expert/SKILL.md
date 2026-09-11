---
name: decomp-expert
description: Run one batch (about four bc attempts) or one validate repair of a Toy Story 2 reconstruction campaign. The orchestrator supplies the target, the phase and an orientation pack; this file is the exact command sequence.
---

# Decomp expert

AGENTS.md (in your context via CLAUDE.md) holds the rules; this file holds the mechanics.
Never read AGENTS.md, CLAUDE.md or this file again. Never read tools/*.py or --help; if a
tool prints something you cannot act on, quote it in the handoff TOOL line and stop.

## Tool facts

- `tools/decomp bc ADDR` builds, compares, logs one attempt, saves the diff and prints
  `attempt K/N raw X% (+d) best Y% (attempt k)` plus a region index
  (`N  0xADDR  -a +b  FILE:LINES`). It is the only command that consumes an attempt.
- `bc ADDR --hunks` reprints the index and `bc ADDR --hunk N` one region, both from the
  saved diff, without building. After an edit, plain `bc` first.
- Never pipe `bc` into `head` or `tail`: a closed pipe aborts the build. Remove noise
  only with the grep filter in the cycle below.
- A `header side effect:` line under the index means your header edit moved an untouched
  function; make the type file-local in the next attempt.
- The best-scoring tree is saved as `build/decomp-cache/best/ADDR.patch` (with its diff
  beside it); `git checkout -- src && git apply build/decomp-cache/best/ADDR.patch`
  restores it.
- `tools/decomp evidence ADDR --decomp-range A:B` prints decompilation lines A-B.
- Source files use tabs. An edit that does not apply is the usual lost attempt, so every
  edit asserts its anchor before the build runs, and edit and bc are `&&`-chained.

## The attempt cycle (one shell call per attempt)

    python3 - <<'EOF' && clang-format -i --lines=A:B FILE && tools/decomp bc ADDR 2>&1 | grep -vE 'warning C4|LNK4'
    p='FILE'; s=open(p).read()
    old="""EXACT LINES INCLUDING TABS"""
    new="""REPLACEMENT"""
    assert s.count(old)==1, s.count(old)
    open(p,'w').write(s.replace(old,new))
    EOF

One source-level idea per cycle. Then `tools/decomp bc ADDR --hunk N` only for regions
whose `-a +b` counts changed, and `sed -n A,Bp FILE` only for lines you will edit next.
If the score dropped, restore the best patch in the same call as the next edit. Never
read the compact or raw diff files or anything under tool-results/: the pack, the index
and two or three regions are enough to choose the next idea. Batch independent reads.

## Scope rules

- Edit only the target function's .cpp and a header included by that one .cpp. Never
  change a header or macro shared by other translation units: in the pilot such edits
  regressed untouched functions and cost 10 to 40 minutes at validate. A type the
  function needs goes file-local; note the header idea under OPEN.
- Stop the batch when `bc` prints a `stall:` line (three attempts without a half-point
  gain) or at the attempt count the assignment gives. Leave the tree holding the best model.
- Never run validate, record, commit or push unless the assignment says REPAIR.

## Handoff (end of every batch, under 40 lines, quoted heredoc, no python)

    mkdir -p build/decomp-cache/handoff && cat > build/decomp-cache/handoff/ADDR.md <<'EOF'
    # ADDR NAME - batch N (MODE)
    BEST: X% at attempt K (baseline B%). Tree holds best: yes. Patch: build/decomp-cache/best/ADDR.patch
    ABI: (coverage only) convention, parameters, return, frame size
    TRIED (attempt score idea, one line each, earlier batches carried forward):
     1 B% baseline
    OPEN (priority order; region numbers from the attempt-K index, with retail addresses):
     1. regions R (0xADDR): hypothesis; the exact source form to build
    LINES: FILE:A-B
    TOOL: exact text of any tool error, else none
    EOF

Return only three lines: `BEST: X% attempt K`, `ATTEMPTS: a-b`, `HANDOFF: written` (or the
tool error). The orchestrator reads the file itself and runs validate, record and commit.

## Repair (only when the assignment says REPAIR; at most one validate run)

The assignment quotes the `validation failed:` lines. `untouched function score regressed`
means a shared header or macro changed: `git checkout HEAD -- FILE`, keep the type
file-local. `annotation is untagged` means add the tag it names. A `source debt` line
names a lint rule to fix in the target only. Then one call:
`git add src tools/Resources/functions_map.txt && tools/decomp validate --mode MODE --target ADDR --staged 2>&1 | grep -vE 'warning C4|LNK4' | tail -25`
Return `VALIDATE: passed` or the `validation failed:` lines verbatim, then stop.
