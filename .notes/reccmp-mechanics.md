# reccmp mechanics

How reccmp, its annotations, and the Ghidra importer behave. These are tool
rules, not code-generation rules.

Read one section when the index in `codegen-index.md` points at it.

## [TOOL-01] `// GLOBAL:` address must include the `0x` prefix

- A `// GLOBAL: TOY2 005282CC` annotation (no `0x` prefix) silently fails to
  register the retail address-to-name mapping. reccmp does not warn. The only
  symptom is that a matched function referencing that global shows the retail
  operand as an unresolved `<OFFSETN>` while the build side resolves to the
  named symbol. This caps the function just below a match.
- The correct form is `// GLOBAL: TOY2 0x005282CC`. Always include the `0x`
  prefix. A pre-existing malformed annotation can go unnoticed until the first
  matched function references it.
- Signal: the verbose compare shows a single global-store/load line where the
  retail side reads `[<OFFSETN>]` and the build side reads
  `[Namespace::g_symbolName (DATA)]`. Every other instruction matches.

## [TOOL-02] Do not use raw address literals to force a match

- Never bake an absolute literal (e.g. `while ((int)p < 0x........);`) to normalize both sides and raise a score. It hides the real condition.
- One-past-end bound mismatches occur because reccmp resolves `OFFSET` operands against the *current* matched-symbol set. With many functions unmatched, adjacent objects shift, so a bound resolves differently per side.
- As unmatched functions resolve, the symbol table stabilizes. The faithful form (e.g. `while (p < &array[count]);`) then matches on its own.
- Keep the natural symbol-based bound. If it mismatches today, record it in the handoff and move on.

## [TOOL-03] STUB symbols resolve as references

- `// STUB:` registers the symbol. `should_skip()` only suppresses *body* comparison. It does not suppress the symbol.
- A matched `// FUNCTION` may call, take the address of, or use as a pointer-init a not-yet-reconstructed function. reccmp resolves the operand to the stub's address on both sides.
- Supported way to depend on an unfinished sibling: forward-declare it, give it a `// STUB` body, and reference it normally. Do not use raw address literals for this.
- **Call operands resolve by PAIRED ADDRESS, not by name string.** This differs from the "Globals match by symbol name" gotcha below. Globals have no fixed build address (the linker places them), so they must match by name. Functions are paired by address via `// FUNCTION`/`// STUB` annotations. Practical consequence: a call to an unreconstructed sibling resolves correctly even when the build's namespace qualification differs from the map's RE label (e.g. build PDB symbol `NS::Class::Method` vs map label `Class::Method`). You do not need to make the map name match the build's qualified symbol for CALL operands. The address pairing is what counts.

## [TOOL-04] Annotation must sit directly above the signature

- reccmp attaches `// FUNCTION:`/`// STUB:` to the **next** definition.
- Prose between the annotation and the header breaks recognition. The function is still built and emitted to the PDB. But reccmp reports `"status":"unmatched"`, `"matching":0`, and the per-function verbose comparison fails with `Failed to find a match at address 0x...`.
- The aggregate denominator grows by one, so the summary and `progress` look normal. Only the verbose comparison or the report's `unmatched` row reveals it.
- A clean STUB-to-FUNCTION conversion can be *worse* than the stub. A registered STUB at least resolves as a reference target.
- Rule: keep the annotation block immediately above the signature. Move prose *above* the `// FUNCTION:` line. After any annotation edit, verify that the per-function verbose comparison prints a diff, not the "Failed to find" error.
- Keep prose to the minimum. Add it only where it is absolutely necessary.

## [TOOL-05] STUB annotations must not carry a comment before the function body

- If you write a `//` comment block between `// STUB: TOY2 0x...` and the function definition, reccmp's annotation parser uses the comment text as the STUB's display symbol name. The symptom: a `call` to that STUB from a matched caller shows a small diff. The ONLY lines that differ are the call-target label. The retail side shows the comment text. Your side shows the C++ name. Every instruction byte is otherwise identical.
- Fix: keep `// STUB: TOY2 0xADDR` immediately followed by the function definition, exactly like the other STUBs in the repo. Put any explanation in the header declaration (above the prototype) or in handoff or notes. Do not put it between the STUB annotation and the body.

## [TOOL-06] Globals match by symbol name, not by retail address pinning

- reccmp resolves a `MOV`/`FMUL`/etc. operand that references a global to a *symbol name* on each side. It then compares names. It does **not** require the build-side symbol to live at the retail address. The `// GLOBAL: TOY2 0xADDR` annotation registers the **retail-side** address-to-name mapping (so a retail raw address resolves to the name). The build side resolves via the PDB (wherever the linker placed the symbol).
- Consequence: a build global can land at a completely different address than retail. The referencing function still matches 100% as long as both sides resolve to the same name. The build's `.data`/`.rdata`/`.bss` layout is free. You do **not** need a linker script or address pinning.
- `data_sources: []` in `reccmp-project.yml` means the tool does not byte-compare data sections. It compares only `.text` functions (per-instruction, with operand symbol resolution).
- Practical wins:
  - A `.rdata` `const double` multiplier (e.g. `extern const double k_x = 1.0/320.0;`) referenced by `FMUL double ptr [retailAddr]` matches as long as it is annotated `// GLOBAL: TOY2 <retailAddr>` and referenced by name. The build-side address can differ.
  - `extern const double X = val;` (the `extern` + initializer form) gives a named const external linkage. It then appears in the PDB and lands in `.rdata`. `extern const double X;` in the header is the matching decl.
  - If you add a `// GLOBAL:` annotation for a previously-unannotated retail data address, it **helps** future reconstruction. Any function that references that raw address now resolves to the symbol name on both sides.
- Caveat: if a *matched* function references a data address that is annotated on one side but the build side has no corresponding named symbol (e.g. the build function still uses a raw literal), that operand will mismatch. Keep the annotation and the named reference together in the same change.

## [TOOL-07] Negative-offset `[base + global - k]` reads resolve to the preceding symbol, not the intended global

- reccmp resolves a `[reg + disp32]` operand to the symbol that *contains* the displacement address (nearest symbol `S` with `S <= addr < S+size`). For a **positive** offset into a named array (`g_array[i]` with `i >= 0`), the displacement lands inside `g_array` and resolves cleanly to `g_array` (the intended symbol). For a **negative** offset (`g_array - k`, which arises when the loop counter is incremented early and a later read uses `g_array[i+1]` expressed as `[post_inc_ecx + (g_array - 3)]`), the displacement lands *before* `g_array` and resolves to whatever symbol precedes it — or is unresolved (`<OFFSETN>`) if nothing annotated sits there.
- The mismatch is **asymmetric by layout**: retail and the build place globals at different addresses, so the symbol preceding `g_array` can differ. Symptom: one side shows `<OFFSETN>` (unresolved raw) and the other shows `g_someOtherGlobal+k` (a different preceding symbol). The instructions are byte-for-byte **pattern-identical** (both `mov al, [ecx + g_array - 3]`); only the symbol resolution differs.
- Confirmed on `SoftwareRenderer::UnkFunc7` (0x00470C70): the green/red palette reads are `mov al, [ecx + g_paletteSource - 3]` / `- 2` (early-increment strip). Retail's disp `0x704a39`/`0x704a3a` is unresolved (`<OFFSET6>`/`<OFFSET8>`) because nothing annotated precedes `g_paletteSource` (0x704a3c) in retail; the build's disp resolves to `g_unk504D34+1`/`+2` because `g_unk504D34` happens to be placed 4 bytes before `g_paletteSource` in the build's BSS layout. Score ~95.65%; every other instruction (blue read/store, green/red stores, all `/128` math, the `SetEntries` vtable call) matches exactly. Byte dump confirms both sides emit `8a 81 <disp32>` with disp32 = `g_paletteSource - 3` / `- 2`.
- **This is not a codegen error and not source-fixable.** Retail's own codegen uses the negative-offset displacement (the early increment is retail's choice); you cannot express the green read at a non-negative `g_paletteSource` offset without removing the early increment, which breaks the blue store's base/index decomposition. Do not restructure to chase this.
- May auto-resolve as more globals in the preceding region get annotated (then retail's disp resolves to the same preceding symbol as the build's). If you need it gone sooner, the only lever is the *preceding* global's placement (e.g. an uninitialized-vs-initialized declaration difference moves it between BSS and `.data`), which is out of scope for the function being reconstructed. Record it and move on.
- Signal: the verbose compare shows exactly the negative-offset reads as the only mismatches, each paired as `<OFFSETN>` (retail) vs `g_otherGlobal+k` (build), with every positive-offset access into the same array matching. Verify with a byte dump before accepting — if the disp32 values differ by more than the symbol relocation, it is a real codegen difference, not this artifact.
