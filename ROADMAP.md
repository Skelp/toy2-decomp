# Roadmap

Unresolved retail bytes by family on 2026-09-10 (358k total). Fill the actual
column at the end of each week from `tools/decomp candidates` and
`tools/decomp throughput`.

| Family | Members | Unresolved bytes | Planned work | Expected delta | Actual delta |
| --- | --- | --- | --- | --- | --- |
| SoftwareRenderer textured-polygon rasterizers | 14 implemented at 20-24%, 2 STUB | 38k | refinement from the 42% edge model on 0x0045B0B0, then siblings | +7k | |
| SoftwareRenderer colour-offset and tail slots | 20 STUB/unstarted | 121k | coverage from sibling forms after the polygon family moves | +10k | |
| Collision sweep and resolve solvers | 16 | 42k | coverage of SweepAgainstCandidates/RaycastAgainstCandidates, refinement of ResolveSubstep | +2k | |
| Level `::Interactions` state machines | 16 | 34k | refinement from the top of `candidates --refine --yield` | +2k | |
| DevDraw submitters | 6 | 11k | coverage after re-sizing the gap-sized entries | +1k | |
| Map hygiene | 0x004490A0 and 19 unmapped Ghidra starts | 23k | add to `functions_map.txt`, confirm with `discover` | visibility only | |

Pilot pass line: at least half the Sep 8 daytime rate over ten campaigns with
tooling share under 15 percent (see `tools/Resources/throughput-baseline.json`).
