# Shared data globals

Notes on widely-referenced retail data globals that cross TU boundaries. The
next agent does not need to re-derive their role.

## The zero-byte "empty string" global at 0x00528160

- A single zero-valued byte in `.bss` referenced from many functions across
  several subsystems (mode-selection window proc, the save-menu state machine,
  the main run loop, misc-event processing, etc.).
- Two usage shapes, both faithful to retail:
  1. **Address taken** and passed as a `char*` argument. Typically this passes
     to the error-handler that the logger's `GetErrorHandler(file, line)`
     returns. The caller invokes the handler with an empty message `""`.
  2. **Byte loaded** (`MOV AL, [0x00528160]`) into a 1-byte stack local.
     Then `&local` is used as a fallback empty string when a `const char*`
     argument is NULL.
- It is **not** a string literal. Those live in `.rdata` and lower to a `LEA`.
  The byte load proves the source reads a real global `char`.
- Reconstructed as `extern char g_emptyString;`. The definition lives in the
  SaveManager TU, the first matched user. Other TUs that reference it via raw
  `&DAT_00528160` are still unmatched stubs. When you reconstruct them, `extern`
  the same symbol rather than re-declare it.
- reccmp resolves the operand by symbol name on both sides. A build-side `char`
  global lands wherever the linker places it and still matches.
