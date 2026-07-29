  # Harden the AI Decompilation Workflow

  ## Summary

  The repository is mechanically healthy, but its progress and quality signals overstate confidence:

  - Of 1,149 mapped functions, 850 are marked implemented, but only 489 are exact and 18 are reccmp-effective. The remaining 343 are ordinary partial or zero matches. See the build/decomp-report.html.
  - The .notes/caps-registry.tsv contains 67 entries. Thirty-three score below 50%, and 18 score below 25%. Only six use the byte-identical label-artifact category; one of those now matches exactly.
  - CAP evidence is not reproducible: detailed notes and the agent skill are ignored local files, trial source variants were not preserved, and CAP entries permanently hide functions from selection.
  - The quality gate passes with 8 legacy errors and 77 warnings. It misses per-function dashboard attribution because lint and report addresses use different canonical forms. Examples such as InitGameplayCamera can therefore become
    complete functions with address-named globals and opaque fields.

  - tools/decomp score calls effective matches “exact,” while candidates offers those same effective matches as unfinished work.

  Compiler entropy is real, but it must remain evidence, not a default explanation. The LEGO Island project estimates this effect at about 5% and emphasizes small reviewable changes. MSVC /O2 implies /Oy, while frame-pointer behavior
  can also be controlled per function, so EBP/FPO differences require compile-context investigation. Reccmp also documents limitations in its effective-match relocation logic. LEGO Island guidance
  (https://raw.githubusercontent.com/isledecomp/isle/master/CONTRIBUTING.md), Microsoft /Oy documentation (https://learn.microsoft.com/en-us/cpp/build/reference/oy-frame-pointer-omission), reccmp issue #324
  (https://github.com/isledecomp/reccmp/issues/324).

  Adopt the selected policy: retain single-agent automation, freeze new partial reconstruction during the initial audit, and permit permanent suppression only for mechanically proven tool-label artifacts.

  ## Verification Model and Interfaces

  - Give every FUNCTION two independent states:
      - Binary fidelity: exact, effective, tool-equivalent, or partial.
      - Source quality: clean or debt.

  - Standardize annotation tags:
      - [MATCHED]: exact binary result and clean source.
      - [EFFECTIVE]: reccmp-effective result and clean source.
      - [TOOL]: mechanically proven tool-only operand or relocation-label difference and clean source.
      - [PROVISIONAL]: every other FUNCTION, including exact code with source-quality debt.
      - Keep STUB for behavior that is known to be incomplete.

  - Define tool equivalence narrowly: the normalized opcode, control-flow, register, stack, call, and memory-access streams must agree. Differences may only be symbolic rendering or paired relocation target/addend naming. Frame
    layout, register allocation, block order, scheduling, or pointer-walk differences never qualify.

  - Replace permanent CAPs with:
      - A tracked tool-artifact ledger for [TOOL] cases.
      - A tracked audit ledger for provisional findings, previous claims, tested scores, and explicit revisit triggers. Audit entries never suppress candidates.

  - Make all normative agent inputs reproducible by tracking the decompilation skill, original-name extraction, codegen rules/observations, and every file referenced by the workflow. Add a consistency test that rejects references to
    missing or ignored normative files.

  ## Workflow and Tooling Changes

  - Add tools/decomp baseline to build a fresh parent-state report and record the compiler binary, flags, SDK, source dependency, and Git HEAD fingerprints under build/.
  - Add tools/decomp validate --target <address> --staged to:
      - Build and compare the full executable.
      - Distinguish exact, effective, tool-equivalent, and raw similarity.
      - Reject loss of any exact/effective/tool-equivalent result.
      - Reject score regressions in untouched functions.
      - Run lint, map checks, annotation checks, and diff checks.
      - Verify that the annotation tag agrees with the fresh report.
      - Treat all warnings as blockers for [MATCHED], [EFFECTIVE], and [TOOL].

  - Permit a readability-driven score decrease only during an audit. The function must remain [PROVISIONAL], the eliminated source defect and before/after scores must enter the audit ledger, and no unrelated function may regress.
  - Add tools/decomp experiment start|try|report <address>. Each labeled trial records the source patch, raw score, normalized diff, compiler fingerprint, and structural changes under build/. Compiler claims may cite these
    experiments, but cannot permanently suppress a function.

  - Add tools/decomp audit with --legacy-caps, --quality, and --score-below filters. This becomes the default session queue during the freeze.
  - Restrict candidates to genuine new work and fix it to exclude exact, effective, and proven tool-equivalent functions. Make score report 100% effective (raw N%) instead of “exact.”
  - Extend linting to catch address-named globals, opaque members such as data[N], unresolved fields used by complete functions, unexplained helper abstractions, and mismatched original-name vocabulary. Preserve unresolved globals
    only in provisional work.

  - Correct report address normalization. Replace the misleading repository-wide “quality PASS” with separate change-gate, source-debt, binary-fidelity, and verified-reconstruction metrics.
  - Make verified function and byte coverage the headline progress metric. Keep annotation coverage as a secondary implementation metric.

  ## Existing Repository Audit

  1. Remove all CAP-based candidate suppression.
  2. Recheck the six CAP-23 rows with the new tool-equivalence classifier:
      - Remove 0x00414320, which now matches exactly.
      - Retain at most the other five as [TOOL], and only when mechanically proven.

  3. Move the remaining 61 CAP claims to the provisional audit ledger without the assertion that they are correct or unfixable.
  4. Classify all 850 current FUNCTIONs with fresh binary and source-quality states.
  5. During the audit-first freeze:
      - Deep-audit all former non-tool CAPs.
      - Deep-audit every current function below 50% similarity.
      - Audit every exact/effective function with a lint finding.
      - Allow new targets only when they reach exact, effective, or tool-equivalent status in the same session; otherwise keep or restore STUB.

  6. Each audit must end in one of three states:
      - Verified exact/effective/tool-equivalent with clean source.
      - STUB because retail behavior is not established.
      - [PROVISIONAL] with a recorded uncertainty and revisit trigger.

  7. End the freeze after all former CAPs, sub-50% functions, and verified-code quality findings have received this classification. Continue auditing higher-scoring provisional functions through the normal queue.

  ## Test and Acceptance Plan

  - Add unit fixtures proving:
      - Effective matches retain their raw score and are never labeled exact.
      - Effective and tool-equivalent functions are excluded from new-work candidates.
      - Register, frame, scheduling, and block-order differences fail tool-equivalence classification.
      - Lint findings attach to canonical report addresses.
      - Annotation tags become stale and fail validation after a report regression.
      - Full-report regressions are detected even when the target improves.

  - Add migration tests confirming that no non-tool CAP remains suppressed and that all current functions have a valid status.
  - Run the full tooling test suite on Linux and Windows, then run build, full compare, lint, map checks, progress, report generation, and git diff --check.
  - Initial tooling and metadata migration requires no runtime test. Any audit that changes gameplay, loading, audio, or rendering behavior follows the existing runtime-testing policy.
  - Acceptance criteria:
      - The dashboard reports 489 exact, 18 effective, and 343 partial/zero mapped functions before audit changes, rather than presenting all 850 as equally complete.
      - InitGameplayCamera and similar code appear as provisional source debt.
      - No agent can create a permanent compiler-quirk suppression.
      - No new function receives verified status without a fresh comparison, clean source-quality result, and matching annotation tag.
