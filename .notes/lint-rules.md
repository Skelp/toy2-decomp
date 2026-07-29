# Source plausibility lint rules

`tools/decomp lint` checks whether reconstructed code has a coherent source
model. The machine-code comparison cannot make this check. A function can have
an exact match and still be a transliteration of decompiler output.

## Finding classes

An error identifies a source form that is not acceptable in a completed
function. Fix the source model, or change the function annotation to `STUB`
until you can reconstruct it.

A warning identifies a plausible quality problem. It does not stop a commit by
default. Use the evidence from callers, callees, retail strings, and related
Nu3D code before you rename an unknown item.

The committed baseline contains reviewed old findings. A baseline finding stays
in the output as legacy debt, but it does not stop unrelated work. Do not add a
new finding to the baseline. When a change fixes old debt, remove the stale
baseline row in the same commit.

## Data-model errors

- `raw-layout-access`: A literal byte offset replaces a structure field.
- `typed-byte-roundtrip`: A typed pointer changes to bytes and back to the same
  type. Use element or row stride arithmetic.
- `anonymous-buffer-view`: Code dereferences an inline cast instead of a named
  typed view.
- `implicit-record-layout`: Several offsets treat a scalar buffer as an
  undeclared record.
- `placeholder-field-use`: Completed code reads or writes an unresolved member.
- `magic-pointer`: Code converts a nonzero integer literal to a pointer.
- `signature-concealment`: A cast at a project-function call hides an incorrect
  caller type.

Runtime byte movement is valid for file cursors, compressed data, locked
surfaces, and SDK buffers. Convert the boundary once to a role-named typed local.
Use `sizeof`, `offsetof`, or a named format constant when the byte count is part
of a real file or API format.

## Reconstruction errors

- `decompiler-identifier`: A completed body contains an analysis name such as
  `iVar2`, `field88`, or `LAB_00401000`.
- `placeholder-parameter`: A completed interface does not name a parameter by
  its role. This rule is a warning for an unfinished `STUB`.
- `unfinished-function`: A `FUNCTION` annotation has an empty or default-return
  body. Use `STUB` unless comparison proves a retail null function.

## Quality warnings

- `unknown-symbol`: Completed code still uses a working `UnkFunc` or `g_unk`
  name. A genuinely unexplained global can keep `g_unk`.
- `unnamed-bitmask`: Flags or state use a raw mask instead of an established
  name.
- `unstructured-control-flow`: A completed body has several goto paths.
- `unpinned-layout`: A recovered layout has unresolved storage and no size
  assertion.
- `signature-name-drift`: A declaration and definition use different role
  names.
- `placeholder-field`: A shared layout or stub signature still has an
  unresolved name.
- `arithmetic-name`: An identifier states a calculation instead of a role.
- `opaque-state-slot`: A numbered `data[N]` slot hides a state field.
- `address-named-symbol`: A completed function uses an address as a symbol name.
- `unexplained-helper`: A helper name does not identify its operation.
- `original-name-vocabulary`: Source uses an alias where retail text supplies
  the original engine name.

The linter does not reject integer lookup tables, fixed-point arithmetic, manual
shifts, IEEE 754 bit operations, or other established Nu3D idioms.

## Narrow exceptions

Only `anonymous-buffer-view` and `typed-byte-roundtrip` can have a local
exception. Put this directive on the line before the expression:

```cpp
// decomp-lint: allow[typed-byte-roundtrip] reason: SDK pitch is measured in bytes
```

The reason must identify the external format or API constraint. A directive
applies to one expression. File-wide exceptions are not available.

Use `tools/decomp lint --explain RULE` for a short explanation. Use
`tools/decomp lint --format json` when a tool needs structured findings.
