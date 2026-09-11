---
name: decomp-expert
description: Run one batch (about four bc attempts), one cleanup or one finish repair of a Toy Story 2 reconstruction campaign. The driver (tools/decomp campaigns run) supplies the target and the attempt range; this file is the exact command sequence.
---

# Decomp expert

You are the writer. This text holds the mechanics; AGENTS.md holds the rules (Claude Code
loads it through CLAUDE.md; Codex reads it directly); the last message is your assignment.
Never read AGENTS.md, CLAUDE.md, .agents/ or tools/*.py, and never run --help: your context
has them or does not need them. If a tool prints something you cannot act on, quote it in
the handoff TOOL line and stop.

## Tool facts

- `tools/decomp bc ADDR` builds, compares, logs one attempt, saves the diff and prints
  `attempt K/N raw X% (+d) best Y% (attempt k)` plus a region index
  (`N  0xADDR  -a +b  FILE:LINES`). It is the only command that consumes an attempt.
- Then `bc` prints up to two regions changed since the last diff, each with its source
  lines (tabs kept, ready for an edit anchor); `bc ADDR --hunk N` reads a region a `not
  shown:` line names, without a build. A driver assignment already holds the pack.
- A failed build prints the errors and `bc: no attempt logged`; fix it and run the cycle
  again. Never pipe `bc` into `head` or `tail`: a closed pipe aborts the build.
- A `header side effect:` line means your header edit moved an untouched function; make the
  type file-local in the next attempt.
- A `lint:` line is a new finding validate will reject: fix the declared type or field. A
  legacy cast whose shared declaration cannot change stays inline; note the declaration under
  OPEN (the function stays PROVISIONAL). Never move a cast into a local or macro to pass the
  lint. An `advice:` line names a literal to replace with its name; validate ignores it.
- The best tree (a cleanup tie replaces it) is `build/decomp-cache/best/ADDR.patch`;
  `git checkout -- src && git apply build/decomp-cache/best/ADDR.patch` restores it.
  `tools/decomp evidence ADDR --decomp-range A:B` prints decompilation lines A-B.
- Source files use tabs. Every edit asserts its anchor (a failed assert logs no attempt).
  Keep the build on the line after `EOF`, never joined with `&&`: a permission rule denies it.
- A sandbox (Codex) lets you write the tree, build/, the git directory and the Wine prefix;
  quote any other write error in the TOOL line and never work around it.

## The attempt cycle (one shell call per attempt)

    python3 - <<'EOF'
    p='FILE'; s=open(p).read()
    old="""EXACT LINES INCLUDING TABS"""
    new="""REPLACEMENT"""
    assert s.count(old)==1, s.count(old)
    open(p,'w').write(s.replace(old,new))
    EOF
    clang-format -i --lines=A:B FILE && tools/decomp bc ADDR 2>&1 | grep -vE 'warning C4|LNK4'

One source-level idea per cycle, written from the changed regions and the pack; `sed -n
A,Bp FILE` only for lines outside the printed spans. If the score dropped, put the restore
command of the best patch on its own line above `python3 - <<'EOF'` in the next cycle.
Never read the diff files or tool-results/. Batch independent reads.

## Scope and source rules

- Name each constant you add or touch: the name the file already has, else a `const`, enum
  or `#define` near its use. Naming does not change the generated code.
- Give each state or sub-block you touch one section comment.
- A form that exists only to steer codegen gets `// fakematch: REASON` on the line above.
- Edit only the target's .cpp and a header only that .cpp includes. A shared header or
  macro edit regresses untouched functions (10 to 40 minutes lost at validate): make the
  type file-local and note the header idea under OPEN.
- Stop the batch when `bc` prints a `stall:` line (three attempts without a half-point
  gain) or at the attempt count the assignment gives. Leave the tree holding the best model.
- Never run validate, record, commit or push: the driver runs `campaigns finish`.

## Handoff (end of every batch, under 40 lines)

Your final message is the handoff, with no tool call after it: the driver saves it, starts
the next pack with it and puts TRIED and OPEN in the commit message (else save it with
`tools/decomp handoff ADDR`).

    # ADDR NAME - batch N (MODE)
    BEST: X% at attempt K (baseline B%). Tree holds best: yes. Patch: build/decomp-cache/best/ADDR.patch
    ABI: (coverage only) convention, parameters, return, frame size
    TRIED (attempt score idea, one line each, earlier batches carried forward):
     1 B% baseline
    OPEN (priority order; region numbers from the attempt-K index, with retail addresses):
     1. regions R (0xADDR): hypothesis; the exact source form to build
    LINES: FILE:A-B
    TOOL: exact text of any tool error, else none

## Cleanup (only when the assignment says CLEANUP)

The tree holds the best model; the assignment lists the work. In one edit, name the listed
literals (scope rules above), give each listed block opener one section comment and tag
codegen-only forms `// fakematch: REASON`; change no code, field, type or header. Run the bc
line once. If the score dropped, restore the best patch, redo half of the edits and run bc
once more; stop at the `budget:` line. Return 1-5 plain lines under 100 characters saying
what changed and any field, type or helper idea for a later batch, then stop.

## Repair (only when the assignment says REPAIR)

The assignment quotes the problems `campaigns finish` reported; fix only those, in the
target's source. `untouched function score regressed` means a shared header or macro
changed: `git checkout HEAD -- FILE` and keep the type file-local. `annotation is untagged`
means add the tag it names. A `source debt` or `lint:` line names a rule to fix in the
target. Run the bc line of the cycle once and confirm the score holds and no `lint:` line
remains; the driver runs finish again. Return `REPAIR: X%, lint clean` or the remaining
problem lines verbatim, then stop.
