from datetime import datetime, timedelta, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from fractions import Fraction
from math import comb
import unittest
from unittest import mock
from contextlib import redirect_stderr, redirect_stdout

from tools import decomp_options as options
from tools import decomp_study as study
from tools.decomp_mismatch import ROUTE_ORDER
from tools.tests.test_decomp_options import digest, make_preregistration
from tools.tests.test_decomp_options import make_registry


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def run_git(root, *arguments, env=None):
    completed = subprocess.run(
        ["git", *arguments],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )
    return completed.stdout.strip()


def write_registry(root, registry):
    path = root / "tools/Resources/decomp-options.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(registry, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def make_repository(root):
    for relative in study.TRUSTED_INPUTS:
        if relative in {".gitignore", "tools/Resources/decomp-options.json"}:
            continue
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PROJECT_ROOT / relative, target)
    (root / ".gitignore").write_text("/.decomp-replay/\n", encoding="utf-8")
    write_registry(root, make_registry(include_study=False))
    run_git(root, "init", "-b", "agent/continuous")
    run_git(root, "config", "user.name", "Study Test")
    run_git(root, "config", "user.email", "study@example.invalid")
    environment = dict(os.environ)
    environment.update(
        {
            "GIT_AUTHOR_DATE": "2020-01-01T00:00:00+00:00",
            "GIT_COMMITTER_DATE": "2020-01-01T00:00:00+00:00",
        }
    )
    run_git(root, "add", ".")
    run_git(root, "commit", "-m", "base", env=environment)
    parent = run_git(root, "rev-parse", "HEAD")
    registry = make_registry()
    write_registry(root, registry)
    run_git(root, "add", "tools/Resources/decomp-options.json")
    run_git(root, "commit", "-m", "preregister study", env=environment)
    introduction = run_git(root, "rev-parse", "HEAD")
    run_git(
        root,
        "update-ref",
        "refs/remotes/origin/agent/continuous",
        introduction,
    )
    return registry["preregistrations"][0], parent, introduction


def make_study_receipt(root, preregistration, introduction):
    observation_time = datetime(2020, 1, 1, tzinfo=timezone.utc)
    receipt_time = observation_time + timedelta(seconds=1)
    study.ingest_observations(
        preregistration["study_id"],
        introduction,
        make_observations(preregistration),
        root=root,
        clock=lambda: observation_time,
    )
    summary = study.certify_study(
        preregistration["study_id"],
        root=root,
        clock=lambda: receipt_time,
    )
    return summary, receipt_time


def make_observations(preregistration, *, synthetic=False):
    rows = []
    treatment_ids = [item["id"] for item in preregistration["treatments"]]
    for index, population in enumerate(preregistration["population"]):
        for arm in ["off", *treatment_ids]:
            if arm == "off":
                primary = 100
            elif arm == treatment_ids[0]:
                primary = 101
            else:
                primary = 100
            rows.append(
                {
                    **population,
                    "arm": arm,
                    "status": "ok",
                    "primary": primary,
                    "protected": {
                        "terminal-bytes": 5,
                        "elapsed-budget": 5,
                    },
                    "budget_used": 7,
                }
            )
    return {
        "schema_version": 1,
        "kind": "option-study-observations",
        "synthetic": synthetic,
        "rows": rows,
    }


def passing_evaluation():
    preregistration = make_preregistration()
    return study.evaluate_study(
        preregistration,
        make_observations(preregistration),
        verified_evidence=True,
    )


def evaluate_study(preregistration, observations):
    return study.evaluate_study(
        preregistration, observations, verified_evidence=True
    )


def evaluate_training(corpus, linked_study):
    return study.evaluate_training_gate(
        corpus, linked_study, verified_evidence=True
    )


def make_training_corpus(*, synthetic=False):
    rows = []
    for index in range(200):
        group = index // 4
        rows.append(
            {
                "row_commitment": digest(f"row-{index}"),
                "campaign_commitment": digest(f"train-campaign-{group}"),
                "target_commitment": digest(f"train-target-{group}"),
                "route": ROUTE_ORDER[index % 5],
                "label": index % 2 == 0,
                "split": "train" if group < 25 else "test",
                "valid": True,
            }
        )
    return {
        "schema_version": 1,
        "kind": "route-policy-training-corpus",
        "synthetic": synthetic,
        "rows": rows,
    }


class DecompStudyTests(unittest.TestCase):
    def test_exact_sign_tail_uses_fraction(self):
        expected = Fraction(sum(comb(12, index) for index in range(10, 13)), 2**12)
        self.assertEqual(study.exact_sign_tail(10, 12), expected)
        self.assertIsInstance(study.exact_sign_tail(10, 12), Fraction)

    def test_complete_real_paired_study_passes(self):
        result = passing_evaluation()
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["real_evidence"])
        self.assertEqual(result["aggregate"]["cases"], 20)
        self.assertEqual(result["aggregate"]["campaigns"], 20)
        self.assertEqual(result["aggregate"]["targets"], 20)
        treatment = result["treatments"][0]
        self.assertEqual(treatment["wins"], 20)
        self.assertEqual(treatment["losses"], 0)
        self.assertEqual(treatment["protected_regressions"], 0)
        self.assertEqual(
            treatment["sign_test_p"], {"numerator": 1, "denominator": 2**20}
        )
        self.assertEqual(
            treatment["bonferroni_alpha"], {"numerator": 1, "denominator": 20}
        )

    def test_caller_claimed_real_evidence_fails_closed(self):
        preregistration = make_preregistration()
        result = study.evaluate_study(
            preregistration, make_observations(preregistration)
        )
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["real_evidence"])
        self.assertIn("unverified-acquisition", result["failure_codes"])

        training = study.evaluate_training_gate(
            make_training_corpus(), passing_evaluation()
        )
        self.assertEqual(training["status"], "failed")
        self.assertFalse(training["verified_evidence"])
        self.assertIn("unverified-acquisition", training["failure_codes"])

    def test_sweep_keeps_all_treatments_and_requires_one_winner(self):
        preregistration = make_preregistration(sweep=True)
        result = evaluate_study(
            preregistration, make_observations(preregistration)
        )
        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(result["treatments"]), 2)
        self.assertEqual(result["winner"], "candidate-on")
        self.assertEqual(
            result["treatments"][0]["bonferroni_alpha"],
            {"numerator": 1, "denominator": 40},
        )

    def test_two_passing_treatments_fail_even_with_one_aggregate_winner(self):
        preregistration = make_preregistration(sweep=True)
        observations = make_observations(preregistration)
        for row in observations["rows"]:
            if row["arm"] == "context-on":
                row["primary"] = 102
        result = evaluate_study(preregistration, observations)
        self.assertEqual(result["status"], "failed")
        self.assertIn("passing-treatment-count", result["failure_codes"])

    def test_aggregate_tie_fails(self):
        preregistration = make_preregistration(sweep=True)
        observations = make_observations(preregistration)
        for row in observations["rows"]:
            if row["arm"] == "context-on":
                row["primary"] = 101
                row["protected"]["terminal-bytes"] = 4
        result = evaluate_study(preregistration, observations)
        self.assertIn("no-unique-primary-aggregate", result["failure_codes"])
        self.assertIsNone(result["winner"])

    def test_loss_or_protected_regression_fails(self):
        preregistration = make_preregistration()
        observations = make_observations(preregistration)
        treatment = next(row for row in observations["rows"] if row["arm"] != "off")
        treatment["primary"] = 99
        result = evaluate_study(preregistration, observations)
        self.assertEqual(result["treatments"][0]["losses"], 1)
        self.assertEqual(result["status"], "failed")
        observations = make_observations(preregistration)
        treatment = next(row for row in observations["rows"] if row["arm"] != "off")
        treatment["protected"]["terminal-bytes"] = 4
        treatment["protected"]["elapsed-budget"] = 6
        result = evaluate_study(preregistration, observations)
        self.assertEqual(result["treatments"][0]["protected_regressions"], 2)
        self.assertEqual(result["status"], "failed")

    def test_missing_extra_duplicate_error_and_short_budget_fail(self):
        preregistration = make_preregistration()
        transforms = []
        transforms.append(lambda rows: rows.pop())
        transforms.append(
            lambda rows: rows.append({**rows[-1], "case_commitment": digest("extra")})
        )
        transforms.append(lambda rows: rows.append(dict(rows[-1])))
        transforms.append(
            lambda rows: rows[-1].update(
                {"status": "error", "primary": None, "protected": None}
            )
        )
        transforms.append(lambda rows: rows[-1].update({"budget_used": 6}))
        expected = [
            "missing-cells",
            "extra-cells",
            "duplicate-cells",
            "failed-or-invalid-rows",
            "incomplete-budget",
        ]
        for transform, code in zip(transforms, expected):
            observations = make_observations(preregistration)
            transform(observations["rows"])
            result = evaluate_study(preregistration, observations)
            with self.subTest(code=code):
                self.assertIn(code, result["failure_codes"])
                self.assertEqual(result["status"], "failed")

    def test_boolean_metrics_and_budget_are_invalid(self):
        preregistration = make_preregistration()
        for field in ("primary", "budget_used"):
            observations = make_observations(preregistration)
            observations["rows"][0][field] = True
            with self.subTest(field=field), self.assertRaises(study.StudyError):
                evaluate_study(preregistration, observations)

    def test_synthetic_study_or_rows_never_pass(self):
        preregistration = make_preregistration(synthetic=True)
        result = evaluate_study(
            preregistration, make_observations(preregistration)
        )
        self.assertIn("synthetic-evidence", result["failure_codes"])
        preregistration = make_preregistration()
        result = evaluate_study(
            preregistration,
            make_observations(preregistration, synthetic=True),
        )
        self.assertIn("synthetic-evidence", result["failure_codes"])

    def test_small_or_repeated_population_fails(self):
        preregistration = make_preregistration()
        preregistration["population"] = preregistration["population"][:19]
        from tools.decomp_options import preregistration_id

        preregistration["study_id"] = preregistration_id(preregistration)
        result = evaluate_study(
            preregistration, make_observations(preregistration)
        )
        self.assertIn("minimum-cases", result["failure_codes"])

    def test_distinct_campaign_target_and_non_tie_boundaries_fail(self):
        from tools.decomp_options import preregistration_id

        cases = []
        preregistration = make_preregistration()
        preregistration["population"][-1]["campaign_commitment"] = preregistration[
            "population"
        ][0]["campaign_commitment"]
        preregistration["study_id"] = preregistration_id(preregistration)
        cases.append((preregistration, "minimum-campaigns"))
        preregistration = make_preregistration()
        preregistration["population"][-1]["target_commitment"] = preregistration[
            "population"
        ][0]["target_commitment"]
        preregistration["study_id"] = preregistration_id(preregistration)
        cases.append((preregistration, "minimum-targets"))
        for preregistration, code in cases:
            result = evaluate_study(
                preregistration, make_observations(preregistration)
            )
            with self.subTest(code=code):
                self.assertIn(code, result["failure_codes"])
        preregistration = make_preregistration()
        observations = make_observations(preregistration)
        treatment_rows = [
            row for row in observations["rows"] if row["arm"] == "candidate-on"
        ]
        for row in treatment_rows[9:]:
            row["primary"] = 100
        result = evaluate_study(preregistration, observations)
        self.assertEqual(result["treatments"][0]["non_ties"], 9)
        self.assertEqual(result["status"], "failed")

    def test_route_training_gate_passes_without_training(self):
        result = evaluate_training(
            make_training_corpus(), passing_evaluation()
        )
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["route_policy_only"])
        self.assertFalse(result["training_performed"])
        self.assertEqual(result["aggregate"]["valid_rows"], 200)
        self.assertEqual(result["aggregate"]["campaigns"], 50)
        self.assertEqual(result["aggregate"]["targets"], 50)
        self.assertEqual(result["aggregate"]["qualifying_routes"], 5)
        self.assertEqual(result["aggregate"]["positive_labels"], 100)
        self.assertEqual(result["aggregate"]["negative_labels"], 100)

    def test_route_training_rejects_unknown_route_and_boolean_commitments(self):
        corpus = make_training_corpus()
        corpus["rows"][0]["route"] = "invented-route"
        with self.assertRaisesRegex(study.StudyError, "not canonical"):
            evaluate_training(corpus, passing_evaluation())
        corpus = make_training_corpus()
        corpus["rows"][0]["row_commitment"] = True
        with self.assertRaises(study.StudyError):
            evaluate_training(corpus, passing_evaluation())

    def test_route_training_rejects_overlap_duplicates_invalid_and_synthetic(self):
        mutations = []
        mutations.append(lambda corpus: corpus["rows"][0].update({"split": "test"}))
        mutations.append(
            lambda corpus: corpus["rows"][1].update(
                {"row_commitment": corpus["rows"][0]["row_commitment"]}
            )
        )
        mutations.append(lambda corpus: corpus["rows"][0].update({"valid": False}))
        mutations.append(lambda corpus: corpus.update({"synthetic": True}))
        codes = [
            "split-group-overlap",
            "duplicate-row-commitments",
            "invalid-rows",
            "synthetic-evidence",
        ]
        for mutation, code in zip(mutations, codes):
            corpus = make_training_corpus()
            mutation(corpus)
            result = evaluate_training(corpus, passing_evaluation())
            with self.subTest(code=code):
                self.assertIn(code, result["failure_codes"])
                self.assertEqual(result["status"], "failed")

    def test_route_training_rechecks_linked_study_result(self):
        linked = passing_evaluation()
        linked["status"] = "failed"
        result = evaluate_training(make_training_corpus(), linked)
        self.assertIn("linked-study-not-passing", result["failure_codes"])

    def test_route_training_exact_minimum_boundaries_fail(self):
        mutations = []
        mutations.append(lambda corpus: corpus["rows"].pop())

        def repeat_campaign(corpus):
            source = corpus["rows"][48 * 4]["campaign_commitment"]
            for row in corpus["rows"][49 * 4 : 50 * 4]:
                row["campaign_commitment"] = source

        def repeat_target(corpus):
            source = corpus["rows"][48 * 4]["target_commitment"]
            for row in corpus["rows"][49 * 4 : 50 * 4]:
                row["target_commitment"] = source

        mutations.extend([repeat_campaign, repeat_target])
        mutations.append(
            lambda corpus: [
                row.update({"route": ROUTE_ORDER[0]})
                for row in corpus["rows"]
                if row["route"] == ROUTE_ORDER[4]
            ]
        )
        mutations.append(
            lambda corpus: [
                row.update({"label": index < 49})
                for index, row in enumerate(corpus["rows"])
            ]
        )
        mutations.append(
            lambda corpus: [
                row.update({"label": not (index < 49)})
                for index, row in enumerate(corpus["rows"])
            ]
        )
        mutations.append(
            lambda corpus: [row.update({"split": "train"}) for row in corpus["rows"]]
        )
        codes = [
            "minimum-valid-rows",
            "minimum-campaigns",
            "minimum-targets",
            "minimum-qualified-routes",
            "minimum-positive-labels",
            "minimum-negative-labels",
            "empty-train-or-test-split",
        ]
        for mutation, code in zip(mutations, codes):
            corpus = make_training_corpus()
            mutation(corpus)
            result = evaluate_training(corpus, passing_evaluation())
            with self.subTest(code=code):
                self.assertIn(code, result["failure_codes"])

    def test_route_training_rejects_cross_namespace_commitments(self):
        corpus = make_training_corpus()
        corpus["rows"][0]["row_commitment"] = corpus["rows"][0][
            "campaign_commitment"
        ]
        result = evaluate_training(corpus, passing_evaluation())
        self.assertIn("commitment-namespace-overlap", result["failure_codes"])


class DecompStudyStorageTests(unittest.TestCase):
    def test_eager_transitive_quality_tool_must_match_head(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            (root / "tools/decomp_quality.py").write_text(
                "# changed eager dependency\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(study.StudyError, "not clean at HEAD"):
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    make_observations(preregistration),
                    root=root,
                )

    def test_empty_list_does_not_touch_private_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_registry(root, make_registry(include_study=False))
            result = study.list_studies(root)
            self.assertEqual(result["status"], "unavailable")
            self.assertFalse((root / ".decomp-replay").exists())

    def test_private_duplicate_key_error_does_not_echo_the_key(self):
        secret = "private-secret-token"
        with self.assertRaises(study.StudyError) as caught:
            study._strict_json(
                f'{{"{secret}": 1, "{secret}": 2}}'.encode("utf-8"),
                "private corpus",
            )
        self.assertNotIn(secret, str(caught.exception))

    def test_ingest_is_order_independent_and_recovers_original_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            observations = make_observations(preregistration)
            first = study.ingest_observations(
                preregistration["study_id"],
                introduction,
                observations,
                root=root,
                clock=lambda: datetime.now(timezone.utc),
            )
            reordered = {**observations, "rows": list(reversed(observations["rows"]))}

            def must_not_restamp():
                self.fail("an immutable corpus retry requested a new timestamp")

            second = study.ingest_observations(
                preregistration["study_id"],
                introduction,
                reordered,
                root=root,
                clock=must_not_restamp,
            )
            self.assertEqual(first, second)

            corpus_path = (
                root
                / ".decomp-replay/studies/observations"
                / preregistration["study_id"]
                / "corpus.json"
            )
            temporary_path = corpus_path.with_name(".corpus.json.tmp")
            os.replace(corpus_path, temporary_path)
            recovered = study.ingest_observations(
                preregistration["study_id"],
                introduction,
                observations,
                root=root,
                clock=must_not_restamp,
            )
            self.assertEqual(first, recovered)
            self.assertTrue(corpus_path.is_file())
            self.assertFalse(temporary_path.exists())

    def test_ingest_rejects_a_conflicting_rerun(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            observations = make_observations(preregistration)
            study.ingest_observations(
                preregistration["study_id"],
                introduction,
                observations,
                root=root,
            )
            changed = make_observations(preregistration)
            changed["rows"][1]["primary"] += 1
            with self.assertRaisesRegex(study.StudyError, "logical content"):
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    changed,
                    root=root,
                )

    def test_preregistration_ancestry_and_timestamp_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, parent, introduction = make_repository(root)
            observations = make_observations(preregistration)
            with self.assertRaisesRegex(study.StudyError, "exact study"):
                study.ingest_observations(
                    preregistration["study_id"],
                    parent,
                    observations,
                    root=root,
                )
            before = datetime(2019, 12, 31, 23, 59, 59, tzinfo=timezone.utc)
            with self.assertRaisesRegex(study.StudyError, "must follow"):
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    observations,
                    root=root,
                    clock=lambda: before,
                )
            equal = datetime(2020, 1, 1, tzinfo=timezone.utc)
            saved = study.ingest_observations(
                preregistration["study_id"],
                introduction,
                observations,
                root=root,
                clock=lambda: equal,
            )
            self.assertEqual(saved["status"], "stored")

    def test_high_level_forged_real_corpus_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            observations = make_observations(preregistration, synthetic=False)
            study.ingest_observations(
                preregistration["study_id"],
                introduction,
                observations,
                root=root,
            )
            evaluation = study.evaluate_registered_study(
                preregistration["study_id"], root=root
            )
            self.assertEqual(evaluation["status"], "failed")
            self.assertFalse(evaluation["real_evidence"])
            self.assertIn("unverified-acquisition", evaluation["failure_codes"])
            summary = study.certify_study(
                preregistration["study_id"], root=root
            )
            self.assertEqual(summary["status"], "failed")
            self.assertFalse(summary["certified"])
            self.assertIn("unverified-acquisition", summary["failure_codes"])
            receipt_id = summary["receipt"]["content_sha256"]
            self.assertEqual(
                study.verify_study_receipt(
                    preregistration["study_id"], receipt_id, root=root
                ),
                summary,
            )

    def test_incomplete_corpus_cannot_be_replaced_by_selected_results(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            incomplete = make_observations(preregistration)
            incomplete["rows"].pop()
            study.ingest_observations(
                preregistration["study_id"],
                introduction,
                incomplete,
                root=root,
            )
            summary = study.certify_study(
                preregistration["study_id"], root=root
            )
            self.assertEqual(summary["status"], "failed")
            self.assertIn("missing-cells", summary["failure_codes"])
            with self.assertRaisesRegex(study.StudyError, "logical content"):
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    make_observations(preregistration),
                    root=root,
                )

    def test_tampered_and_stale_receipts_fail_revalidation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study.ingest_observations(
                preregistration["study_id"],
                introduction,
                make_observations(preregistration),
                root=root,
            )
            summary = study.certify_study(preregistration["study_id"], root=root)
            receipt_id = summary["receipt"]["content_sha256"]
            receipt_path = (
                root
                / ".decomp-replay/studies/receipts"
                / f"{receipt_id}.json"
            )
            original = receipt_path.read_bytes()
            receipt_path.write_bytes(original + b" ")
            with self.assertRaises(study.StudyError):
                study.verify_study_receipt(
                    preregistration["study_id"], receipt_id, root=root
                )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study.ingest_observations(
                preregistration["study_id"],
                introduction,
                make_observations(preregistration),
                root=root,
            )
            summary = study.certify_study(preregistration["study_id"], root=root)
            receipt_id = summary["receipt"]["content_sha256"]
            (root / "tools/__init__.py").write_text("# changed\n", encoding="utf-8")
            run_git(root, "add", "tools/__init__.py")
            run_git(root, "commit", "-m", "change trusted input")
            head = run_git(root, "rev-parse", "HEAD")
            run_git(
                root,
                "update-ref",
                "refs/remotes/origin/agent/continuous",
                head,
            )
            with self.assertRaisesRegex(study.StudyError, "stale or invalid"):
                study.verify_study_receipt(
                    preregistration["study_id"], receipt_id, root=root
                )

    def test_private_corpus_rejects_symlinks_and_hardlinks(self):
        for link_kind in ("symlink", "hardlink"):
            with self.subTest(link_kind=link_kind), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                preregistration, _, introduction = make_repository(root)
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    make_observations(preregistration),
                    root=root,
                )
                corpus_path = (
                    root
                    / ".decomp-replay/studies/observations"
                    / preregistration["study_id"]
                    / "corpus.json"
                )
                outside = root / "private-copy"
                shutil.copy2(corpus_path, outside)
                os.chmod(outside, 0o600)
                corpus_path.unlink()
                if link_kind == "symlink":
                    corpus_path.symlink_to(outside)
                else:
                    os.link(outside, corpus_path)
                with self.assertRaises(study.StudyError):
                    study.evaluate_registered_study(
                        preregistration["study_id"], root=root
                    )

    def test_secure_publication_recovers_only_matching_temporary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            os.chmod(root, 0o700)
            directory_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            try:
                temporary = root / ".corpus.json.tmp"
                temporary.write_bytes(b"expected")
                os.chmod(temporary, 0o600)
                study.replay._write_exclusive_at(
                    directory_fd,
                    "corpus.json",
                    b"expected",
                    reject_unknown_temporary=True,
                )
                self.assertEqual((root / "corpus.json").read_bytes(), b"expected")

                other = root / ".other.json.tmp"
                other.write_bytes(b"unknown")
                os.chmod(other, 0o600)
                with self.assertRaises(study.replay.ReplayError):
                    study.replay._write_exclusive_at(
                        directory_fd,
                        "other.json",
                        b"expected",
                        reject_unknown_temporary=True,
                    )
                self.assertEqual(other.read_bytes(), b"unknown")
            finally:
                os.close(directory_fd)

    def test_git_proofs_ignore_path_shims_and_injected_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            shim = root / "shim"
            shim.mkdir()
            sentinel = root / "ambient-git-ran"
            fake_git = shim / "git"
            fake_git.write_text(
                f"#!/bin/sh\ntouch {sentinel.as_posix()}\nexit 99\n",
                encoding="utf-8",
            )
            fake_git.chmod(0o755)
            with mock.patch.dict(
                os.environ,
                {
                    "PATH": shim.as_posix(),
                    "GIT_OBJECT_DIRECTORY": (root / "false-objects").as_posix(),
                    "LD_PRELOAD": (root / "false-library.so").as_posix(),
                },
            ):
                saved = study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    make_observations(preregistration),
                    root=root,
                )
            self.assertEqual(saved["status"], "stored")
            self.assertFalse(sentinel.exists())

    def test_alternate_git_index_cannot_hide_the_real_staged_index(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            alternate = root / "alternate-index"
            shutil.copy2(root / ".git/index", alternate)
            probe = root / "staged-probe"
            probe.write_text("staged\n", encoding="utf-8")
            run_git(root, "add", "staged-probe")
            with mock.patch.dict(
                os.environ, {"GIT_INDEX_FILE": alternate.as_posix()}
            ):
                with self.assertRaisesRegex(study.StudyError, "clean index"):
                    study.ingest_observations(
                        preregistration["study_id"],
                        introduction,
                        make_observations(preregistration),
                        root=root,
                    )

    def test_merged_add_then_remove_private_history_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            run_git(root, "switch", "-c", "private-history")
            private_file = root / ".decomp-replay/leaked-row"
            private_file.parent.mkdir()
            private_file.write_text("private\n", encoding="utf-8")
            run_git(root, "add", "-f", ".decomp-replay/leaked-row")
            run_git(root, "commit", "-m", "add private row")
            run_git(root, "rm", ".decomp-replay/leaked-row")
            run_git(root, "commit", "-m", "remove private row")
            run_git(root, "switch", "agent/continuous")
            run_git(
                root,
                "merge",
                "--no-ff",
                "private-history",
                "-m",
                "merge private history",
            )
            head = run_git(root, "rev-parse", "HEAD")
            run_git(
                root,
                "update-ref",
                "refs/remotes/origin/agent/continuous",
                head,
            )
            with self.assertRaisesRegex(study.StudyError, "appears in HEAD history"):
                study.ingest_observations(
                    preregistration["study_id"],
                    introduction,
                    make_observations(preregistration),
                    root=root,
                )

    def test_training_lifecycle_is_linked_aggregate_only_and_unverified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            study_receipt_id = study_summary["receipt"]["content_sha256"]
            corpus = make_training_corpus(synthetic=False)
            stored = study.ingest_training_corpus(
                preregistration["study_id"],
                study_receipt_id,
                corpus,
                root=root,
                clock=lambda: receipt_time,
            )
            self.assertEqual(stored["status"], "stored")
            evaluation = study.evaluate_registered_training_gate(
                preregistration["study_id"], root=root
            )
            self.assertEqual(evaluation["status"], "failed")
            self.assertFalse(evaluation["verified_evidence"])
            self.assertIn("unverified-acquisition", evaluation["failure_codes"])
            self.assertIn("linked-study-not-passing", evaluation["failure_codes"])
            summary = study.certify_training_gate(
                preregistration["study_id"],
                root=root,
                clock=lambda: receipt_time + timedelta(seconds=1),
            )
            self.assertEqual(summary["status"], "failed")
            self.assertFalse(summary["certified"])
            self.assertFalse(summary["training_performed"])
            self.assertNotIn(
                corpus["rows"][0]["row_commitment"],
                json.dumps(summary, sort_keys=True),
            )
            gate_receipt_id = summary["receipt"]["content_sha256"]
            self.assertEqual(
                study.verify_training_gate_receipt(
                    preregistration["study_id"], gate_receipt_id, root=root
                ),
                summary,
            )

    def test_missing_training_link_does_not_create_private_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, _ = make_repository(root)
            with self.assertRaises(study.StudyError):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    digest("missing-study-receipt"),
                    make_training_corpus(),
                    root=root,
                )
            self.assertFalse((root / ".decomp-replay").exists())

    def test_training_chronology_rejects_earlier_and_accepts_equal_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            receipt_id = study_summary["receipt"]["content_sha256"]
            with self.assertRaisesRegex(study.StudyError, "must follow"):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    receipt_id,
                    make_training_corpus(),
                    root=root,
                    clock=lambda: receipt_time - timedelta(seconds=1),
                )
            saved = study.ingest_training_corpus(
                preregistration["study_id"],
                receipt_id,
                make_training_corpus(),
                root=root,
                clock=lambda: receipt_time,
            )
            self.assertEqual(saved["status"], "stored")

    def test_training_retry_recovers_temp_and_rejects_unknown_residue(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            receipt_id = study_summary["receipt"]["content_sha256"]
            corpus = make_training_corpus()
            first = study.ingest_training_corpus(
                preregistration["study_id"],
                receipt_id,
                corpus,
                root=root,
                clock=lambda: receipt_time,
            )
            reordered = {**corpus, "rows": list(reversed(corpus["rows"]))}

            def must_not_restamp():
                self.fail("an immutable training retry requested a new timestamp")

            self.assertEqual(
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    receipt_id,
                    reordered,
                    root=root,
                    clock=must_not_restamp,
                ),
                first,
            )
            corpus_path = (
                root
                / ".decomp-replay/studies/training"
                / preregistration["study_id"]
                / "corpus.json"
            )
            temporary = corpus_path.with_name(".corpus.json.tmp")
            os.replace(corpus_path, temporary)
            self.assertEqual(
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    receipt_id,
                    corpus,
                    root=root,
                    clock=must_not_restamp,
                ),
                first,
            )
            temporary.write_bytes(b"unknown-private-content")
            os.chmod(temporary, 0o600)
            with self.assertRaisesRegex(study.StudyError, "unknown content"):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    receipt_id,
                    corpus,
                    root=root,
                    clock=must_not_restamp,
                )
            self.assertEqual(temporary.read_bytes(), b"unknown-private-content")

    def test_training_corpus_cannot_change_rows_or_linked_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            first_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            first_receipt = first_summary["receipt"]["content_sha256"]
            corpus = make_training_corpus()
            study.ingest_training_corpus(
                preregistration["study_id"],
                first_receipt,
                corpus,
                root=root,
                clock=lambda: receipt_time,
            )
            changed = make_training_corpus()
            changed["rows"][0]["label"] = not changed["rows"][0]["label"]
            with self.assertRaisesRegex(study.StudyError, "logical content"):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    first_receipt,
                    changed,
                    root=root,
                )
            second_summary = study.certify_study(
                preregistration["study_id"],
                root=root,
                clock=lambda: receipt_time + timedelta(seconds=1),
            )
            second_receipt = second_summary["receipt"]["content_sha256"]
            self.assertNotEqual(first_receipt, second_receipt)
            with self.assertRaisesRegex(study.StudyError, "logical content"):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    second_receipt,
                    corpus,
                    root=root,
                )
            with self.assertRaises(study.StudyError):
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    passing_evaluation(),
                    corpus,
                    root=root,
                )

    def test_training_receipt_and_link_tampering_or_staleness_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            study_receipt_id = study_summary["receipt"]["content_sha256"]
            study.ingest_training_corpus(
                preregistration["study_id"],
                study_receipt_id,
                make_training_corpus(),
                root=root,
                clock=lambda: receipt_time,
            )
            gate = study.certify_training_gate(
                preregistration["study_id"],
                root=root,
                clock=lambda: receipt_time + timedelta(seconds=1),
            )
            gate_id = gate["receipt"]["content_sha256"]
            linked_path = (
                root
                / ".decomp-replay/studies/receipts"
                / f"{study_receipt_id}.json"
            )
            linked_path.write_bytes(linked_path.read_bytes() + b" ")
            with self.assertRaises(study.StudyError):
                study.verify_training_gate_receipt(
                    preregistration["study_id"], gate_id, root=root
                )

    def test_training_gate_receipt_tamper_and_unknown_temp_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            study.ingest_training_corpus(
                preregistration["study_id"],
                study_summary["receipt"]["content_sha256"],
                make_training_corpus(),
                root=root,
                clock=lambda: receipt_time,
            )
            gate_time = receipt_time + timedelta(seconds=1)
            gate = study.certify_training_gate(
                preregistration["study_id"],
                root=root,
                clock=lambda: gate_time,
            )
            gate_id = gate["receipt"]["content_sha256"]
            receipt_path = (
                root
                / ".decomp-replay/studies/training-receipts"
                / f"{gate_id}.json"
            )
            original = receipt_path.read_bytes()
            receipt_path.write_bytes(original + b" ")
            with self.assertRaises(study.StudyError):
                study.verify_training_gate_receipt(
                    preregistration["study_id"], gate_id, root=root
                )
            receipt_path.write_bytes(original)
            temporary = receipt_path.with_name(f".{gate_id}.json.tmp")
            temporary.write_bytes(b"unknown-private-content")
            os.chmod(temporary, 0o600)
            with self.assertRaisesRegex(study.StudyError, "unknown content"):
                study.certify_training_gate(
                    preregistration["study_id"],
                    root=root,
                    clock=lambda: gate_time,
                )
            self.assertEqual(temporary.read_bytes(), b"unknown-private-content")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            preregistration, _, introduction = make_repository(root)
            study_summary, receipt_time = make_study_receipt(
                root, preregistration, introduction
            )
            study.ingest_training_corpus(
                preregistration["study_id"],
                study_summary["receipt"]["content_sha256"],
                make_training_corpus(),
                root=root,
                clock=lambda: receipt_time,
            )
            gate = study.certify_training_gate(
                preregistration["study_id"],
                root=root,
                clock=lambda: receipt_time + timedelta(seconds=1),
            )
            gate_id = gate["receipt"]["content_sha256"]
            (root / "tools/__init__.py").write_text("# stale\n", encoding="utf-8")
            run_git(root, "add", "tools/__init__.py")
            run_git(root, "commit", "-m", "change trusted input")
            head = run_git(root, "rev-parse", "HEAD")
            run_git(
                root,
                "update-ref",
                "refs/remotes/origin/agent/continuous",
                head,
            )
            with self.assertRaises(study.StudyError):
                study.verify_training_gate_receipt(
                    preregistration["study_id"], gate_id, root=root
                )

    def test_training_corpus_rejects_symlinks_and_hardlinks(self):
        for link_kind in ("symlink", "hardlink"):
            with self.subTest(link_kind=link_kind), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                preregistration, _, introduction = make_repository(root)
                study_summary, receipt_time = make_study_receipt(
                    root, preregistration, introduction
                )
                study.ingest_training_corpus(
                    preregistration["study_id"],
                    study_summary["receipt"]["content_sha256"],
                    make_training_corpus(),
                    root=root,
                    clock=lambda: receipt_time,
                )
                corpus_path = (
                    root
                    / ".decomp-replay/studies/training"
                    / preregistration["study_id"]
                    / "corpus.json"
                )
                outside = root / "training-private-copy"
                shutil.copy2(corpus_path, outside)
                os.chmod(outside, 0o600)
                corpus_path.unlink()
                if link_kind == "symlink":
                    corpus_path.symlink_to(outside)
                else:
                    os.link(outside, corpus_path)
                with self.assertRaises(study.StudyError):
                    study.evaluate_registered_training_gate(
                        preregistration["study_id"], root=root
                    )


class DecompStudyCliTests(unittest.TestCase):
    def test_study_trust_set_includes_all_eager_tool_imports(self):
        self.assertEqual(
            set(study.TRUSTED_INPUTS),
            {
                ".gitignore",
                "tools/__init__.py",
                "tools/decomp",
                "tools/decomp.ps1",
                "tools/decomp_mismatch.py",
                "tools/decomp_options.py",
                "tools/decomp_quality.py",
                "tools/decomp_replay.py",
                "tools/decomp_study.py",
                "tools/Resources/decomp-options.json",
            },
        )
        self.assertEqual(study.TRUSTED_INPUT_MODES["tools/decomp"], "100755")
        for path, mode in study.TRUSTED_INPUT_MODES.items():
            if path != "tools/decomp":
                self.assertEqual(mode, "100644")

    def test_public_status_and_list_are_json_and_do_not_touch_private_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_registry(root, make_registry(include_study=False))
            output = io.StringIO()
            with mock.patch.object(options, "ROOT", root), redirect_stdout(output):
                self.assertEqual(options.main(["status", "--json"]), 0)
            self.assertEqual(json.loads(output.getvalue())["status"], "unavailable")
            self.assertFalse((root / ".decomp-replay").exists())

            output = io.StringIO()
            with mock.patch.object(study, "ROOT", root), redirect_stdout(output):
                self.assertEqual(study.main(["list"]), 0)
            self.assertEqual(json.loads(output.getvalue())["status"], "unavailable")
            self.assertFalse((root / ".decomp-replay").exists())

    def test_option_require_and_study_require_pass_use_nonzero_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_registry(root, make_registry(include_study=False))
            error = io.StringIO()
            with mock.patch.object(options, "ROOT", root), redirect_stderr(error):
                self.assertEqual(
                    options.main(["require", "candidate-gate", "--json"]), 1
                )
            self.assertEqual(json.loads(error.getvalue())["status"], "unavailable")

        failed = {
            "schema_version": 1,
            "kind": "option-study-evaluation",
            "status": "failed",
            "failure_codes": ["unverified-acquisition"],
        }
        output = io.StringIO()
        with mock.patch.object(
            study, "evaluate_registered_study", return_value=failed
        ), redirect_stdout(output):
            code = study.main(["evaluate", digest("study"), "--require-pass"])
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(output.getvalue()), failed)

    def test_cli_input_is_bounded_owner_only_and_nofollow(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "input.json"
            source.write_text('{"kind":"fixture"}\n', encoding="utf-8")
            source.chmod(0o600)
            self.assertEqual(
                study._read_cli_document(source, "private test input")["kind"],
                "fixture",
            )
            source.chmod(0o644)
            with self.assertRaisesRegex(study.StudyError, "missing or invalid"):
                study._read_cli_document(source, "private test input")
            source.chmod(0o600)
            link = root / "link.json"
            link.symlink_to(source)
            with self.assertRaises(study.StudyError):
                study._read_cli_document(link, "private test input")
            hardlink = root / "hardlink.json"
            os.link(source, hardlink)
            with self.assertRaises(study.StudyError):
                study._read_cli_document(source, "private test input")

    def test_cli_error_does_not_echo_private_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.json"
            secret = "private-secret-row-value"
            source.write_text(
                f'{{"{secret}": 1, "{secret}": 2}}', encoding="utf-8"
            )
            source.chmod(0o600)
            error = io.StringIO()
            with redirect_stderr(error):
                code = study.main(
                    [
                        "ingest",
                        digest("study"),
                        "--preregistration-commit",
                        "a" * 40,
                        "--input",
                        str(source),
                    ]
                )
            self.assertEqual(code, 2)
            self.assertNotIn(secret, error.getvalue())
            self.assertEqual(json.loads(error.getvalue())["status"], "failed")

    def test_bash_and_powershell_wrappers_have_matching_dispatch(self):
        bash = (PROJECT_ROOT / "tools/decomp").read_text(encoding="utf-8")
        powershell = (PROJECT_ROOT / "tools/decomp.ps1").read_text(encoding="utf-8")
        for command, module in (
            ("options", "tools.decomp_options"),
            ("study", "tools.decomp_study"),
        ):
            with self.subTest(command=command):
                self.assertIn(f"    {command})", bash)
                self.assertIn(f'-m {module} "$@"', bash)
                self.assertIn(f'"{command}"', powershell)
                self.assertIn(f'"{module}"', powershell)
        self.assertTrue(os.access(PROJECT_ROOT / "tools/decomp", os.X_OK))

    def test_bash_study_dispatch_does_not_source_local_build_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wrapper = root / "tools/decomp"
            python = root / ".tooling/venv/bin/python"
            wrapper.parent.mkdir(parents=True)
            python.parent.mkdir(parents=True)
            shutil.copy2(PROJECT_ROOT / "tools/decomp", wrapper)
            wrapper.chmod(0o755)
            python.write_text(
                "#!/usr/bin/env bash\nprintf '%s\\n' \"$*\"\n",
                encoding="utf-8",
            )
            python.chmod(0o755)
            (root / ".decomp-local").write_text(
                'touch "$ROOT/local-config-was-sourced"\n', encoding="utf-8"
            )
            completed = subprocess.run(
                [wrapper, "study", "list"],
                cwd=root,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(completed.stdout, "-m tools.decomp_study list\n")
            self.assertFalse((root / "local-config-was-sourced").exists())


if __name__ == "__main__":
    unittest.main()
