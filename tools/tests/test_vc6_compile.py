import os
import subprocess
import tempfile
import unittest
from pathlib import Path


LAUNCHER = Path(__file__).parents[1] / "vc6-compile"


class Vc6CompileTests(unittest.TestCase):
    def make_compiler(self, directory: Path) -> tuple[Path, Path]:
        argument_log = directory / "arguments.txt"
        compiler = directory / "compiler"
        compiler.write_text(
            "#!/usr/bin/env bash\n"
            "printf '%s\\n' \"$@\" > \"$ARGUMENT_LOG\"\n",
            encoding="utf-8",
        )
        compiler.chmod(0o755)
        return compiler, argument_log

    def test_uses_an_object_specific_pdb(self):
        with tempfile.TemporaryDirectory() as directory_name:
            compiler, argument_log = self.make_compiler(Path(directory_name))
            subprocess.run(
                [
                    LAUNCHER,
                    compiler,
                    "/nologo",
                    "/Fooutput/source.obj",
                    "/Fdoutput/target/",
                    "source.cpp",
                ],
                check=True,
                env={**os.environ, "ARGUMENT_LOG": str(argument_log)},
            )

            self.assertEqual(
                argument_log.read_text(encoding="utf-8").splitlines(),
                [
                    "/nologo",
                    "/Fooutput/source.obj",
                    "/Fdoutput/source.obj.pdb",
                    "source.cpp",
                ],
            )

    def test_preserves_pdb_argument_without_an_object(self):
        with tempfile.TemporaryDirectory() as directory_name:
            compiler, argument_log = self.make_compiler(Path(directory_name))
            subprocess.run(
                [LAUNCHER, compiler, "/Fdoutput/target/", "source.cpp"],
                check=True,
                env={**os.environ, "ARGUMENT_LOG": str(argument_log)},
            )

            self.assertEqual(
                argument_log.read_text(encoding="utf-8").splitlines(),
                ["/Fdoutput/target/", "source.cpp"],
            )


if __name__ == "__main__":
    unittest.main()
