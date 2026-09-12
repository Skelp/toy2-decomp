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

<!-- campaign-id: 89da2797-422a-45ec-8921-75e1b4b7623b -->
## 2026-09-12 | SoftwareRenderer | 0x00467080

- Mode: refinement.
- Ruled out: 2 55.57% CLIP_POLYGON_ROW_RANGE: load v0.y into bottomY and copy to topY (was topY first). Gives retail's exact instruction order in region 2 but registers ecx/edx where retail has edi/ecx; reverted
- Ruled out: 3 43.52% hoist the span locals (leftX, rightX, width, u..blue, five steps, pixelCount, pixel) to function scope via DECLARE_LIT_TEXTURED_SPAN_LOCALS so both row loops share one home each; large regression, also perturbed the edge loops; reverted
- Ruled out: 4 55.70% declare edgeXStep after edgeRedStep in RASTERIZE_LIT_EDGE_WITH_END (retail's xStep slot 0x30 sits above the step slots 0x1c-0x2c); no codegen change at all - declaration order is inert for this macro; reverted
- Result note: no attempt raised the score

<!-- campaign-id: da75d232-6cec-44b1-a07b-4e1281bfea8c -->
## 2026-09-12 | Collision | 0x00486520

- Mode: coverage.
- Ruled out: per-face ground/ceiling resolve with the workspace slot loop, 16.96% at attempt 5
- Result note: second attempt on this body; coverage stalled at 16.96% of a 4,979-byte function (needs 25% of the ceiling)

## AudioManager::LoadSoundEffect 0x0047E5B0 — magic-pointer finding is not fixable (2026-09-12)

Ruled out: changing the declared type of `g_loopingSoundOwners`.

The finding is the `(void*)1` sentinel this function stores into
`g_loopingSoundOwners[index * 6 + i]` to mark a sound effect slot that is in use but
has no actor owner. The declared type `void* [768]` is already correct: the array is
compared against real owner pointers (`g_loopingSoundOwners[chan[0]] == owner`, and
the same at the looping scan) and is aliased as `void**`. So the rule's remedy, to
correct the declared type, does not apply.

The two routes both make the source worse:

- Naming the sentinel keeps the cast in the named constant's own initialiser, so the
  rule fires again; `magic-pointer` is not in SUPPRESSIBLE_RULES, so no comment can
  accept it either.
- Declaring the array as an integer type forces a cast at every comparison and
  assignment site, about twenty of them, in functions that match exactly today.

So 0x0047E5B0 keeps one legacy error and stays out of terminal status. The same
sentinel reaches PlaySoundBuffer 0x0047DE50, whose magic-pointer error has the same
cause and the same verdict.

## Moving a constant declaration can move a stack frame

TarmacTrouble.cpp, campaign `8f774ef`. The unnamed-constant fixes needed
LINK_SCALE_ONE and BUTTON_LINK_FIRST above their earliest use, which sat 400
lines above the declarations. Moving the whole 33-line constants block to the
head of the namespace built and read well, and it cost `Toy2::TarmacTrouble::Init`
(0x0042E600) 6.73 points, 94.23% to 87.50%. The diff is only stack slots,
`[esp + 0x30]` against `[esp + 0x40]`, in a function whose text did not change.
A declaration emits no code, so VC6 laid out that frame differently for a
reason the source does not state.

Method: move the least source possible. Put the one or two declarations a fix
needs immediately above the earliest function that uses them and leave the
rest of the block alone. The same move in SoftwareRenderer.cpp (`34fa172`) and
AudioManager.cpp (`6a2c5c3`) changed no score, so the effect is not general;
check every function of the file after any declaration move.

## SoftwareRenderer.cpp macros do not cross a retail band

Measured on `f7feb1a`, before planning the renderer split. The file defines 34
object- and function-like macros. Counting users transitively, so a macro used
only inside another macro's body inherits that macro's functions, every macro
has all of its users inside one of the three address bands that hold 94 of the
123 functions:

- 32 macros belong to 0x00454D30-0x00477EB0, the rasterizer band.
- TOY2_BUILD_COLOUR_SCALE_TABLE belongs to 0x004BC900-0x004C1FC0.
- NU_FMIN sits outside all three and is already `#undef`ed beside its one use.

No macro has users in two bands, so a split needs no `*Internal.h` for the
macros: each band takes its own macros with it. Count the users transitively or
the answer is wrong: a scan that skips `#define` lines reports
BLEND25_LIT_TEXEL_555 as dead, and it is used by WRITE_BLEND25_TEXEL_555.

## How to split a file that holds several retail bands

Method of the five SoftwareRenderer campaigns (`01eb825` to `004a1c8`), which
took the file from 11,666 lines and 15 retail runs to 5,706 lines and one
namespace. Repeat it for the next mixed file.

1. `tools/decomp structure FILE` names the bands. A band is one contiguous run
   of the retail address order, so it is one retail object.
2. Move whole blocks, comments included, and **keep the order the old file
   held**. Ordering the new file by address instead changes which copy of a
   duplicated block lint calls the owner, and every baseline row of the band
   goes stale for no gain. Put data blocks before code blocks, because C++ has
   to see a declaration before its use, and the old file held its data at the
   top.
3. A global goes with the band that writes it. Count the users per band first;
   every SoftwareRenderer band except the back buffer owned all of its globals.
   A global that two bands read needs an `extern`, and `SoftwareRenderer.h`
   already held most of them.
4. A record that two bands need goes to `*Internal.h`. RenderCommand,
   ScanlineScratch and SoftwareRenderDispatchTable moved for that reason.
5. A **constant enum that two bands spell stays in both files.** The
   unnamed-constant rule resolves a name only inside the file that uses it, by
   design, so a shared header would hide every bare literal that still stands
   where one of those names belongs. Write the reason above the enum.
6. Macros: the note above proves none crosses a band, but a macro and its
   `#undef` sit around their user, so move all three together and put the
   `#undef` after the whole function, not at the first `\t}`.
7. A band that reaches into a second namespace takes that namespace with it.
   The closing brace of a namespace belongs to the last block before it, so the
   file a block leaves needs its brace back.
8. Build one function of the band, then `validate --mode structure --staged`.
   Layout deltas print as warnings; every one of the five campaigns changed no
   score.

## Retail source paths and the toy2.cpp span

The retail binary quotes nine source paths, and the addresses that reference
each one prove where that unit's code sits:

| retail unit | assert sites | our file |
| --- | --- | --- |
| `toy2\toy2.cpp` | 0x490AC7, 0x4984A3, 0x498573, 0x49BB3F | Toy2/Toy2.cpp |
| `toy2\Win95.cpp` | 0x4A64DE, 0x4A657D | Toy2/Win95.cpp |
| `toy2\direct6.cpp` | 0x412B8A, 0x412BBE | Toy2/Direct6.cpp |
| `toy2\modesel.cpp` | 0x4333DA | ModeSelect.cpp |
| `nu3d\objcore.c` | 0x4B3024 | Nu3D/ObjCore.cpp |
| `nu3d\world.c` | 0x4C41AA, 0x4C4285 | Nu3D/World.cpp |
| `nu3d\hobjload.c` | 0x4CA3EA, 0x4CA806 | Nu3D/HObjLoad.cpp |
| `nu3d\objload.c` | 13 sites, 0x4CB398 to 0x4CCA13 | Nu3D/ObjLoad.cpp |
| `nu3d\fmv.c` | 10 sites, 0x4DB67B to 0x4DBB64 | Nu3D/FMV.cpp |

Every site lands in the file whose name matches, so these nine placements are
confirmed, not guessed. 0x4984A3 is the exception: it sits 0x29 bytes past the
end of `SaveManager::SetLightShadowEffects`, whose retail body is 10 bytes, so
it belongs to an **unmapped** function in the gap 0x49847A-0x498550. Run
`tools/decomp discover` over that gap before using the site.

**Open question for the next Toy2.cpp campaign.** The four toy2.cpp sites span
0x4909E0 to 0x49BB3F. MSVC links one object as one contiguous block, so that
whole span should be retail toy2.cpp; we currently split it across fifteen
files, with Renderer, DevDraw and Renderer::Sprite holding 27 of its functions.
No other retail unit has a site inside the span, so nothing contradicts the
reading. Either those 27 functions are misfiled, or the retail link did not keep
object order. Decide that before moving anything, because the span holds about
44 KB of code. The three unmapped DevDraw functions inside it (0x490EE0,
0x491F20, 0x492F70) have to be mapped first, or the contiguity test runs on
incomplete data.
