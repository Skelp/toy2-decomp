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
- Superseded: The analogue pilots scored 19.98 percent and 15.09 percent
  against stale map gaps of 7,936 and 5,824 bytes. Ceiling-relative, those
  scores are about 56 percent and 31 percent of the 2,824-byte and 2,813-byte
  bodies. The 2026-09-08 map repair set both ceilings above 99 percent, so the
  analogue model is not ruled out. Retry it from the current
  `RasterizeBlend75TexturedPolygon555` form.

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

<!-- campaign-id: 15852ab3-9bef-4b3a-8187-c4afb63f97f4 -->
## 2026-09-09 | Toy2::TarmacTrouble | 0x0042DCB0

- Mode: refinement.
- Ruled out: Inline second-loop position arguments and use one shared rotation call. This model reduced similarity to 64.96 percent.
- Ruled out: Cache first-loop sine and cosine values before coordinate transforms. This model reduced similarity to 49.13 percent.

<!-- campaign-id: 1ed08bbe-172c-42a1-aea4-3a060f2a4ae0 -->
## 2026-09-10 | Toy2::Buzz | 0x004359D0

- Mode: refinement.
- Ruled out: Move each X and Z actor-coordinate load into both sign branches to match the retail load and branch order. This model was not scored because the first-score gate expired.

<!-- campaign-id: 8df14259-cc8b-4d29-9319-0f5902730299 -->
## 2026-09-10 | Toy2 | 0x0049C420

- Mode: refinement.
- Ruled out: Cache g_controlConfigFirstVisibleRow once before the visible-row loop. Use it with a separate row index and ControlConfigEntry cursor to calculate each Y position. The score increased from 63.41 percent to 64.74 percent. The typed-data oracle rejected relocation and data-section regressions.

<!-- campaign-id: e41fef60-20c9-4ff2-82f0-b731859d79e3 -->
## 2026-09-11 | Collectables | 0x004A0F80

- Mode: refinement.
- Ruled out: 2 50.66% signed `for (i = 0; i < recordCount; i++)` with `buzzY = (pos.y >> 5) - 0xE6` moved up, distance sum Z+Y+X. The loop re-reads recordCount each pass, so no down-counter. The buzz load order (y, recordData, x, z) matched retail.
- Ruled out: 3 51.97% same as 2, but with `int32_t pickupCount = recordCount; for (i = 0; i < pickupCount; ...)`. Gives the retail `test/jle` + `dec/jne`, but buzz loads moved to z, recordData, y, x.
- Ruled out: 4 36.92% cached `g_respawningGadgetPickup` in a local pointer in cases 6/7/8. Large misalignment; stall line printed.
- Result note: no attempt raised the score

<!-- campaign-id: 0b624c1f-8cdb-4d3f-a153-3729ee998f8c -->
## 2026-09-11 | Collision | 0x0048D530

- Mode: refinement.
- Ruled out: 2 56.01% cached branch gets its own zero-initialized `cacheLocalX/Y/Z`; the uncached branch declares block-local `localX/Y/Z`. No change: the allocator already treats the two branches separately.
- Ruled out: 3 56.01% rotated rows written as m2x*relZ + m1x*relY + m0x*relX. No change: the compiler puts the terms in a fixed order either way.
- Ruled out: 4 56.01% drop the `z` local in the moving-mesh loop and read `position->z` directly, plus a named `DISTANCE_SHIFT`. No change: the compiler already reloads z.
- Result note: no attempt raised the score

<!-- campaign-id: 08b99b1a-0011-4bdf-809f-430b9981d669 -->
## 2026-09-11 | Toy2 | 0x00433700

- Mode: refinement.
- Ruled out: 2 56.08% dropped the `riderIndex` local and used `g_objects[g_forcedFacingActive - 1]` and `g_forcedFacingActive - 1` directly. The object address is now computed like retail (`lea eax,[ecx+ecx*4]`, base g_objects-40), but the first loop's registers changed (buzz moved from ebx to ecx), so the score fell.
- Ruled out: 3 59.13% ambient check on `data[riderIndex].y` instead of `.x`, plus `targetY` written inline. Same score. The retail displacement `[eax+ecx*4-4]` with ecx=active*3 decodes to data[active-1].y, so `.y` is the correct field. The inline `targetY` gave the same code as the local.
- Ruled out: 4 58.08% rebuilt the path-bounds logic as `if (progress==0 && cur>target) {dec; ps=0} else { if (==segLen) {cur<count-2 ? adv : ps=moving} else if (>segLen) {cur<count-2 ? adv : clamp} if (<0) {...} }`. The compiler did not share the step-back block or the advance block, so the score fell.
- Result note: no attempt raised the score

<!-- campaign-id: 8d9a90c5-daaa-453e-8546-c84958c77c9b -->
## 2026-09-11 | SoftwareRenderer | 0x00461D20

- Mode: refinement.
- Ruled out: 2 40.32% texel index `((v >> 8) & k_upperByteMask) + (rowU >> k_fixedPointShift)` in all 8 loops: compiles to sar/and, not the retail `xor edx,edx; mov dh,byte [v+2]`; keep `((v >> 16) & 0xFF) * 0x100`
- Ruled out: 3 66.04% subtractive loops: destinationPixel and sourcePixel as uint16_t. The offset-free loop now loads `mov dx,[ecx]` with no xor, as retail does, but the registers moved
- Ruled out: 4 67.45% `uint16_t useSubtractive = flags & SOFTWARE_RENDER_SUBTRACTIVE; if (useSubtractive != 0)`: the compiler folded it back to `test dh,0x10`, so the code did not change. Stall.
- Result note: no attempt raised the score

<!-- campaign-id: 964bc444-ca5f-4e28-9de3-77817090b5d1 -->
## 2026-09-11 | Collision | 0x00486520

- Mode: coverage.
- Ruled out: per-face ground/ceiling resolve loop over the workspace slot, 306 lines, 6.38%
- Result note: coverage stalled at 6.38% of a 4,979-byte body (needs 25% of the ceiling); the ground/ceiling resolve loop was not modelled
