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

<!-- campaign-id: 7d41f21f-c66d-4fc6-93b3-ce84d0e7d70a -->
## 2026-09-08 | SoftwareRenderer | 0x00465180

- Mode: coverage.
- Ruled out: Complete 555 Blend75 control skeleton with exclusive edge walks, separate keyed and unkeyed loops, 565 key 0x07C0, and 565 blend masks scored 19.98 percent.
- Result note: The corrected map confirms a near-100-percent ceiling. Refine 0x0045B0B0's shared edge and declaration source form before retrying the rasterizer family.

<!-- campaign-id: 541acdac-54f0-45ed-902d-f0c121ee6ebe -->
## 2026-09-08 | SoftwareRenderer | 0x0045B0B0

- Mode: refinement.
- Ruled out: Target-local branch-directional two-interpolant edge macro with direct scalar arms and quad-first topology scored 41.24 percent.
- Ruled out: The same branch-directional model with topY declared before bottomY scored 43.49 percent.
- Result note: The edge topology is supported, but the original declaration lifetimes and 0x20 stack-frame layout remain unresolved.

<!-- campaign-id: 43affac8-3427-41b1-a453-197c301263a2 -->
## 2026-09-08 | SoftwareRenderer | 0x0045B0B0

- Mode: refinement.
- Ruled out: The target-local branch-directional edge model with narrow first-use edge locals, branch-local edgeHeight, two-phase clear/draw locals, quad-first ordering, and topY-first declaration scored 42.34 percent.
- Ruled out: Adding explicit destination loads and retail blend-term evaluation order in both pixel loops left the score at 42.34 percent.
- Result note: The remaining source form must reproduce the retail edge-region stack and register allocation despite the confirmed seed copies and 0x20 frame.

<!-- campaign-id: cee5105c-aa4c-4550-b3b8-8854a0f86e47 -->
## 2026-09-08 | SoftwareRenderer | 0x00461890

- Mode: coverage.
- Ruled out: Complete RGB565 clipped textured-rectangle rasterizer using vertices 0 and 2, inclusive dimensions, fixed-point UV interpolation, transparent texel 0x07C0, odd-scanline skipping, and additive/subtractive/50-percent/opaque flag branches.

<!-- campaign-id: aefac452-a232-4536-9e7c-70c5b145a85a -->
## 2026-09-08 | SoftwareRenderer | 0x004617D0

- Mode: coverage.
- Ruled out: Complete 16-bit nearest-neighbor scaled texture blitter with nine cdecl integer arguments, inclusive destination bounds, signed fixed-point U/V steps, 256-pixel texture rows, and nested do-while loops.

<!-- campaign-id: 140509a6-f216-4165-97d8-cf996eec87cf -->
## 2026-09-08 | Software renderer colour-offset rasterizers | 0x00469900

- Mode: coverage.
- Ruled out: Natural inline five-interpolant RGB565 colour-offset blend-25 rasterizer: clone RasterizeBlend25TexturedPolygon565’s min/max clipping, polygon edge topology, and keyed/unkeyed span loops; expand each edge and span across u, v, blue, green, and red; shade sampled texels through g_redRampLow/g_greenRampLow/g_blueRampLow using the interpolated offsets; add the 75-percent destination contribution with masks 0x7BEF and 0x39E7; preserve transparent texel 0x07C0.

<!-- campaign-id: 79d8b101-0b81-4826-af94-3f506b4709e2 -->
## 2026-09-08 | Input mapping table | 0x004ED398

- Mode: data.
- Ruled out: Change the g_inputMapping[25].name initializer even though the retail and build targets both contain the bytes for "(".
- Ruled out: Add named character storage or another "(" literal only to alter string pooling and duplicate-match order, without retail source evidence.
