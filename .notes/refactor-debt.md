# Refactor debt

Functions that match the retail machine code but still state byte offsets where
they should state names. The behavior is confirmed correct, so **do not revert
them**. Retyping is low risk precisely because the match pins every offset: if
a declared structure is right, the comparison stays at the same percentage.

Run `tools/decomp lint` for the current list. This file records the plan.

**Work the list in the order below.** `tools/decomp candidates --debt` ranks by
error count, which puts the largest item first. That is the wrong opening move.
Start with section 0: those are single-decision fixes with immediate feedback.
Bank them, then take on the design-heavy items.

## Why this debt exists

Machine-code similarity was measured every build cycle. Source plausibility was
measured nowhere. So an exact match became the only stopping signal, and several
functions reached 100% as transliterated decompiler output. `tools/decomp lint`
now supplies the missing signal.

## Current state

Sections 0, 1, and 4 are cleared, section 3 is mostly cleared, and section 2 is
partly done. What remains in sections 3 and 5 is **blocked on reconstruction,
not on naming**: those placeholder names sit in `STUB` signatures whose roles
only the finished body reveals. Do not rename them speculatively.

So there is no cheap debt left. Take a `STUB` or an unannotated leaf from
`tools/decomp candidates` instead of `--debt`. Note that this is how `c698aeb`
found its target: the top-ranked `STUB` also carried section 3 lint errors, so
reconstructing it cleared the debt as a side effect. That is the pattern to look
for — a debt item and a `STUB` that are the same function.

## 0. Start here: the single-decision fixes — DONE

Cleared in `a0968c2`, `62a2aa6`, and `8afe907`. `SortedPrimitive::fieldC` and
`field10` became `textureIndex` and `primitiveGroup`, the forwarding parameters
followed, and `D3DAppInfo::lpViewport` became `lpD3DViewport` to match the name
the retail strings state.

Each of these was one rename in one function, already at 100%. Rename, build,
confirm the score holds, commit. One commit each. No design decision is
involved, so do not deliberate.

The `SortedPrimitive` record is the root of most of them. Its own fields are
named `field10` and `fieldC`, and every submitter that forwards to
`SubmitSortedTriangle` inherits those names. Fix the record's field names first
and the parameters follow.

The roles are already provable from the call sites in `SoftwareRenderer.cpp`:

- `SubmitQuad` calls `SubmitSortedTriangle(renderFlags, 0, textureIndex, ...)`,
  so the **third** parameter (`fieldC`, record `+0xC`) carries a texture index.
- Every indexed submitter calls
  `SubmitSortedTriangle(renderFlags, field10, 0, ...)` and receives its
  `field10` from its own caller, so the **second** parameter (record `+0x10`)
  is the value the indexed path forwards while the quad path zeroes it.

Name the record fields for those two roles, then propagate. Confirm each
function stays at its current percentage.

| Address | Function | Errors |
| --- | --- | --- |
| 0x004B5FB0 | `SoftwareRenderer::SubmitTriangleList` | 1 |
| 0x004B6140 | `SoftwareRenderer::SubmitTriangleStripRaw` | 1 |
| 0x00490410 | `SoftwareRenderer::UnkFunc67` | 2 |
| 0x004B5E40 | `SoftwareRenderer::SubmitSortedTriangle` | 2 |

Deferred on purpose: the `STUB`s that carry `fieldNN` parameters
(`UnkFunc35`, `UnkFunc34`, `UnkFunc8`, `UnkFunc22`,
`Toy2::Animation::EvaluateClip`). Their signatures are guesses until the bodies
are reconstructed, so fix those names as part of reconstructing each one. Do not
rename them speculatively now.

## 1. The `drawb` / `drawtranb` draw buffers — DONE

Cleared in `90315ca` (the two structures and the twelve offset casts) and
`ddaf204` (the shared vertex pool and the offset assertions). The plain-structure
form was the right call; no template was needed.

The rest of this section is kept because the layout evidence is still the
reference for anyone touching these two types.

The retail binary **names this structure itself**. See
`.notes/original-names.md`, and the log strings in `Renderer/Renderer.cpp`
around the two flush functions:

```text
(LPVOID) (drawb->Vertice[i]), drawb->VerticeCount[i],
(LPWORD) & (drawb->Index[i]), drawb->IndexCount[i]
```

Affected: `DevDraw::FlushDrawBufferSlot` (0x00490470),
`DevDraw::FlushTransparentDrawBufferSlot` (0x004905C0). Twelve byte-offset
casts between them.

Offsets observed, opaque buffer at `g_unk500A10`, transparent at
`g_unk500A14`:

| Member | Offset | Element | Note |
| --- | --- | --- | --- |
| `Vertice[]` | +0x00000 | `void*` | vertex data per slot |
| `VerticeCount[]` | +0x00104 | `int16_t` | 0x104 / 4 = **65 slots** |
| `Index[]` | +0x00186 | `WORD` | slot stride 2000 opaque, 12000 transparent |
| `IndexCount[]` | +0x1FD56 opaque, +0x5DD86 transparent | `int16_t` | |

The arithmetic closes exactly, which confirms the layout:

- opaque: (0x1FD56 - 0x186) / 2000 = **65** slots, matching 0x104 / 4.
- transparent: (0x5DD86 - 0x186) / 12000 = **32** slots, matching the
  `slot < 0x20` guard already present in the transparent flush.

### The vertex pool is now declared

The two draw-buffer flush functions were the whole of this item, and both were
already at 100%. What the table below did not cover is the shared vertex pool
that follows the slot arrays. `ddaf204` declares it:

```cpp
Nu3D::VertexTL VerticePool[16384];   // +0x1FDD8
int16_t VerticePoolCount;            // +0x9FDD8
```

The function at 0x00490D10 proves both the stride and the capacity. It appends
two vertices per call with `LEA edi,[edx + esi*1 + 0x1FDD8]` after
`esi = count << 5`, so the element stride is 0x20, which is exactly
`sizeof(Nu3D::VertexTL)`. Its `cmp cx, 0x3FFE` guard keeps room for that pair,
which makes the highest usable index 0x3FFF and the capacity 16384. The
arithmetic closes exactly: `(0x9FDD8 - 0x1FDD8) / 0x20 = 16384`.

Offset assertions now pin every member of both structures, so a later layout
mistake fails the build instead of silently lowering a score.

### Do not re-derive these two facts

A previous session spent its whole budget re-deriving the following and produced
nothing. Both are settled.

1. **The arrays hold 65 entries, but `DevDraw::DrawSlots` iterates 64
   (`i < 0x40`). That is expected.** Retail allocated one spare slot. It is not
   evidence that the layout is wrong, and it does not imply a header field,
   padding, or a different structure. Leave the arrays at 65 and the loop at 64.
2. **The transparent buffer shares the 65-entry `Vertice` and `VerticeCount`
   arrays but has only 32 index slots.** Its `slot < 0x20` guard is what keeps
   the 64-iteration loop inside those 32 entries. The oversized vertex arrays
   are intentional, not a contradiction.

### Use this form

**Declare two plain structures.** Do not use a template. A template is
defensible C++ but it buys nothing here, and choosing between the two forms is
not worth one minute of deliberation.

| | opaque (`drawb`) | transparent (`drawtranb`) |
| --- | --- | --- |
| `Vertice[]` | 65 | 65 |
| `VerticeCount[]` | 65 | 65 |
| `Index[N][stride]` | N=65, stride 1000 `WORD` | N=32, stride 6000 `WORD` |
| `IndexCount[]` | 65 | 32 |

The strides are given in `WORD` elements. In bytes they are the 2000 and 12000
seen in the current source.

### Order of work

Write the structures first, then build, then compare. Do not settle the design
by thinking about it. Both functions are at 100% now, so a regression is
immediate and unambiguous proof that the layout is wrong, and that answer
arrives in about 45 seconds. If it regresses, `git restore` and try the other
form.

Then write `drawb->VerticeCount[i]` and confirm both functions hold at 100%.

## 2. `d3dappi` — the Direct3D application structure — DONE

`8afe907` retyped `lpViewport` to `lpD3DViewport`. The structure now declares
all 17 members that the retail strings name.

`.notes/original-names.md` lists 17 members recovered from the retail strings:
`lpDD`, `lpFrontBuffer`, `lpBackBuffer`, `lpZBuffer`, `lpD3D`, `lpD3DDevice`,
`lpD3DViewport`, `lpTexture`, `lpTextureSurf`, `lpTextureMat`,
`lpTextureMatHandle`, `lpGroundMat`, `lpGroundMatHandle`, `lpSkyMat`,
`lpSkyMatHandle`, `TextureHandle`, `hwnd`.

Seven unused `unkInt` members previously followed `lpSkyMatHandle`. Ghidra has
no retail cross-references to their addresses. They are now part of the unknown
padding before `lpD3D`. Offset assertions pin all named members after this
padding. Do not split this padding without new cross-reference evidence.

## 3. `SoftwareRenderer` placeholder parameters

`fieldNN` reached function **signatures**, where it is never acceptable: the
caller already proves each argument's role.

- `UnkFunc22` — `param5`, `param6`
- `UnkFunc8` — `param1`, `param2`
- `Toy2::Animation` — `arg2`, `arg3`, `arg4`

Done: `SubmitSortedTriangle` (`field10`, `fieldC`) in `a0968c2`, `UnkFunc67`
(`param1`, `param2` -> `x`, `y`) in `62a2aa6`, and `UnkFunc35`, `UnkFunc34`,
`UnkFunc29` in `c698aeb`.

**The method for the `c698aeb` group is reusable, so prefer it over guessing.**
`field80` and `field88` were already declared correctly on a *sibling*:
`UnkFunc22` receives the same two values and calls them `texData` and
`renderState`. Reconstructing `UnkFunc35` then confirmed both, because it
compares +0x80 against NULL to select an untextured rasterizer and bit-tests
+0x88 against the `Renderer::RENDER_ALPHA_*` flags that `RenderType.h` already
names. So: **before you rename a placeholder, grep the other functions that
receive the same value.** The name is often already recovered somewhere else in
the repository, which costs one grep instead of an investigation.

`field94` became `useAlternateSpans` a different way. `UnkFunc35` *ignores* that
argument, so its role is invisible there; the sibling `UnkFunc34` branches on it
to swap in alternate rasterizers. When a parameter is dead in the function you
are reconstructing, the caller and the sibling supply the role.

Fix the rest when you reconstruct the callee, which is what reveals each role.

`UnkFunc8` (0x0047D210) is a special case worth knowing before you spend time
on it. Its body **ignores both parameters**: every `%ebp` reference in
0x0047D210-0x0047D4D0 is negative, so nothing reads the argument slots. Its one
caller passes `g_unk839278`, which holds 0x27F or 0x3FF. `ddaf204` corrected the
first parameter's *type* to `int32_t` on that evidence, but left both names
alone, because a name needs a role and the retail body supplies none. Settle
these names while reconstructing the body, not before.

## 4. `Toy2::UpdateD3DState` names the arithmetic, not the role — DONE

Cleared in `ddaf204`. The `Minus1` pair were indeed clip limits, and the
`Times1024Minus1` value was the same limit in 1/1024 units.

Two pieces of evidence settled every name. Keep them if a later session
questions the rename:

1. **`InitSoftWindow` (0x0047CBA0) names itself** in its own
   `"InitSoftWindow(%i,%i)"` log string, and it writes the same six globals.
   It centers a window of the requested size in the destination rectangle:
   `left = (destWidth - windowWidth) / 2`, `right = left + windowWidth - 1`,
   and the same for the vertical pair. `UpdateD3DState` writes the
   full-window case, where left and top are 0, which is why its stores looked
   like bare arithmetic on the width.
2. **The helper at 0x00490D10** clamps a point's x field up to `0x008828E0`
   and down to `0x008828C4`, and its y field up to `0x008828E4` and down to
   `0x008828CC`. Four edges, one rectangle.

The `*Fixed` pair holds the left and right edges in 1/1024 units.
`InitSoftWindow` computes `right * 1024 + 1023`, which for a zero left edge is
exactly the `width * 1024 - 1` that `UpdateD3DState` stores.

The prefix is `g_screenClip*`, **not** `g_clip*`: `SoftwareRenderer` already
owns a different clip rectangle at 0x00B7FBBC, and the shorter name collides.

| Address | Was | Now |
| --- | --- | --- |
| 0x008828E0 | `g_unk8828E0` | `g_screenClipLeft` |
| 0x008828C4 | `g_destRectWidthMinus1` | `g_screenClipRight` |
| 0x008828E4 | `g_unk8828E4` | `g_screenClipTop` |
| 0x008828CC | `g_destRectHeightMinus1` | `g_screenClipBottom` |
| 0x008828BC | `g_unk8828BC` | `g_screenClipLeftFixed` |
| 0x00882784 | `g_destRectWidthTimes1024Minus1` | `g_screenClipRightFixed` |
| 0x00882798 | `g_destRectWidthCopy` | `g_softWindowWidth` |
| 0x008828E8 | `g_destRectHeightCopy` | `g_softWindowHeight` |
| 0x008828D8 | `g_destRectHalfWidthCopy` | `g_softWindowHalfWidth` |

The two lint errors in the body are also gone. `g_unk839278` is `int32_t`, not
a pointer: it holds 0x27F and 0x3FF, and its only reader passes it to
`SoftwareRenderer::UnkFunc8`, which ignores the argument entirely. The
`+0x9fdd8` cast became `g_drawBuffer->VerticePoolCount`; see item 1.

## 5. `RenderCommand` metadata fields

`SoftwareRenderer::RenderCommand` holds `field80`, `field88`, `field90`,
`field94`, `field98`. These stay warnings, not errors, because the layout is
still being recovered. Name them when `UnkFunc29` and `UnkFunc35` are
reconstructed. Do not leave them once both sides are known.

## Also worth doing

The `.notes/original-names.md` translation-unit table proves ownership for 21
functions. The current layout already disagrees in at least one place:
`direct6.cpp` functions sit in `src/Toy2/Toy2.cpp`. Do not reorganize the tree
for its own sake. Use the table when a function's destination is genuinely in
question.
