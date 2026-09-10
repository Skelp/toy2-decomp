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
  - [x] Add the default-off registry, exact evaluators, immutable private storage, aggregate failed receipts, and command infrastructure.
  - [ ] Add an authoritative acquisition controller and acquisition receipts.
  - [ ] Run a preregistered real private population and find one unique safe winner.
  - [ ] Activate the winner in a later meta commit with a current passing receipt.
  - [ ] Collect a qualified route corpus and issue a passing data-gate receipt.
  - [ ] Train and enable the route policy.

One canonical writer remains required for all source changes. Dynamic evidence cannot replace the exact or effective completion rules.

## Research references

These primary sources informed the roadmap. The last column identifies the related roadmap items. The project-specific receipts, sealed briefs, private storage, preregistration, and fail-closed gates are local safeguards. The cited systems do not define these controls. Links were verified on 2026-09-10.

| Reference | Result used in this roadmap | Items |
| --- | --- | --- |
| [LLM4Decompile: Decompiling Binary Code with Large Language Models](https://arxiv.org/abs/2403.05286) ([code](https://github.com/albertan017/LLM4Decompile)) | Refine conventional decompiler output and measure re-executability on separate benchmarks. | 3, 6, 7 |
| [DeGPT: Optimizing Decompiler Output with LLM](https://www.ndss-symposium.org/ndss-paper/degpt-optimizing-decompiler-output-with-llm/) ([code](https://github.com/PeiweiHu/DeGPT)) | Separate proposal, correction, and semantic acceptance roles. | 1, 2, 4 |
| [FidelityGPT: Correcting Decompilation Distortions with Retrieval Augmented Generation](https://arxiv.org/abs/2510.19615) ([code](https://github.com/ZhouZhiping045/FidelityGPT)) | Classify distortions and retrieve relevant code and variable dependencies. | 1, 3 |
| [Decaf: Improving Neural Decompilation with Automatic Feedback and Search](https://arxiv.org/abs/2605.11501) ([code](https://github.com/AlexShypula/decaf)) | Generate bounded candidates, reject compile failures, compare recompiled assembly, and retain the best candidate. | 2, 3, 5, 7 |
| [Constraint-Guided Multi-Agent Decompilation for Executable Binary Recovery](https://arxiv.org/abs/2604.23940) | Use separate parse, compile, and behavior constraints with specialized repair agents. | 1, 2, 4, 5 |
| [Binary Decompilation LLM with Feedback-Driven Multi-Turn Refinement](https://arxiv.org/abs/2606.16162) | Use stage-aware compiler, execution, and test feedback with progress and regression rewards. | 1, 2, 4, 5, 6 |
| [CHISEL-ing Back Source Code with AI-enabled Iterative Recovery](https://arxiv.org/abs/2608.27981) ([code](https://github.com/VarunKohli18/chisel)) | Combine compiler feedback, differential fuzzing, divergence memory, and best-candidate retention. | 2, 4, 5, 6 |
| [When LLM Decompilers Recompile More and Preserve Less](https://arxiv.org/abs/2609.05370) | Detect behavior that recompilation and shipped tests miss by replaying a fuzzing corpus against both implementations. | 4, 5, 6 |
| [Decompile-Bench: Million-Scale Binary-Source Function Pairs for Real-World Binary Decompilation](https://arxiv.org/abs/2505.12668) ([data and code](https://github.com/albertan017/LLM4Decompile/tree/main/decompile-bench)) | Use a separate, post-cutoff evaluation corpus and several decompilation metrics. | 6, 7 |
| [CODEFUSE-DEBENCH: An Empirical Study on Readability, Recompilability, and Functionality](https://arxiv.org/abs/2605.29490) ([code](https://github.com/codefuse-ai/CodeFuse-DeBench)) | Use a fixed repair budget, differential tracing, and controlled comparisons across decompilers, models, compilers, and architectures. | 1, 2, 5, 7 |
| [ReAgent](https://github.com/Dryxio/reagent) | Use bounded reverser and checker rounds, normalized evidence, structural checks, and build, test, runtime, and parity gates. | 2, 3, 4 |
