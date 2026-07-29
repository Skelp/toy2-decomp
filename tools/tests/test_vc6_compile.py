import unittest

from tools.vc6_compile import rewrite_arguments



class Vc6CompileTests(unittest.TestCase):
    def test_uses_an_object_specific_pdb(self):
        self.assertEqual(
            rewrite_arguments(
                [
                    "/nologo",
                    "/Fooutput/source.obj",
                    "/Fdoutput/target/",
                    "source.cpp",
                ]
            ),
            [
                "/nologo",
                "/Fooutput/source.obj",
                "/Fdoutput/source.obj.pdb",
                "source.cpp",
            ],
        )

    def test_preserves_pdb_argument_without_an_object(self):
        self.assertEqual(
            rewrite_arguments(["/Fdoutput/target/", "source.cpp"]),
            ["/Fdoutput/target/", "source.cpp"],
        )


if __name__ == "__main__":
    unittest.main()
