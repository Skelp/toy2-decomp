# Source models

This file records source models that a completed campaign ruled out. Search it
with `tools/decomp notes QUERY --source models` before a new source trial.

Each campaign entry names its targets and the tested model. A new result must
include the compiled score or the evidence that rejected the model.

## Historical import | SoftwareRenderer rasterizer edge walkers

- Targets: `0x00454D30`, `0x0046D7B0`, `0x00471B30`, and `0x00472E70`.
- Ruled out: An array-backed five-interpolant edge model scored 9.17 percent.
- Ruled out: A scalar-unrolled five-interpolant model scored 8.77 percent.
- Ruled out: The adjacent 555 color-offset model scored 9.41 percent.
- Needed evidence: Find the retail edge expansion and branch-local declaration order.

## Historical import | SoftwareRenderer blend-75 rasterizers

- Targets: `0x00465180` and `0x00474C80`.
- Confirmed: The dispatch slots use blend-75 behavior from adjacent formats.
- Ruled out: The analogue pilots scored 19.98 percent and 15.09 percent.
- Score limit: Stale map gaps limited the raw scores for both pilots.

## 2026-09-08 | BarnEncounter | `0x00424490`

- Mode: refinement.
- Ruled out: Direct indexed minion scale and camera accesses with narrower
  pointer lifetimes scored 48.20 percent. The baseline scored 50.61 percent.
- Ruled out: Direct indexed minion scale updates with camera pointers retained
  scored 45.43 percent.
- Needed evidence: Determine the source form for the 0x88-byte retail stack
  frame and the cold camera-selection block at `0x0042559E` through
  `0x0042567F`.
- Audit note: The campaign recorder rejected the result because the old
  experiment command overwrote the immutable baseline. The abort records retain
  the measured time. Campaign `ae64c1df-a577-48a0-9390-f745434f4d85` retains
  the first-score time.

## 2026-09-08 | BarnEncounter camera selection | `0x00424490`

- Retained: Six attachment slots, stable boss-position snapshots, and
  `actorIndex - 1` indexing increased the score from 50.61 to 55.02 percent.
- Ruled out: A full shared `dx`, `dz`, and `d0` camera-local model reduced the
  retained score to 54.22 percent.
- Retained: Explicit encounter-state bounds, `timer <= 0`, and `x`, `y`, `z`
  target-store order increased the final score to 55.27 percent.
