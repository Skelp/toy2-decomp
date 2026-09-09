# Mismatch workflow roadmap

- [x] 1. Add one shared mismatch taxonomy. The three workflow consumers emit schema version 1 routes without changing their legacy fields.
- [x] 2. Add provenance-safe bounded experiment sessions. Each session reports its best trial and stops promotion when an early gate fails.
- [x] 3. Add normalized context and evidence packs. Each pack can retrieve the accepted source that has matching provenance.
- [x] 4. Add a post-write impact and refuter check. Source promotion requires an accepted claim that survives this check.
- [x] 5. Add an allowlisted leaf differential oracle. Each oracle result has a receipt that binds all inputs and outputs.
- [ ] 6. Add a private replay benchmark. Route advice requires a verified improvement trajectory on the private cases.
  - [x] Add fail-closed replay, redaction, storage, and certification infrastructure.
  - [ ] Enroll the required verified private cases.
  - [ ] Issue a passing certificate and enable route advice.
- [ ] 7. Add option-gated population tool studies. Project training requires verified study data and an explicit data gate.

One canonical writer remains required for all source changes. Dynamic evidence cannot replace the exact or effective completion rules.
