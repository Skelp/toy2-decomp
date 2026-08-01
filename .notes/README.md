# Reconstruction notes

Use `tools/decomp notes QUERY` to search these files. Do not read all note
files at the start of a campaign.

- `original-names.md` contains names recovered from retail strings. These names
  have the highest naming priority. Regenerate it with `tools/decomp names`.
- `codegen-patterns.md` contains concise source-form hypotheses for MSVC6.
- `refactor-debt.md` identifies source that still exposes implementation
  offsets or other plausibility debt.
- `reccmp-mechanics.md` explains comparison and annotation behavior.
- `shared-globals.md` records retail globals that cross translation units.

Add a compiler pattern only when it is general, actionable, and not already in
the file. Describe the visible symptom first. Store function-specific trials
with `tools/decomp experiment`; do not add a permanent narrative.
