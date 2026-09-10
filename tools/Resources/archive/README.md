# Archived campaign records

`campaign-ledger-2026-09-10.jsonl` is the full ledger as it was before the
campaign apparatus added on 2026-09-09 was removed. It contains 181 campaign
rows, 80 delivery rows, 6 evidence rows and 5 abort rows.

Notes for anyone who reads the September rows:

- Campaign `d003adb6` (0x004813B0, +15.9 effective bytes) was rejected at
  delivery on 2026-09-09T06:20Z. Its bytes were never integrated. Count it as
  zero.
- The `record_type: delivery` rows describe a delivery chain that no longer
  exists. The live tools ignore them.
- Rows from 2026-09-09 onward carry `prediction`, `deadlines` and receipt
  fields that no live tool reads.

The tools that produced these rows are available in git history at tag
`pre-simplification`. The staged, never-committed "economic gate" selector
change is on branch `archive/cohort-v2`.
