from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import decomp_provenance as provenance  # noqa: E402


class ProvenanceTests(unittest.TestCase):
    def prepare_root(self, root: Path) -> tuple[Path, Path]:
        retail = "retail\n"
        retail_sha256 = hashlib.sha256(retail.encode()).hexdigest()
        files = {
            "src/Probe.cpp": "void Probe() {}\n",
            "original/toy2.exe": retail,
            "build/toy2.exe": "build\n",
            "build/toy2.pdb": "symbols\n",
            "build/reccmp-build.yml": "targets: {}\n",
            "reccmp-project.yml": (
                "targets:\n"
                "  TOY2:\n"
                "    filename: toy2.exe\n"
                "    hash:\n"
                f"      sha256: {retail_sha256}\n"
            ),
            "reccmp-user.yml": (
                "targets:\n  TOY2:\n    path: original/toy2.exe\n"
            ),
            "build/decomp-function-sizes.json": (
                '[{"address":"00401000","size":16}]\n'
            ),
            "tools/Resources/functions_map.txt": (
                "0x00401000 Probe\n0x00401010 Next\n"
            ),
        }
        for relative, content in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        subprocess.run(["git", "add", "."], cwd=root, check=True)
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Provenance Tests",
                "-c",
                "user.email=provenance@example.invalid",
                "commit",
                "-qm",
                "Prepare provenance fixture",
            ],
            cwd=root,
            check=True,
        )
        report = root / "build/decomp-current-report.json"
        report.write_text(
            json.dumps(
                {
                    "data": [
                        {
                            "address": "0x00401000",
                            "matching": 0.6,
                            "type": 1,
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        diff = root / "build/decomp-diffs/0x00401000.txt"
        diff.parent.mkdir(parents=True, exist_ok=True)
        diff.write_text(
            "---\n+++\n@@ -0x401000,2 +0x401000,2 @@\n"
            "0x401000 : -mov eax, ecx\n"
            "0x401000 : +mov edx, ecx\n"
            "Probe is only 60.00% similar to the original, diff above\n",
            encoding="utf-8",
        )
        return report, diff

    def test_report_and_diff_are_bound_to_current_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, diff = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            report.write_text(report.read_text(encoding="utf-8"), encoding="utf-8")
            provenance.seal_report(report, root=root, input_receipt=captured)
            provenance.seal_diff(diff, "0x00401000", report, root=root)

            provenance.validate_report(report, root=root)
            provenance.validate_diff(
                diff, 0x00401000, current_match=0.6, root=root
            )

            (root / "build/toy2.exe").write_text("changed\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "stale"):
                provenance.validate_report(report, root=root)
            with self.assertRaisesRegex(ValueError, "stale"):
                provenance.validate_diff(diff, 0x00401000, root=root)

    def test_reccmp_user_config_cannot_redirect_the_retail_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, _ = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            report.write_text(report.read_text(encoding="utf-8"), encoding="utf-8")
            provenance.seal_report(report, root=root, input_receipt=captured)

            (root / "other.exe").write_bytes(b"other retail\n")
            (root / "reccmp-user.yml").write_text(
                "targets:\n  TOY2:\n    path: other.exe\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "must be original/toy2.exe"):
                provenance.reccmp_user_identity(root)
            with self.assertRaisesRegex(ValueError, "must be original/toy2.exe"):
                provenance.validate_report(report, root=root)

    def test_project_hash_rejects_changed_retail_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, _ = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            report.write_text(report.read_text(encoding="utf-8"), encoding="utf-8")
            provenance.seal_report(report, root=root, input_receipt=captured)

            (root / "original/toy2.exe").write_bytes(b"changed retail\n")
            with self.assertRaisesRegex(ValueError, "does not match reccmp-project.yml"):
                provenance.reccmp_user_identity(root)
            with self.assertRaisesRegex(ValueError, "does not match reccmp-project.yml"):
                provenance.validate_report(report, root=root)

    def test_reccmp_user_config_rejects_unsafe_or_missing_target_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.prepare_root(root)
            config = root / "reccmp-user.yml"
            cases = {
                "absolute": (
                    f"targets:\n  TOY2:\n    path: {(root / 'original/toy2.exe').resolve()}\n",
                    "must be relative",
                ),
                "escape": (
                    "targets:\n  TOY2:\n    path: ../outside.exe\n",
                    "escapes the repository",
                ),
                "missing": (
                    "targets:\n  OTHER:\n    path: original/toy2.exe\n",
                    "no TOY2 target",
                ),
            }
            for name, (content, message) in cases.items():
                with self.subTest(name=name):
                    config.write_text(content, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, message):
                        provenance.reccmp_user_identity(root)

    def test_report_seal_rejects_input_changes_during_generation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, _ = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            (root / "src/Probe.cpp").write_text(
                "void Probe(int value) {}\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "changed while"):
                provenance.seal_report(
                    report,
                    root=root,
                    input_receipt=captured,
                )

    def test_changed_diff_or_report_sidecar_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, diff = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            report.write_text(report.read_text(encoding="utf-8"), encoding="utf-8")
            provenance.seal_report(report, root=root, input_receipt=captured)
            provenance.seal_diff(diff, "0x00401000", report, root=root)

            diff.write_text(diff.read_text(encoding="utf-8") + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "changed after"):
                provenance.validate_diff(diff, 0x00401000, root=root)
            diff.write_text(
                diff.read_text(encoding="utf-8").rstrip() + "\n",
                encoding="utf-8",
            )

            report_sidecar = provenance.provenance_path(report)
            report_sidecar.write_text(
                report_sidecar.read_text(encoding="utf-8") + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "report identity changed"):
                provenance.validate_diff(diff, 0x00401000, root=root)

    def test_report_cannot_be_resealed_without_a_prior_input_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, _ = self.prepare_root(root)
            with self.assertRaisesRegex(ValueError, "captured before generation"):
                provenance.seal_report(report, root=root)

    def test_unchanged_preexisting_report_cannot_be_sealed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, _ = self.prepare_root(root)
            captured = root / "build/decomp-cache/provenance/input.json"
            provenance.capture_input(captured, report, root=root)
            with self.assertRaisesRegex(ValueError, "did not replace or update"):
                provenance.seal_report(report, root=root, input_receipt=captured)


if __name__ == "__main__":
    unittest.main()
