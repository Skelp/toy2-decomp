# Compiler patterns

This file contains short source-form patterns that have produced useful MSVC6
results in this repository. Search it with
`tools/decomp notes QUERY --source codegen`. Treat each row as a hypothesis to
test against the current function.

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
- `FIX-41` **loop counter spilled to the wrong stack slot** — When retail keeps `param-N` in the parameter slot, reuse the parameter as the counter. A separate local can cause a register swap. Use this form only when the counter is spilled.
