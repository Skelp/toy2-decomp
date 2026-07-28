# Codegen index

Read this file. Do not read the detail files end to end.

Each row is a symptom you can see in a `tools/decomp compare --verbose` diff,
and the rule it implies. Match your diff against a symptom first, then open
only that one section, by its bracketed ID:

- **FIX-nn** in `codegen-rules.md` — a source change reproduces retail.
- **CAP-nn** in `codegen-caps.md` — no source form fixes it. Stop and accept it.
- **TOOL-nn** in `reccmp-mechanics.md` — reccmp, annotation, or importer behavior.

When you hit a CAP, record the address in `caps-registry.tsv` so no later
session re-derives the same conclusion.

**Before you name a structure, a field, or a global, read
`original-names.md`.** The retail binary quotes the developers' own expressions
in its assert text, so many names are already recovered. An exact machine-code
match that still states byte offsets is a transliteration, not a
reconstruction. `tools/decomp lint` reports those.

## FIX

- `FIX-01` **SetEvent without WaitForSingleObject** — A worker-thread signal that skips the ack wait is intentional fire-and-forget, not a missing call.
- `FIX-02` **REPNE SCASB then REP MOVSD/MOVSB** — That is an inlined `strcpy` including the null. Write `strcpy`, never `memcpy` with `strlen`.
- `FIX-03` **mixed base-LEA and per-store reloads in a vertex fill** — Mirror retail's per-field choice of struct copy versus field-by-field.
- `FIX-04` **`cmp reg,1` + `jge` versus `test` + `jg`** — Write `<= 0`, not `< 1`, for a post-decrement test.
- `FIX-05` **CMP immediate is off by one from the decompilation** — The source operator selects the CMP immediate and Jcc. Trust the disassembly, not the decompiler's rewrite.
- `FIX-06` **`jle` to a minimal tail epilogue** — That guard is compiler loop rotation. Use a plain `while`, not an explicit guard plus `do-while`.
- `FIX-07` **`FILD` where you expected `FLD`** — The global is `int32_t`, not `float`. Integer ops on the bits and a caller `__ftol` confirm it.
- `FIX-08` **dead base LEA beside a folded field load** — Materialize the pointer (`Field* f = &obj->arr[i];`). The natural pointer form reproduces it.
- `FIX-09` **scalar fill loop where retail has `rep stosd`** — A runtime-count fill vectorizes only as an `else` tail. Invert the `if` to put it there.
- `FIX-10` **one combined `ADD ESP, N` after several calls** — MSVC6 batches `__cdecl` cleanup. The callees are still `__cdecl`. Add no manual cleanup.
- `FIX-11` **C2065 or a cascading C2872** — A namespace-scope global is invisible to functions defined above it. Place the user after the definition.
- `FIX-12` **simple free or destroy loop over an array** — Addressing-only indexed loops do strength-reduce correctly. Write the natural indexed `for`.
- `FIX-13` **`je` versus `jne` with the two blocks swapped** — Branch layout follows source order. Mirror this function's own order, not a sibling's.
- `FIX-14` **`FCOMPP` with the candidate left on the FPU stack** — A recomputed inner minimum implies a nested ternary with no intermediate local.
- `FIX-15` **every local below the out-params shifts by a constant** — Model an unknown out-param as the minimal type you read. Do not pad to match the frame.
- `FIX-16` **`sub`/`je` chain over one register** — Sparse integer cases were a `switch`, not an `if`/`else if` chain.
- `FIX-17` **error returns sit at the far tail** — Nest the success path inside `if (ok)` so the error becomes the jump target.
- `FIX-18` **a lone `JMP` with no prologue** — A single trailing `__cdecl` call tail-calls. Write the one-line body, add no statements.
- `FIX-19` **every field access shifts and `sub esp` differs** — Named locals for struct fields force spills. Read the fields on demand instead.
- `FIX-20` **false value pre-loaded then conditionally overwritten, single store** — That is a ternary, not an `if`/`else`. Match retail's register-init order.
- `FIX-21` **the condition shares the result register** — A pointer-select ternary pulls the condition into the result register; the `if`/`else` does not.
- `FIX-22` **constant-count zero fill** — A compile-time-constant dword count vectorizes anywhere. No restructuring is needed.
- `FIX-23` **plain `MOV` where you emit `MOVSX`/`MOVZX`** — Retail's load width gives the declared parameter width. Widen or narrow to match it.
- `FIX-24` **the zero test branches the wrong way** — `if (x == 0)` and `if (!x)` invert the test and swap the blocks. Pick by retail's jump target.
- `FIX-25` **`dec eax; jz` chain** — Consecutive cases from 1 lower to 1-byte `DEC`. The source was a `switch`, even with two cases.
- `FIX-26` **`NEG`/`SBB`/`AND`/`ADD`** — That is the natural ternary for a nonzero test. Do not hand-roll the mask form.
- `FIX-27` **`MOV DX, word[mem]; PUSH EDX` with no extend** — Declare the callee parameter 16-bit and pass the value directly.
- `FIX-28` **`if (g != 0)` followed by a vtable call through `g`** — That global is a COM interface pointer, not a flag. Name it by its interface type.
- `FIX-29` **a vtable slot looks wrong against memory** — DirectX 6 method order differs from later SDKs. Read the vendored `DECLARE_INTERFACE_` block.
- `FIX-30` **COM global declared `void*` with a cast at each call** — Use the real `LP<INTERFACE>` type from the vendored header. The codegen is identical.
- `FIX-31` **`LEA EDX,[ESP+4]` with no `SUB ESP`** — A single address-taken out-param reuses the dead parameter's slot. Write the clean local.
- `FIX-32` **`MOV [esp+N], 0` where retail has `XOR EAX,EAX`** — Split the merged value into a separate non-address-taken local.
- `FIX-33` **Ghidra reports `__fastcall` with an unused `param_1`** — `PUSH ECX`/`POP ECX` is the one-dword frame-slot trick. Declare no parameter.
- `FIX-34` **`dwSize` is 0x24 where retail passes 0x14** — The vendored `dsound.h` defaults to DirectX 7. Define `DIRECTSOUND_VERSION 0x0600` first.
- `FIX-35` **raw dword zero stores versus field-aware word and dword stores** — `memset` zeroes raw dwords; `= {0}` zeroes each field. Match retail's grouping.
- `FIX-36` **retail reloads a value inside a loop** — Read the global the local was stored to inside the loop, so the index register stays live.
- `FIX-37` **shared cleanup with a success store placed after it** — Make the cleanup the if-true body of the last check and `goto` into it from earlier failures.
- `FIX-38` **`SUB <base>, <scaled index>` with a negative index** — That is an ascending table. Write `table[-index - 1]` after checking the sign at the call sites.
- `FIX-39` **linked-list walk loop gets a duplicated (peeled) body** — `while`/`do-while`/`for(cond)` forms peel the first iteration. Use `for(;;) { if (!A) break; if (!B) break; body; }`.
- `FIX-40` **loop pointer init hoisted before the count guard** — Put the init inside `if (count) { ...; do { } while (count); }` so MSVC defers the load+LEA past the guard. A plain `while` with pre-loop init saves the reg eagerly.
- `FIX-41` **loop counter spilled to the wrong stack slot** — When retail keeps `param-N` in the param's own slot, reuse the param as the counter (`param -= N; while (param) { ...; param--; }`). A separate `remaining` local lands in a dead param slot and cascades an ECX↔EDX swap. (Inverse of CAP-17: only when the counter is spilled, not register-kept.)

## CAP

- `CAP-01` **pointer reloaded from its stack slot before every store** — An address-taken out-param must stay the address-taken local. Do not add a clean copy.
- `CAP-02` **char kept in `al` with `ecx` scratch** — A character-loop register assignment is not source-fixable. Confirm the semantics and move on.
- `CAP-03` **callee-saved register holds a pointer local in a longer sibling** — Whole-function register pressure picks the register. Not source-fixable.
- `CAP-04` **sibling pushes a callee-saved register eagerly, you push it lazily** — Prologue scheduling differs between siblings and is not source-fixable.
- `CAP-05` **retail walks two running pointers, you emit a scaled index** — For a two-array copy with arithmetic, keep the natural indexed form and accept the cap.
- `CAP-06` **locked vertex-buffer write loop stalls below a match** — Address-mode and diffuse-base choices cap the whole cluster. Accept it.
- `CAP-07` **one register swap plus a store reordering in consecutive struct copies** — Load and store deferral is scheduling. A forcing local makes it worse.
- `CAP-08` **retail `jbe`, yours `jle`, same source** — A toolchain-version opcode choice. Do not churn the parameter type; it will not move.
- `CAP-09` **retail uses an EBP frame, you compile FPO** — A whole-frame regalloc decision. Not source-fixable; prefer a smaller leaf.
- `CAP-10` **`MOV ECX, count` hoisted to the top of a field-zero function** — A constant `rep stosd` count is hoisted regardless of source form. Accept the register shuffle.
- `CAP-11` **four out-params land on different `[esp+N]` slots** — Slot coalescing for several out-params to one COM call differs by toolchain. Accept it.
- `CAP-12` **same instructions, different block order** — Cold exiting blocks are placed by a toolchain heuristic that labels do not control.
- `CAP-13` **`JAE`+`JMP` where retail has `JL`** — Any forward jump out of a `do {} while` inverts the loop. No source form avoids it.
- `CAP-14` **a clean EAX<->ECX swap across the whole body** — A table-loaded pointer's home register differs by toolchain. Six source forms were tested.
- `CAP-15` **`ADD AX,[mem]` where you emit `ADD EAX,ECX`** — Retail re-reads a struct field where the build CSEs it. Adding a local does not help.
- `CAP-16` **retail unrolls a constant-count struct-copy loop, you keep it counted** — The `REP MOVSD` body and running pointers match; only the loop scaffolding differs. No clean source form unrolls.
- `CAP-17` **loop-invariant `x-const` init hoisted as LEA+early load vs retail late MOV+SUB** — MSVC CSEs `count-3` into `LEA reg,[src-3]` and hoists the count load; retail loads count into the home reg late and SUBs. Not source-fixable (~97%).
- `CAP-18` **parallel-walked cursor anchors on the store-target field, retail on the read-source field** — When two field pointers are equally used, MSVC ties to the store target; retail ties to the read source. The actor side matches (both pick the store target); the creature side differs. Robust across 3 forms; only a raw byte-indexed store defeats it (not credible). ~85%.
- `CAP-19` **maskedTexData in EBP vs retail EBX; count register-kept vs retail EBP-spilled** — For a divide-by-3 triangle dispatch loop, MSVC assigns the masked-texture ternary to EBP (pushed eagerly) and keeps the loop count in EBX, where retail assigns maskedTexData to EBX and spills the count to EBP (pushed inside the loop guard). The index walk also takes a negative-offset strip (`[esi-4]/[esi-2]/[esi]` then `add esi,6`) vs retail's `[esi]/[esi+2]` + 3× `add esi,2`. Same vertices and call; pure regalloc + pointer-walk divergence. Robust across 5 forms (34.8-36.5%).
- `CAP-20` **retail reuses span arguments, build reserves two FPO locals** — In alpha-blended span functions, retail uses no local frame and writes the RGB steps and loop count into dead argument slots. The build reserves eight bytes and uses shifted argument and local slots. Explicit parameter swaps and retail-shaped save locals regress. The endpoint walk and channel blend core agree.
- `CAP-21` **merged arithmetic locals use opposite scratch registers** — Retail and the build select different scratch registers for two merged arithmetic results. Natural expression and local forms do not change the selection.
- `CAP-22` **textured alpha spans reserve different FPO locals** — Retail reserves 20 bytes and keeps the destination pointer in EBP. The build reserves 12 bytes and keeps the destination pointer in ESI. This changes the stack homes and registers across the function. The 555 and 565 twins have the same result.
- `CAP-23` **an immediate constant resolves to different data labels** — The instruction bytes match. reccmp names the same immediate from different nearby data symbols and reports an operand difference.
- `CAP-24` **retail retains paired divisors on the x87 stack, but the build spills and reloads them** — In four related UV expressions, the frame, calls, values, and stores agree. Expression and local forms change the spill pattern but do not reproduce the retail schedule.

## TOOL

- `TOOL-01` **retail operand shows <OFFSETN>** — A `// GLOBAL:` annotation without the `0x` prefix fails silently. Always write `0x`.
- `TOOL-02` **one-past-end bound mismatch** — Never bake an absolute address literal to force a match. Keep the symbol-based bound.
- `TOOL-03` **call to an unfinished sibling** — A `// STUB` still registers its symbol, so callers resolve. Call operands pair by address, not by name.
- `TOOL-04` **`Failed to find a match at address`** — Prose between the annotation and the signature breaks recognition. Keep the annotation directly above it.
- `TOOL-05` **call-target label differs, every byte matches** — A comment between `// STUB:` and the body becomes the stub's display name. Remove it.
- `TOOL-06` **a build global lands at a different address** — Globals match by symbol name. Data sections are not byte-compared, so no address pinning is needed.
- `TOOL-07` **negative-offset `[base+global-k]` reads resolve to the preceding symbol** — An early-increment loop makes a later `g_array[i+1]` read land at `g_array-3`; reccmp resolves that disp to the preceding symbol (or `<OFFSETN>`), which differs by layout. Byte-identical pattern; not source-fixable (~96%).
