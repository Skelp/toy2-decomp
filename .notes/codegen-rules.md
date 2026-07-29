# Codegen rules

Source-level idioms that change MSVC6 code generation. Every entry here is
**actionable**: it tells you which source form to write.

Read one section when the index in `codegen-index.md` points at it. Do not read
this file end to end.

A companion file, `codegen-caps.md`, holds the differences that no source form
fixes. `reccmp-mechanics.md` holds the tool and annotation behavior.

## [FIX-01] Stream-thread command/event/ack idiom: not every signal waits for ack

- A producer→worker-thread command pattern commonly lowers as: null-guard the event handle (`TEST reg,reg; JZ <end>`), write a command code and optional args into a global command block, `SetEvent(event)`, then optionally `WaitForSingleObject(ackEvent, INFINITE)` to block until the worker acknowledges.
- The **stop/play** variants block on the ack (they need the worker to finish before proceeding). The **exit** variant does **not** — it signals and returns immediately (fire-and-forget), because the caller does not need the thread to die before continuing. Recognize the absence of `WaitForSingleObject` after `SetEvent` as an intentional fire-and-forget, not a missing call.
- The null guard and the command-block stores reproduce from the natural `if (eventHandle != NULL) { commandCode = N; <args>; SetEvent(eventHandle); [WaitForSingleObject(ackEvent, INFINITE);] }` form. Store order in source matches retail's store sequence (command code before args). The `SetEvent`/`WaitForSingleObject`/`INFINITE` come from `<windows.h>`.
- The command code is a small integer (1=stop, 2=play, 3=exit in this engine). A separate "thread ready" flag is set to 1 by the worker at startup and looped on (`CMP [flag],0; JNZ <loop>`); the exit signal zeroes it so the worker's loop falls out. Name such a flag by its "ready/alive" role, confirmed by the worker's startup-set and loop-test, not by the signal that clears it.

## [FIX-02] MSVC6 inlines `strcpy` as REPNE SCASB + REP MOVSD/MOVSB (copies the null terminator)

- Retail MSVC6 inlines `strcpy(dst, src)` to a fixed sequence: `OR ECX,0xffffffff; XOR EAX,EAX; MOV EDI,src; REPNE SCASB; NOT ECX; SUB EDI,ECX; MOV ESI,EDI; MOV EDI,dst; MOV EAX,ECX; SHR ECX,2; REP MOVSD; MOV ECX,EAX; AND ECX,3; REP MOVSB`.
- The `REPNE SCASB` scans for the terminating null. `NOT ECX` then yields `len+1` (the length **including** the null). The dword loop (`REP MOVSD`) plus the byte tail (`REP MOVSB`) therefore copy the null terminator too. This is `strcpy`, not `memcpy(dst, src, strlen(src))` (which would omit the null).
- Recognize this idiom and write the natural `strcpy(dst, src)`. Do not hand-roll the length/copy. Do not use `memcpy` with a `strlen` argument (it drops the null-copying tail and mismatches). MSVC6 inlines `strcpy` consistently to this exact form in this codebase (matched callers confirm it).
- `strcat` inlines similarly (a `strlen`-like scan of the dest, then a copy of the source). `strcat` with a string-literal argument also auto-resolves the anonymous `.rdata` literal operand without a `// GLOBAL:` annotation (the pointer table for an array of literals does need one).
- Side note: the inlined `strlen` alone (just `REPNE SCASB; NOT ECX` with the count consumed, no copy) is the `strlen` intrinsic. A function that only scans and uses the count is `strlen`, not `strcpy`.

## [FIX-03] Struct copy vs field-by-field for address-taken vertex fills

- When you fill a locked vertex buffer from struct members, MSVC6 generates **different reload patterns**. The pattern depends on whether the source is a struct copy or a per-field assignment. This holds even though the dst pointer is address-taken in both cases.
- A **3-dword struct copy** (`dst[i].position = src->position;` for a 3-float vector) materializes a base LEA to `&src->position`. It loads the dst pointer from its stack slot **once**. It then emits 3 stores through that cached register (`mov [eax],val; mov [eax+4],val; mov [eax+8],val`). It reads the src via `[baseLEA+N]`.
- A **2-dword field-by-field** fill (`dst[i].coords.x = ...; .y = ...;` for a 2-float vector) reloads the address-taken dst pointer **before each store** (`mov ecx,[esp+N]; mov [ecx+off],val; mov eax,[esp+N]; mov [eax+off],val`).
- Do **not** assume one form fits all fields. Retail fill can use struct copy for one member (copied from a whole struct) but field-by-field for another (copied from a different source). To match exactly, use the same mix in source. If you write all field-by-field, you drop the position base-LEA. If you write all struct-copy, you merge the coords dst reloads. Inspect the retail vertex-fill disasm. Mirror the per-field choice.
- Heuristic: a copied member whose src is a whole struct (`a.x = b.x`) reproduces as struct copy when retail shows a live base LEA to `&src`. A copied member whose retail reloads the dst pointer per store reproduces as field-by-field.

## [FIX-04] MSVC `x < 1` vs `x <= 0` codegen

- For post-decrement tests, write `if (--*p <= 0)`, not `if (--*p < 1)`.
- MSVC emits `cmp reg,1`+`jge` for `< 1`, but `test reg,reg`+`jg` for `<= 0`. This is a real codegen difference that reccmp surfaces.

## [FIX-05] Comparison operator determines CMP immediate + Jcc (decompiler renders `>= N` as `> N-1`)

- MSVC6 does **not** algebraically fold `> N-1` to `>= N` or `< N+1` to `<= N`. The source comparison operator directly selects both the CMP immediate and the Jcc opcode. `x >= K` becomes `CMP x,K; JGE`. `x > K` becomes `CMP x,K; JG`. `x <= K` becomes `CMP x,K; JLE`. `x < K` becomes `CMP x,K; JL`.
- The Ghidra decompiler frequently renders the boundary form as the algebraically-equivalent neighbor. It shows `x >= K` as `K-1 < x` (i.e. `x > K-1`). It shows `x <= K` as `x < K+1`. Trust the **disassembly's** CMP immediate and Jcc, not the decompile's textual rendering.
- So when retail shows `CMP reg, 0x474; JGE`, the source was `x >= 0x474`. If you write `x > 0x473` instead, you emit `CMP reg, 0x473; JG`, which mismatches. Likewise `CMP reg, 0x17; JLE` means `x <= 0x17`. It does not mean `x < 0x18` (which gives `CMP reg, 0x18; JL`).
- This generalizes the `x < 1` vs `x <= 0` gotcha above to arbitrary N. Pick the operator that directly produces retail's CMP/Jcc, not the decompiler's rewritten neighbor.

## [FIX-06] Zero-trip loop guard: plain `while`/`for` vs explicit `if (n<=0) return` + `do-while`

- When retail shows a loop zero-trip guard as `test reg,reg`+`jle <far tail>` (a forward jump to a *minimal* epilogue at the function's end, which pops only the regs pushed before the guard), the source was a plain `while`/`for` loop. MSVC6 generates the guard itself via loop rotation. It places the zero-trip return at the tail.
- If you instead write an explicit `if (count <= 0) return 0;` guard *before* a `do { ... } while (i < count);`, you **inline** the return (`test reg,reg`+`jg skip` / `xor eax,eax`+`pop ebx`+`ret` locally). This fails to match the `jle <tail>` form.
- Use `while` when retail's guard is a forward `jle` to a tail epilogue. Reserve explicit guard+`do-while` for when retail's early-out is inlined at the top.
- Hint: when retail loads the loop-bound global into EAX *before* it zeroes the counter (EBX), a `while` with a separate `i = 0` init matches better than a `for`. A `for` inits the counter first.

## [FIX-07] Detect mistyped float/int globals via FILD vs FLD

A global declared `float` but actually `int32_t` is a common silent error. `MOV [glob],EAX` is type-agnostic, so a trivial setter can score 100%. The error only surfaces in *consumers*.

Three signals, together conclusive:

1. A consumer does raw integer arithmetic on the bits (`ADD`/`IDIV`/`CMP`) with no float load or store. Integer ops on a float bit-pattern would be garbage.
2. A consumer loads the global with `FILD` (int to float), not `FLD` (float load), when it builds a vertex or coordinate.
3. A caller converts float to int (`__ftol`) *before* it passes the value to the setter. This proves the setter param (and thus the global) is int32.

Fixing the type ripples to the setter signature, the typedef, and float-passing callers. The blast radius is usually small (grep the global and setter). But check all references. A float-typed setter can hide the error until you reconstruct a consumer.

## [FIX-08] Match retail's dead base-LEA by materializing the pointer

- MSVC6 sometimes emits a *dead* base-pointer `LEA` next to a folded field load.
- Pattern: retail emits a dead base `LEA` (immediately overwritten) *and* reads a field via a folded load (base + small displacement). The dead LEA is vestigial. The consumer recomputes the base.
- If you write the field directly (`obj->arr[idx].field`), you get a different valid form. This form folds the element stride into the index LEA and has no load displacement. It omits the dead LEA. It lands well short of a match.
- The natural pointer form a developer would write reproduces the quirk. It yields a base LEA *plus* a folded field load:

```cpp
Field* f = &obj->arr[obj->map[(uint8_t)c]];
... f->field ...
```

- This is not score-chasing. It is credible readable source that triggers the same address-mode selection. When retail shows a dead base LEA beside a folded field access, try the pointer form.
- Inverse: a missing or dead `LEA` in your build that retail has is weak evidence alone. Confirm that the surrounding field access matches in *value* (same displacement, same register) before you conclude anything. The dead LEA is a compiler artifact, not a source requirement.

## [FIX-09] MSVC6 `rep stosd` fill loop needs the else-tail layout

- MSVC6 vectorizes a simple constant-fill `for (i=0;i<n;i++) a[i]=val;` loop into `mov ecx,n; or eax,val; mov edi,a; rep stosd` **only when the loop is a clean tail block** (an `else` body that a `je`/`jne` jumps to). The same loop written as the `if`-fall-through body compiles to a scalar `mov/cmp/jl` loop.
- Consequence: for an `if (p==0) { fill; } else { work; }`, the compiler lays the fill inline (fall-through) and emits a scalar loop. The retail binary instead jumps `je` to a tail and emits `rep stosd`. If you swap to `if (p!=0) { work; } else { fill; }`, you fix *both* the branch layout (the non-null path becomes fall-through) and you trigger `rep stosd`. This is a single source idea.
- The fill count is a runtime dword count, so this is a vectorized loop. It is not `memset` (which would be `rep stosb` with a byte count).

## [FIX-10] Deferred-stack-cleanup for chained `__cdecl` calls (single `ADD ESP, N`)

- MSVC6 defers caller cleanup when a `__cdecl` function makes several `__cdecl` calls in sequence. Instead of `ADD ESP,4` after each one-byte-arg call, it emits one combined `ADD ESP, <total>` at the very end.
- The natural source (several plain calls, no manual stack math) reproduces this exactly. Do not insert dummy cleanups.
- Do not misread the missing per-call `ADD ESP` as evidence that the callees are `__stdcall`/`__thiscall`. They are plain `__cdecl` functions. The cleanup is just batched at the caller's tail.

## [FIX-11] Namespace-scope globals must be defined before use (MSVC6 C2065/C2872)

- A namespace-scope `int32_t g_foo;` definition is **not** visible to functions defined earlier in the *same* translation unit, even within the same namespace. MSVC6 reports `C2065: undeclared identifier` at the use site. A downstream `C2872: ambiguous symbol` can appear at unrelated `using namespace` sites. This is a misleading cascade. The real fix is the use-before-definition.
- A setter that uses a global works only if that global is defined *above* the setter. If a global is defined partway down the file, a function that uses it must be placed *after* that definition. Do not merely group it with related functions by address order.
- Fix options, in order of preference: place the new function after the global's definition in the same TU (this keeps the global where it is). Or move the global definition up to its conceptual group (this is safe for codegen because the linker fixes the address, but it is a larger diff). Avoid scattered `extern` forward declarations. Prefer to define globals in order.

## [FIX-12] Simple addressing-only indexed loops DO strength-reduce to running pointers (unlike the two-array copy case)

- The gotcha above is specific to **two-array copy loops with arithmetic** (`dst[i].field = a + src[i].field`). There the indexed form diverges from retail's two-running-pointer codegen.
- For **simple addressing-only loops** (the index addresses an element; no arithmetic on the loaded value), the natural indexed `for (i=0;i<N;i++) use(arr[i].field)` matches retail exactly. MSVC6 strength-reduces by loop shape:
  - **Constant count** (`for(i=0;i<64;i++) if(a[i].p) free(a[i].p)`): lowers to a running pointer + `DEC/JNZ` count-down.
  - **Parallel arrays at a fixed struct offset** (`for(i=0;i<n;i++){ free(a[i]); free(b[i]); }` where `a` and `b` are adjacent members): lowers to a running pointer over one with the other reached at the fixed offset.
  - **Runtime count** (`for(i=0;i<count;i++) free(arr[i])`): lowers to count-up indexed (`INC/CMP/JL`) with the count reloaded from the struct field each iteration.
- Do **not** preemptively hand-roll a running pointer for a simple free/destroy loop. Write the natural indexed `for`. It matches. Reserve the "accept the partial match" guidance for the two-array copy/arithmetic case.

## [FIX-13] MSVC6 if/else branch layout follows source order (cluster can be inconsistent)

- MSVC6 lays out an `if (c) { A } else { B }` as: fallthrough = `A`, jump-target = `B`. If you invert the condition to `if (!c) { B } else { A }`, you flip which block is fallthrough (`JE` vs `JNE` + block order), even though the semantics are identical. The compiler does *not* reorder by block size here.
- The same logical check can appear with **opposite branch orders across sibling functions** in one cluster. The devs were inconsistent, and the disassembly preserves it. If you copy a sibling's branch order, you can lose ~10% or more purely from the block-order and branch-direction diff. If you mirror retail's own order for that function, you match fully.
- Signal: the diff shows only the branch region differing (`je` vs `jne`, and the two call blocks swapped as fallthrough vs jump-target). The rest matches. That is the branch-layout mismatch, not a data-model problem.
- Fix: match the per-function branch order from the disasm. If you invert the `if`/`else` (and the condition), that is a credible, semantically-identical source form that reproduces retail's block layout. It is not score-chasing.
- Hoisted common-call-args (shared `push`es before the branch for a call made in both branches) reproduce automatically from two separate calls in each branch. MSVC hoists them regardless of which branch is fallthrough.

## [FIX-14] MSVC6 float-min-of-three keeps the candidate on the FPU stack (hard to match)

- Retail may compute `min(v0.z, min(v1.z, v2.z))` via `FLD v1.z; FLD v2.z; FCOMPP` (load both, compare-pop-pop). It reloads the winner. Then it runs `FLD v0.z; FCOMP st(1)` and keeps the candidate on the FPU stack. In the `v0.z >= candidate` branch it **recomputes** the inner min, because `FSTP st(0)` discarded it.
- The double-evaluation of the inner min implies the source is a nested ternary or a MIN/MAX macro with NO intermediate local. A stored local `float cand` would spill to memory and would not recompute. You cannot reproduce the on-stack FPU candidate from clean C++ without source contortion.
- **The macro form is required, not optional.** An inline nested ternary `((a < ((b < c) ? b : c)) ? a : ((b < c) ? b : c))` lowers to `FCOMP m32` (compare one operand on the stack against the other in memory). The same expression wrapped in a `#define FMIN(a,b) ((a)<(b)?(a):(b))` macro and called as `FMIN(a, FMIN(b, c))` lowers to retail's `FLD; FLD; FCOMPP` (load both, compare-pop-pop) and keeps the candidate on the FPU stack. Confirmed on SubmitSortedTriangle (0x004B5E40): inline ternary = 80%, macro = 100% exact. Use the macro.
- A clean `if (b < m) m = b;` form compiles to `FCOM` (single-operand, no pop) plus a memory spill. That is a STRUCTURAL (not incidental) diff.
- Separately, retail may defer callee-saved `push ebx/esi/edi` PAST early-exit guards. A clean `if (a && b) { ... }` pushes them at entry instead. To match, use separate sequential guards that allow the pushes to be scheduled after the early-outs. **Corollary:** an early `return` inside the body (e.g. in the success branch of an if/else) also forces eager callee-saved pushes at entry, because MSVC shares the epilogue across both return paths. Use a shared-tail if/else (`if (p) a; else b; shared;`) instead of `if (p) { a; shared; return; } b; shared;` when retail defers the pushes. Confirmed on SubmitSortedTriangle (0x004B5E40): early-return splice = 67% with eager pushes; shared-tail if/else = 100% with deferred pushes (MSVC still emits two tails via duplication).

## [FIX-15] Frame-size cascade from an unreconstructed callee's out-param size

- When a function calls an unreconstructed wrapper whose out-parameter's real size is unknown (the wrapper writes through a struct tied to an unreconstructed abstraction), the consumer's frame size depends on how large a local it declares for that out-param.
- Retail declares the full struct (e.g. a multi-dword caps/desc struct), so its frame is larger. A reconstruction that models the out-param as the minimal type the consumer actually *reads* (e.g. a single `uint32_t` for a caps dword tested with one mask) produces a smaller frame. Every stack slot below the out-params then shifts by the size difference. This cascades to many `[esp+N]` offset diffs that look structural. But they are pure local allocation.
- Do **not** pad the out-param to match the frame. That requires you to guess the retail struct's size and type. They depend on the unreconstructed abstraction (a defer condition). Padding purely to raise the score is score-chasing. Model the out-param as the minimal type the consumer reads. Accept the frame cascade.
- Signal: a constant `[esp+N]` offset shift (e.g. 0xc) applied to *every* local below the out-params, while the out-params themselves and all instructions match modulo the shift. Confirm that the gap sits between the out-params and the next local in retail's frame.
- Corollary: a `(x >= 0) ? 2 : 0` ternary may lower as either `setge; dec; and 0xfffffffe; add 2` or `setl; dec; and 2`. Both compute `2*(x>=0)` with identical semantics. The set-instruction choice is regalloc. Do not rewrite to flip it.

## [FIX-16] Sparse `switch` lowers to a subtract-chain. `if/else if` does not

- For a small set of sparse integer cases (e.g. mapping 3 FVF codes to strides), retail MSVC6 lowers a `switch (x) { case A: ...; case B: ...; case C: ...; default: ...; }` to a **subtract-chain**: `mov eax,x; sub eax,A; je L1; sub eax,(B-A); je L2; sub eax,(C-B); je L3; xor eax,eax; ret` (cases tested in ascending order, with shared return blocks for equal return values).
- If you write the same logic as an `if/else if` chain (`if (x==A) ...; else if (x==B) ...; else if (x==C) ...;`), it instead lowers to separate `cmp eax,K; jne` tests. For the last branch, it uses a branchless `setne cl; dec ecx; and ecx,V` form. That is a large structural diff.
- So when retail shows a `sub`/`je` subtract-chain over the same register, the source was a `switch`, not an `if/else if` chain. Prefer `switch` for sparse int-to-value mappings.

## [FIX-17] Inverted `if (ok) { success }` places error returns at the far tail

- A function with several `if (err) return ERR;` guards can lower two different ways. MSVC6 **inlines** the error return as fall-through (`test reg,reg; jne <success>; <err return>; success:`) when the guard is `if (err) return ERR;`. But it places the error return at a **far tail** (`test reg,reg; je <far tail>` with the success path that falls through) when the source nests the success path inside `if (ok) { ...success... }` and puts the `return ERR` after the block.
- Retail commonly uses the far-tail form for multi-guard allocators (unknown-arg → E_INVALIDARG, malloc-fail → E_OUTOFMEMORY, success → 0). The tail blocks are ordered innermost-error-first. Reproduce it by nesting: `if (ok1) { if (ok2) { ...success; return 0; } return ERR2; } return ERR1;` rather than a flat sequence of early `return ERR` guards.
- This is the `if/else` branch-layout rule specialized to early-returns. Invert the guard so the success path is fall-through and the error is the jump-target. The result is semantically identical, credible original source.

## [FIX-18] Single trailing `__cdecl` call lowers to a JMP tail-call (thunk)

- A `void f() { g(); }` whose only body is a single call to a `__cdecl` function `g` (same effective signature, return void) is lowered by MSVC6 as a **tail call**: `JMP g` (5 bytes), not `CALL g; RET` (6 bytes). There is no `ADD ESP` because the callee's `RET` pops the caller's frame directly.
- This is how a 5-byte forwarder or thunk (`E9 + rel32`) appears in retail. Reproduce it with the natural one-line body `void f() { g(); }`. Do not write an explicit `return g();` (void) or add trailing statements. Any extra statement forces `CALL; RET`.
- This works even when `g` is a `// STUB` (reccmp resolves the `JMP` operand to the stub's address by symbol name). The thunk's own annotation is `// FUNCTION`. It has a real, complete body (the tail call), even though the target is a stub.
- Signal: retail is a lone `JMP <known function>` with no prologue or epilogue. That is the tail-call thunk, not a `CALL`.

## [FIX-19] Named locals for struct fields cause a frame-size cascade (read fields on demand)

- When a function fills a stack struct (e.g. a `DDSURFACEDESC2` passed to a lock/query API) and then reads several of its fields, retail MSVC6 often keeps those fields **only in registers** (EAX/ECX/EDX/ESI/EBX/EDI). It re-reads from the struct on demand. It never spills them to dedicated stack slots.
- If you introduce named locals (`int32_t height = sd.dwHeight; int32_t width = sd.dwWidth; int32_t pitch = sd.lPitch;`), you force MSVC6 to **spill** them. This grows the frame (`sub esp, 0x88` vs retail's `0x7c`) and shifts every `[esp+N]` offset below the spills by the size difference. This looks like a huge structural diff. But it is pure local allocation.
- Fix: read the struct fields **directly** where you use them (`g_screenDimH = sd.dwHeight; int32_t clipRight = sd.dwHeight - 1; ...`). Keep only the few values that retail genuinely keeps live across stores (e.g. `clipRight`/`clipBottom`/`clipTop`, which several stores reuse). MSVC6 then CSEs the repeated field reads into the same caller-saved registers retail uses. It re-reads from the struct at later use sites (after calls clobber the caller-saved regs) exactly like retail.
- Signal: the verbose compare shows a constant `[esp+N]` offset shift applied to **every** local or field access, with the `sub esp` size differing, but all instruction *mnemonics* match modulo the offset. That is the spill-cascade, not a logic error.
- Corollary: a `Type& alias = sd.member;` reference local also costs a stack slot (4 bytes). Access `sd.member.field` directly instead.

## [FIX-20] Pointer/value ternary lowers to load-default + conditional-overwrite + single store

- A `ptr = (cond) ? valA : valB;` ternary where the result feeds a single store lowers as: load the **false** value into a register first, test the condition, and if true **overwrite** the register with the true value. Then store once. There is no `jmp` after the overwrite. The true-value load falls straight through to the store. There is no second store.
- Contrast with the equivalent `if/else` (`if (cond) ptr = valA; else ptr = valB;`). MSVC6 lowers it as either two separate stores or a branch-with-`jmp`-then-store. That is a real structural diff against the ternary form, even though the semantics are identical.
- Signal: retail shows `MOV reg, valB; CMP src, reg; JNZ skip; MOV reg, valA; skip: MOV [dst], reg` (false-value pre-loaded, single conditional overwrite, single store, no `jmp`). That is the ternary, not an `if/else`.
- When the stored value is reused for a follow-on derivation (`derived = ptr + K;` immediately after), the ternary form keeps the result in the register across the store with **no reload**. An `if/else` or a re-read of the global (`derived = g_ptr + K;`) may reload. Prefer the ternary (or a temp local that holds the result) when retail reuses the just-stored value without a reload.
- **Inverse case: a ternary assigned to a *local* (not one that feeds a store) can lower load-TRUE-first.** `uint32_t v = (acc > 0xffff) ? 0xffff : acc;` may lower as `CMP acc,0xffff; MOV reg,0xffff (true first); JA skip; MOV reg,acc (false); skip:`. That is the opposite of the store-feeding ternary above. Retail sometimes wants load-FALSE-first for the same clamp (`CMP acc,0xffff; MOV reg,acc (false); JBE skip; MOV reg,0xffff (true); skip:`). Reproduce that with the if-overwrite form: `uint32_t v = acc; if (acc > 0xffff) v = 0xffff;`. It explicitly loads the false value first and conditionally overwrites it with the true value, with no `jmp`. The semantics are the same as the ternary. Pick the form that matches retail's register-init order. The branch direction also flips with it (`JA` for load-true-first vs `JBE` for load-false-first) because the skip target differs.

## [FIX-21] Pointer-select ternary that feeds a deref evaluates the condition into the result register

- A pointer-select `int32_t* p = cond ? &A : &B;` (the `&A/&B` then dereferenced, `v = *p;`) lowers differently from the equivalent `p = &A; if (cond == 0) p = &B;` if/else. The difference is which register holds the condition.
- The **ternary** evaluates the condition *into the result register* (EAX) first. Then it overwrites EAX with the selected pointer: `mov eax,[cond]; test eax,eax; mov eax,&A; jne skip; mov eax,&B; skip: mov ecx,[eax]`. The condition and the pointer share EAX (cond loaded, tested, then overwritten).
- The **if/else** (`p = &A; if (cond == 0) p = &B;`) reserves EAX for the default pointer `&A` first. So the condition loads into a *different* register (`mov ecx,[cond]; test ecx,ecx; mov eax,&A; jne skip; mov eax,&B`).
- Both are semantically identical and structurally near-identical. The ONLY diff is the condition's register (EAX vs ECX) plus the `test` operand. Signal: retail loads the condition into EAX and immediately reuses EAX for the pointer. Use the ternary. When retail's condition sits in a side register while EAX holds the default pointer, use the if/else.
- This is the pointer/deref analogue of the store-feeding ternary gotcha above. The ternary pulls the condition into the result register. The if/else keeps the result register reserved for the default.

## [FIX-22] Constant-count dword zero-fill lowers to `rep stosd` regardless of branch position

- The "rep stosd needs the else-tail layout" gotcha above is specific to a **runtime** dword count (`mov ecx, n` where n is a variable). That case compiles to a scalar `mov/cmp/jl` loop when the fill is the `if`-fall-through body. It vectorizes to `rep stosd` only when you hoist it to an `else` tail.
- A **compile-time-constant** count with a zero value (`for (i=0;i<K;i++) arr[i]=0;` where K is a literal) is recognized as a memset-like idiom. It lowers to `mov ecx,K; xor eax,eax; mov edi,arr; rep stosd` **even in the `if`-fall-through body**. No restructuring is needed. Write the natural `if (cond) { for (...) arr[i]=0; ... }` form.
- This is a dword fill, not `memset` (which would be `rep stosb` with a byte count). Keep the element type as a 4-byte int and the literal count as the dword count. Then `mov ecx` gets the dword count, not the byte count.

## [FIX-23] Wrapper that forwards a param must match retail's load width (int16 vs int32)

- When a thin wrapper forwards a parameter to a core function (e.g. `return Core(a, b, c, d, e);`), the parameter's declared type controls the load instruction. A 16-bit-typed param loads with `MOVSX`/`MOVZX word [esp+N]`. A 32-bit-typed param loads with a plain `MOV dword [esp+N]`.
- Retail's load width is the truth. If retail loads the arg with a plain 32-bit `MOV` (no sign or zero extension), the original declared it a 32-bit type. A stub's `int16_t` guess is wrong. You must widen it to `int32_t` (in both the header and the definition). This is the int-width analogue of the "FILD vs FLD" float/int mistyping gotcha.
- The forwarded callee's corresponding parameter must also be declared 32-bit, or MSVC6 will extend at the forwarding site. When the callee is itself a stub you declare, set its param type to match retail's load width.
- A `return f(args);` wrapper with several args does **not** tail-call (no `JMP f`). MSVC6 emits `CALL f; ADD ESP, N; RET` (caller cleanup). The 5-arg forwarding register allocation matched retail exactly in practice. Write the natural one-line delegation. Let the compare confirm.

## [FIX-24] `if (x == 0)` vs `if (!x)` lower differently (branch direction + fall-through swap)

- These are semantically identical. But MSVC6 lowers them with **opposite** branch directions and swapped fall-through/jump-target blocks:
  - `if (x == 0) { A } else { B }` becomes `test eax,eax; jne <B>; <A>; jmp end; <B>; end:`. A is fall-through. B is the `jne` jump-target.
  - `if (!x) { A } else { B }` becomes `test eax,eax; je <A>; <B>; jmp end; <A>; end:`. B is fall-through. A is the `je` jump-target.
- `if (x == 0)` keeps the natural "jump-to-else when condition false" form (`jne` to the else body). `!x` inverts the test to "jump-to-then when condition true" (`je` to the then body).
- So when retail shows `test; jne <blockB>` with A as fall-through, the source was `if (x == 0) { A } else { B }`. It was **not** `if (!x) { A } else { B }` (which would be `test; je <A>` with B as fall-through). If you pick the wrong form, you flip the branch opcode (`je` vs `jne`) and swap the two blocks.
- This is the boolean-negation analogue of the if/else branch-layout gotcha. The *form* of the condition (`== 0` vs `!`) controls the test direction, not just the block order. When you match a zero-test if/else, try `== 0` first if retail's else is the `jne` jump-target. Switch to `!x` only if retail's then-body is the `je` jump-target.

## [FIX-25] Consecutive-case switch that starts at 1 lowers to `dec eax; jz` (1-byte), not `sub eax,1`

- The "Sparse switch" gotcha above describes the subtract-chain as `sub eax,A; je L1; sub eax,(B-A); je L2`. When the minimum case value is 1 (so the first subtract is `sub eax,1`) **and** the cases are consecutive (each step subtracts 1), MSVC6 emits the 1-byte `DEC EAX` (0x48) instead of the 3-byte `SUB EAX,1` (0x83E801) for every step.
- So a 2-case switch `switch (mode) { case 1: ...; case 2: ...; }` on an int16_t (loaded with `MOVSX EAX, word [mode]`) lowers to `movsx eax,word[mode]; dec eax; je L_case1; dec eax; jne L_skip; <case2 body>; jmp L_skip; L_case1: <case1 body>; L_skip:`. Note the **second** test is `jne` (jump-to-skip if not case 2). Case 2 is the fall-through between the two decs. Case 1 is the `je` jump-target.
- Contrast with `if/else if` on the same int16_t. It loads `mov ax, word[mode]` (16-bit, no sign-extend) and emits `cmp ax,1; jne ...; cmp ax,2; jne ...`. That is a different load width (ax vs eax), a different test (cmp vs dec), and different branch opcodes. The `movsx eax` + `dec` subtract-chain is the switch signature, even for only 2 cases.
- A 2-case renderMode-style dispatch that reads as a switch should be written as a switch, even if a sibling function uses if/else-if for the same logic. The devs were inconsistent. The disassembly is the truth.

## [FIX-26] Bool-from-nonzero ternary `cond ? A : B` lowers to NEG/SBB/AND/ADD naturally

- A ternary `(value != 0) ? A : B` where the condition is a nonzero-test of a loaded integer and A/B are constants lowers to the branchless `NEG reg; SBB reg2,reg2; AND reg2,(A-B); ADD reg2,B` idiom. This comes **from the natural ternary source**. No contortion is needed.
- The idiom: `NEG` sets CF iff the value is nonzero. `SBB r,r` yields -1 (all ones) if CF, else 0. `AND r,(A-B)` keeps the delta when true, else 0. `ADD r,B` adds the base. Net: `B + (cond ? (A-B) : 0)` = `cond ? A : B`.
- Do **not** rewrite the source into the literal `(-(uint)cond & (A-B)) + B` form. Do not introduce a separate `bool` local to force it. The plain ternary produces the idiom. A separate `bool` local may instead lower to a `SETE` or a branch, which diverges from retail.
- When the value is a wider global read at a narrower width (e.g. an int32 global read as 16-bit), cast in the condition (`((int16_t)g != 0)`) to reproduce the narrow `MOV reg16, word [mem]` load. The cast does not change the ternary lowering.
- Signal: retail shows `MOV r16, word[mem]; NEG r16; SBB r32,r32; AND r32,K; ADD r32,B` for a `cond ? A : B`. Write the natural ternary with the appropriate-width cast.

## [FIX-27] Passing a `uint16_t` arg reproduces `MOV r16, word[mem]; PUSH r32` (no extend)

- When a function argument is a 16-bit value read from memory and the callee's parameter is declared `uint16_t`/`int16_t`, MSVC6 loads it with a 16-bit load (`MOV r16, word[mem]`) and pushes the full 32-bit register (`PUSH r32`) **without** a `MOVZX`/`MOVSX` extend. The upper bits are left as whatever the register held (don't-care, since the callee reads 16 bits).
- This contrasts with the extend you might expect: if the parameter were `int32_t` (or the value were assigned to an `int32_t` local first), MSVC emits `MOVZX/MOVSX r32, word[mem]; PUSH r32`.
- So to reproduce retail's `MOV DX, word[mem]; PUSH EDX`, declare the callee parameter as `uint16_t` (or `int16_t`) and pass the memory value directly via a cast, e.g. `EvaluateClip(..., *(uint16_t*)((uint8_t*)entry + 4), ...)`. Do **not** widen the parameter to `int32_t` to "be safe"; that forces an extend and mismatches.
- Confirmed by a 100% match on a 44-byte wrapper whose only non-trivial detail was this 16-bit arg push. The natural narrow-parameter + cast form is the credible original source.
- Signal: retail call site shows `MOV DX, word[mem]; PUSH EDX` (16-bit load into the low half, then a 32-bit push of the same register, no `MOVZX`/`MOVSX` between).

## [FIX-28] A nonzero-guarded global with vtable dispatch is a COM object pointer, not a flag

- A global guarded `if (g != 0)` immediately before `MOV reg,[g]; MOV reg2,[reg]; CALL [reg2+N]` (a vtable call through the global) is a COM/interface object pointer (e.g. a DirectSound or DirectDraw interface). It is **not** a boolean "enabled" flag. The `TEST reg,reg; JZ` guard is the standard "is the object created/valid" null check shared by every consumer of that interface.
- Confirm by the create path: an init function calls the creation API (e.g. `DirectSoundCreate`) with `&g` as the out-param, and the API writes the object pointer into `g`. Confirm by the release path: a shutdown function does `MOV reg,[g]; PUSH g; CALL [reg+8]` (the `Release` method at vtable offset 8) and then zeroes `g`.
- Name the global by its interface type (`LPDIRECTSOUND`, `LPDIRECTDRAWSURFACE`, etc.), not as a flag. Do not be misled by the guard reading like a boolean test; the vtable dispatch that follows proves it holds an object.
- The same global often backs a whole subsystem's entry points (create, several guarded consumers, release). Reconstructing any one consumer that guards on it should reuse the established interface-typed global rather than re-declaring a flag.

## [FIX-29] DirectX 6 COM vtable order differs from DirectX 8+ (SetVolume is at 0x3c, not 0x38)

- A retail binary shipped against an older DirectX SDK has a **different** COM vtable method order than the layout you may remember from later SDKs. Do not assume the "standard" ordering you recall is the retail ordering. Confirm against the project's vendored SDK header (`external/include/directx6/...`) and the retail call offsets together.
- Concrete example: `IDirectSoundBuffer` in DirectX 6 places `SetCurrentPosition` (0x34) **before** `SetFormat` (0x38), so `SetVolume` lands at **0x3c** and `SetPan` at 0x40, `SetFrequency` at 0x44, `Stop` at 0x48, `Unlock` at 0x4c, `Restore` at 0x50. Later SDKs reorder to `SetFormat`(0x34), `SetVolume`(0x38), `SetPan`(0x3c), `SetFrequency`(0x40), `SetCurrentPosition`(0x44), `Stop`(0x48). The shared tail (`Stop`/`Unlock`/`Restore`) stays at 0x48/0x4c/0x50 in both, so those offsets alone do not disambiguate the earlier slots.
- Signal: a struct field named for the method at slot 0x3c looks "wrong" vs your memory, but a matched caller invokes `[vt+0x3c]` for that method. Trust the vendored header + retail offset over your recall. Reordering the struct to the later-SDK layout would shift every call offset and break matched callers.
- General lesson: when reconstructing any COM interface method slot, read the vendored SDK `DECLARE_INTERFACE_` block (the real vtable order), not just the `#define METHOD(p,...)` convenience macros (which may be listed in a different order). Cross-check the retail indirect-call offset before naming or reordering vtable struct fields.

## [FIX-30] DirectX COM globals should use the real interface type, not `void*`
- A global that holds a COM interface object (`IDirectSound`, `IDirectSoundBuffer`, etc.) and is only ever used by casting `void*` at each call site should instead be declared as the real `LP<INTERFACE>` type from the vendored SDK header. The cast-free `g_obj->Method(args)` C++ call lowers to the identical `mov reg,[g]; mov reg2,[reg]; call [reg2+<vtable offset>]` sequence as the casted `((IInterface*)g)->Method(args)`. No comparison change, but the source stops duplicating the SDK vtable by hand and matches the convention every other DirectX-using TU follows.
- Confirm the type from retail's vtable call offsets against the vendored header's `DECLARE_INTERFACE_` block (the real method order), not from memory. DirectX 6 `IDirectSoundBuffer` puts `SetVolume` at 0x3c (`SetCurrentPosition` at 0x34 before `SetFormat` at 0x38); `Stop` at 0x48 and `Release` at 0x8 confirm the same interface for the buffers. `IDirectSound`'s `SetCooperativeLevel` at 0x18 (after QI/AddRef/Release/CreateSoundBuffer/GetCaps) confirms the top-level DirectSound object.
- The header that uses the type needs `#include <directx6/dsound.h>` (or the relevant SDK header). Add it to the header, not just the TU, so the `extern` declarations resolve.

## [FIX-31] Address-taken out-param local reuses a dead parameter's stack slot (no `SUB ESP`)
- When a small function takes a parameter, uses it once to index an array, then passes a fresh address-taken out-param local (e.g. `DWORD status;` passed as `&status`) to a COM `GetStatus`-style method and returns that local, MSVC reuses the now-dead parameter's stack slot for the out-param. It does **not** emit `SUB ESP`. The natural source reproduces retail's `LEA EDX, [ESP+4]` (the param slot) for the out-param address and `MOV EAX, [ESP+4]` for the return — the call overwrites the dead param in place.
- Write the natural `DWORD status; obj->GetStatus(&status); return status;`. Do **not** contort with `GetStatus((DWORD*)&param)` casts to force the slot reuse. The clean form reproduces it (confirmed by a 100% match). A separate `DWORD status;` local is credible original source and triggers the same slot reuse because the param is dead by the time the address-taken local needs a home.
- This is the out-param analogue of the locked-VB address-taken-spill gotchas, but in the *opposite* direction: here the address-taken local costs **no** extra stack because it reuses a dead param slot, rather than forcing a per-field reload. The difference is whether the param is still live when the address-taken local is homed.

## [FIX-32] Conditionally-filled address-taken out-param needs a separate merged-value local
- When a function fills an address-taken out-param local via a call on one path (e.g. a COM `GetStatus(&status)`) and assigns it a constant on another path (e.g. `status = 0` when a guard fails), then reads the value after the merge, retail MSVC path-sensitively keeps the constant assignment in a **register** (`XOR EAX,EAX`) on the non-call path and only materializes the local in memory on the call path (call writes the slot, then `MOV EAX,[slot]` loads it). At the merge, EAX holds the value from both paths.
- If you use a **single** address-taken local for both the out-param and the merged value, MSVC writes the constant to memory on the non-call path (`MOV [esp+N], 0`), then either reloads or leaves EAX stale. This mismatches retail's register-only `XOR EAX,EAX` (and shifts every downstream jump offset by the size delta). It also risks a semantic error if the merge uses EAX without a reload.
- Fix: introduce a **separate non-address-taken** local for the merged value. Assign it the constant on the non-call path and the out-param's value on the call path (`merged = status;`). MSVC keeps the merged local in a register on both paths (`XOR` on the constant path, `MOV EAX,[outparam]` on the call path) and skips the memory write on the non-call path. This reproduces retail exactly.
- The address-taken out-param local still gets its memory home (for the call's `&`), but it is read only on the call path. MSVC does not coalesce it with the non-address-taken merged local (different homing), so the split is stable.
- This is the path-sensitive analogue of the "out-param reuses a dead param slot" note above. That one covers a single-path out-param that reuses a dead param slot. This one covers a two-path out-param where the non-call path must stay in a register.

## [FIX-33] `PUSH ECX` frame-slot trick is misdetected by Ghidra as `__fastcall` with a param
- MSVC6 allocates a single dword stack local with `PUSH ECX` at function entry (instead of `SUB ESP, 4`) when the frame needs exactly one dword and the function also pushes callee-saved registers. The matching epilogue is `POP ECX; RET` (no `RET n`). This is the **frame-slot trick**: `PUSH ECX` reserves 4 bytes for the local; `POP ECX` reclaims them.
- Ghidra frequently misreads this as `__fastcall` with a parameter in ECX. The decompile then shows a `param_1` and a `uStack_4 = param_1` save, even though the value of ECX at entry is never used as a real argument.
- Three signals, together conclusive, that it is the frame-slot trick (no parameter):
  1. The epilogue is `POP ECX; RET` (or `POP reg; POP ECX; RET`), **not** `RET 4`. A real `__fastcall` dword param is cleaned by the callee only for `__stdcall`/`__thiscall` methods; a plain `__fastcall` int does not add `RET n`. So `RET n` is not the discriminator — the caller is.
  2. The **caller** invokes the function with no `PUSH` of arguments immediately before the `CALL`, and performs no `ADD ESP, 4` cleanup after it. Inspect the single x-ref's call site. No arg setup = no parameter.
  3. The "param" slot (`[ESP+N]` after the pushes) is used only as the address of an address-taken local (e.g. `LEA EDX, [ESP+8]; PUSH EDX` for a COM `GetStatus(&status)` out-param). The entry ECX value is never read.
- Fix: declare the function as a no-argument `__cdecl void f()`. Declare the address-taken local normally (`DWORD status;`). MSVC6 emits the `PUSH ECX`/`POP ECX` pair from the natural source when `status` is the only stack-homed local (all other locals are register-homed or callee-saved-counter). Do not add a parameter to match Ghidra's guess.
- This is distinct from the "out-param reuses a dead parameter's stack slot" gotcha. That one covers a function that **has** a real parameter whose slot is reused. This one covers a function with **no** parameter where `PUSH ECX` allocates a fresh slot. The discriminator is the caller's argument setup.

## [FIX-34] Vendored DirectX 6 dsound.h defaults DIRECTSOUND_VERSION to 0x0700 (DX7 DSBUFFERDESC is too large)
- `external/include/directx6/dsound.h` defaults `DIRECTSOUND_VERSION` to `0x0700` when undefined. At 0x0700, `DSBUFFERDESC` gains a `guid3DAlgorithm` field, so `sizeof(DSBUFFERDESC)` is 0x24 (36 bytes). Retail Toy Story 2 ships against DirectX 6: it passes `dwSize = 0x14` (20 bytes, no guid). A reconstruction that writes `desc.dwSize = sizeof(desc)` therefore emits 0x24 and mismatches retail's 0x14, and the frame grows to hold the extra GUID.
- Fix: define `DIRECTSOUND_VERSION 0x0600` before `#include <directx6/dsound.h>` in the header (guarded `#ifndef`). This makes `DSBUFFERDESC` the 0x14-byte DX6 layout retail uses. The header also exposes `DSBUFFERDESC1` (always 0x14 bytes) as an alternative, but fixing the version macro is cleaner because `CreateSoundBuffer` takes `LPCDSBUFFERDESC`.
- Safety check: the `IDirectSoundBuffer` `DECLARE_INTERFACE_` block in this header is **not** version-guarded, so its vtable is identical at 0x0600 and 0x0700. Changing the macro does not shift `GetStatus`/`Stop`/`Release`/etc. offsets. Confirm with a full compare after the change; the matched buffer-pool functions should not regress. (Other interfaces may still differ — re-check any interface you newly rely on.)

## [FIX-35] `= {0}` zeroes field-by-field (word/dword mixed); `memset` zeroes raw dwords — match retail's choice
- MSVC6 lowers `Type s = { 0 };` by zeroing **each declared field** in declaration order: a `WORD` field gets a word store (`mov word [esp+N], 0` or `ax`), a `DWORD` field gets a dword store. For a struct mixing 16-bit and 32-bit fields (e.g. `PCMWAVEFORMAT` / `WAVEFORMAT`), this produces a word/dword/word mixed store sequence at field offsets.
- Retail often zeroes the same struct as **raw dwords**: `xor eax,eax; mov dword [esp+0], eax; mov dword [esp+4], eax; ...` (4 dword stores for a 16-byte struct). This is the `memset(&s, 0, sizeof(s))` lowering — `memset` is byte-oriented, so MSVC6 emits dword stores (plus a byte/word tail for non-dword-multiple sizes) without respecting field boundaries.
- So when retail shows raw dword zero-stores over a mixed-field struct, write `memset(&s, 0, sizeof(s));`, **not** `Type s = { 0 };`. The reverse also holds: when retail shows field-by-field word/dword zero stores, `= { 0 }` is the match. Picking the wrong one caps the function well below a match even though the field fills and calls all agree.
- This is distinct from the rep-stosd gotchas. For a **small** constant count (roughly ≤ 5 dwords), MSVC6 scalarizes both forms (no `rep stosd`). For a larger constant count, MSVC6 coalesces both into `rep stosd`. The discriminator here is the **store grouping** (field-aware vs raw), not the vectorization threshold. When two adjacent structs are each `memset`-zeroed separately and both counts stay small, MSVC6 emits two separate scalar dword sequences — matching retail's separate zeroing. Declaring both as `= { 0 }` at once may still coalesce or field-split; verify with the verbose compare.

## [FIX-36] Local stored to a global before a loop: retail reloads from the global inside the loop (keep the index reg live)

- When a function computes a scalar local, stores it to a global/array slot, uses the local once more, then references it again inside a later loop, retail MSVC6 often **reloads the value from the global/array inside the loop** rather than keeping the local in a callee-saved register. It keeps the *index* register live across the loop (for the array addressing) instead.
- A reconstruction that references the **local** inside the loop makes MSVC keep that local in a callee-saved register (e.g. EBP). This evicts the index from EBP, shifting the index to a different register or re-deriving it. The loop's array addressing (e.g. `[ebp*2 + table]`) then uses a different register, and the freq/value is pushed from the callee-saved reg instead of reloaded. This is a structural diff (register assignment + reload-vs-cache), not just scheduling.
- Fix: inside the loop, reference the **global/array slot** the local was stored to (e.g. `g_table[index]`), not the local. This forces MSVC to reload it each iteration (matching retail's `mov dx, [ebp*2 + table]`) and keeps the index register live for the addressing. The local can still be used for the pre-loop accesses where retail loads it once into a caller-saved register.
- Signal: retail loads the value into a caller-saved register (e.g. AX) before the loop, uses it, then in the loop does `xor edx,edx; mov dx, [reg*2 + global]` (reload + zero-extend). The build instead keeps the local in a callee-saved register and pushes it directly. Switching the loop body to read the global reproduces the reload.
- This is the loop analogue of the "read struct fields on demand" gotcha, but for a scalar local that was persisted to a global. The dev likely wrote the loop to read back the stored value (or MSVC CSE'd the local with the stored global and chose the reload). Either way, reading the global in the loop matches.

## [FIX-37] Shared cleanup as the fall-through if-true body of the last check (earlier failures goto into it)

- A function with several failure paths that all share one cleanup block, plus a success path that must skip cleanup, has a characteristic retail layout: the cleanup block sits immediately after the LAST failure check (as its fall-through successor), and the success-store sits after the cleanup (as the JZ target of that last check). Earlier failure checks JMP forward to the cleanup. The success-store falls through to the shared close/return tail.
- The source form that reproduces this: make the last check `if (result != 0) { cleanup: <cleanup body> } else { <success-store> }`, and have the earlier failures `goto cleanup;` into the labeled if-true body. The `cleanup:` label is the first statement of the if-true block. After the if-true body, MSVC emits `JMP <end>` (skip else) which lands on the shared tail (closeFile) — matching retail's `JMP` from cleanup to close. The else (success-store) falls through to the same tail.
- Do NOT write the cleanup as a plain labeled block after the success body with `goto success` for the last check. MSVC's block-placement will inline the cleanup right after the FIRST `goto cleanup` (the earliest failure path), turning that goto into a fall-through and pushing the success body elsewhere. This reorders every block and tanks the match. Making the cleanup the if-true fall-through body of the LAST check (not a separate tail) forces MSVC to place it there, because the last-check-fail→cleanup edge is the only fall-through into it; the earlier gotos are forward JMPs to a now-distant label that MSVC cannot collapse.
- The `goto cleanup` into an `if`-body is legal C++ (label at the block's first statement). MSVC6 accepts it. The label makes the if-true body reachable both via the test (fall-through) and via the earlier gotos.
- The success-store placement (after cleanup, as the else) is what distinguishes this from the naive `if (result == 0) { success }` form, which puts success before the fail path. Retail wants success after cleanup. Only the `if (result != 0) { cleanup } else { success }` form with the last check produces that.
- Signal: retail's last failure check is `TEST; JZ <success-store>; <cleanup>; ...; JMP <close>; <success-store>; <close>`. The success-store is the JZ target placed after the cleanup block. Earlier failures are `JNZ`/`JMP` to the cleanup. This is the layout to mirror.

## [FIX-38] `SUB <tablebase>, <scaled_index>` with a NEGATIVE index is an ascending table (`table[-index-1]`)
- A table lookup that compiles to `LEA EDX,[reg*4+4]; MOV EAX,<base>; SUB EAX,EDX; MOV EAX,[EAX]` looks at first like a descending/forward-indexed table. It is not necessarily so. The index register's sign is the discriminator, and the decompiler rarely tells you it.
- Inspect the call sites. If callers push a **negative** literal (e.g. `PUSH -0x5`) as the index, the `SUB` becomes an effective **add**: `base - (idx*4+4)` with idx<0 lands **above** `base`. The table grows upward from `base`, and the access is `table[-idx-1]` (idx=-1 reads `table[0]` at `base`, idx=-2 reads `table[1]` at `base+4`, etc.). A trailing `NULL` entry (idx = -(N+1)) often terminates the valid range.
- The natural C++ for this is `table[-index - 1]` where `index` is the (negative) parameter. It is a credible original form: the devs used negative IDs in a unified indexing scheme (e.g. negative IDs for sequences, positive IDs for one-shots). Do not hand-roll pointer arithmetic to match; the `table[-index-1]` form reproduces the `LEA+SUB` exactly.
- If the index is **positive** at call sites, the same `SUB` genuinely descends below `base`. That is a different (rare) layout. Confirm the sign before choosing the model. The decompiler's `param_1` gives no sign hint; only the call-site immediate does.
- General lesson: whenever a table address is computed as `base - scaled_index`, check the index's sign at the call sites before assuming the table direction. The `+4`/`-4` constant in the `LEA` is the `[-index-1]` adjustment (the `-1` term), not a stride artifact.

## [FIX-39] Linked-list walk loop gets a duplicated (peeled) body; use for(;;) with breaks
- A bucket-sorted linked-list insert `while (node != NULL && node->key > key) { prev = node; node = node->next; }` (and the `while (node) { if (key <= key) break; ... }` and `do { if (...) break; ... } while (...)` equivalents) compiles with the first iteration **peeled**: MSVC duplicates the `fld/fcomp/test` condition block — once for the first iteration (with the break specialized to the prev==NULL exit) and once for subsequent iterations. Retail has a **single** condition block at the loop top with the continue test at the bottom (rotated `while`).
- The fix is `for (;;) { if (node == NULL) break; if (node->key <= key) break; prev = node; node = node->next; }`. The `for(;;)` with two explicit `break` statements lowers to retail's single-body rotated loop: entry `if (!node) goto set_head`, loop-top break-condition, advance, bottom `if (node) goto loop_top`. No peeling, no duplication.
- Confirmed on SubmitSortedTriangle (0x004B5E40): `while`/`do-while`/`for(cond)` = peeled (61-67%); `for(;;)` with breaks = single body matching retail (100% with the other fixes). The `for(;;)` form is credible original source for a walk-and-splice insert.

## [FIX-40] Loop pointer init must be deferred past the count guard: use if + do-while, not a plain while
- A `while (count != 0) { p = base + count + K; ...; count -= N; ... }` where `p` uses the original `count` and is set up once before the loop. If you write `p = base + count + K; while (count != 0) { ... }` (init before the `while`), MSVC places the `base` load and the `LEA p` BEFORE the count guard, which forces the callee-saved register that holds `p` (e.g. ESI) to be saved at function entry and cascades esp-offset shifts through the prologue. Retail defers both the `base` load and the `LEA p` to AFTER the count guard, saving the loop-only callee-saved registers only on the loop path.
- The fix is `if (count != 0) { p = base + count + K; do { ...; count -= N; ... } while (count != 0); }`. The explicit `if` guard makes the `p` init control-dependent on the guard, so MSVC emits `TEST count; JE skip; <load base; LEA p; save loop regs>; body; TEST count; JNE body; skip:` — exactly retail. The `do-while` inside the `if` is NOT the rotation that FIX-06 warns against: FIX-06's guard is a synthetic rotated guard jumping to a minimal tail; here the guard is a real semantic guard (skip the setup+loop when count==0) jumping to a non-minimal tail, and the init is genuinely inside the guarded block.
- Confirmed on SubmitTriangleList (0x004B5FB0): plain `while` with pre-loop init = 84% (init hoisted before guard, ESI saved eagerly, esp shifts); `if + do-while` with init inside the `if` = 100% exact. The `if` and `do-while` test the same condition, which is mildly redundant but is the faithful form: it guards the one-time pointer setup.

## [FIX-41] Reuse the parameter as the loop counter to keep it in the parameter's stack slot
- When retail computes a loop counter as `param - N` and keeps that counter in the **parameter's own stack slot** (e.g. `[ESP+0x24]` = the `indexCount` param slot, reused via a `MOV ECX,[esp+0x18]` early load + `SUB ECX,3` + `MOV [esp+0x24],ECX` store-back), declaring a **separate** local `int32_t remaining = indexCount - 3;` makes MSVC allocate `remaining` in a **different** dead stack slot (it picks the next dead param slot, e.g. `[ESP+0x20]` = the already-read `indices` param). The slot difference then cascades: the reloads of other params (`param_1`/`param_3`) for each call land at different `[ESP+N]` offsets, which flips which caller-saved register (ECX vs EDX) MSVC keeps free for the index math, swapping ECX↔EDX across the whole first-call setup and loop body. The function scores ~42% despite being semantically identical.
- The fix is to **reuse the parameter itself as the loop counter** via in-place modification: `indexCount -= 3; ... while (indexCount != 0) { ...; indexCount--; ... }`. Because `indexCount` is the parameter, MSVC reads it from its own slot, subtracts, and stores back to the same slot — reproducing retail's `[ESP+0x24]` placement exactly. The in-place `-= 3` also emits `SUB`/`ADD -3` against the slot rather than a `LEA reg,[src-3]` CSE, avoiding the CAP-17 hoist. The ECX↔EDX cascade resolves on its own once the slot aligns.
- This is the **inverse** of CAP-17's trap. CAP-17 (SubmitTriangleStrip, 0x004B6040) had `remaining = indexCount - 3` where `remaining` was kept in a **register** (EBX) and MSVC CSE'd the expression into a hoisted `LEA` — not source-fixable there because the counter had a register home. FIX-41 applies when the counter is **spilled to the stack** (all callee-saved registers are taken by pointers/rolling indices): then reusing the parameter's slot is the source-level lever, and it IS fixable.
- Signal: the verbose compare shows your counter spill at `[ESP+0x20]` (or some other dead param slot) where retail has it at `[ESP+0x24]` (the counter's source param slot), plus a clean ECX↔EDX (or similar caller-saved) swap across the call-argument setup. Switching `int remaining = count - N; while (remaining) { ...; remaining--; }` to `count -= N; while (count) { ...; count--; }` fixes both the slot and the cascade.
- Confirmed on SubmitTriangleStripRaw (0x004B6140): separate `remaining` local = 42% (wrong slot + ECX↔EDX cascade); `indexCount -= 3` reusing the param = 100% exact. The cluster sibling SubmitTriangleStrip (0x004B6040) keeps its counter in a register and hits CAP-17 instead — same source idea, different toolchain outcome because of register vs stack pressure.
