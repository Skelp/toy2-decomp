import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import decomp_binary
from tools.extract_resources import encode_resource_file, extract_resources


class ExtractResourcesTests(unittest.TestCase):
    def test_encodes_all_leaf_id_forms(self):
        entries = (
            decomp_binary.ResourceEntry((2, 127, 2057), b"bitmap", 0),
            decomp_binary.ResourceEntry((14, "APPICON", 2057), b"icon", 0),
            decomp_binary.ResourceEntry(("TYPE", "A\"B", 9), b"named", 0),
        )

        encoded = encode_resource_file(entries)

        records = self.decode_records(encoded)
        self.assertEqual(
            records,
            [
                (0, 0, 0, b""),
                (2, 127, 2057, b"bitmap"),
                (14, "APPICON", 2057, b"icon"),
                ("TYPE", 'A"B', 9, b"named"),
            ],
        )

    def test_resource_file_changes_with_payload(self):
        first = encode_resource_file(
            (decomp_binary.ResourceEntry((2, 127, 2057), b"first", 0),)
        )
        second = encode_resource_file(
            (decomp_binary.ResourceEntry((2, 127, 2057), b"second", 0),)
        )

        self.assertNotEqual(first, second)

    def test_extracts_synthetic_entries(self):
        entries = (decomp_binary.ResourceEntry((10, 20, 1033), b"payload", 0),)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "toy2.exe"
            source.write_bytes(b"synthetic")
            output = root / "generated" / "toy2.res"
            output.parent.mkdir()

            with patch(
                "tools.extract_resources.decomp_binary.read_resources",
                return_value=entries,
            ):
                extract_resources(source, output)

            self.assertTrue(output.is_file())
            self.assertEqual(
                self.decode_records(output.read_bytes()),
                [(0, 0, 0, b""), (10, 20, 1033, b"payload")],
            )

    def test_rejects_missing_retail_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            missing = root / "missing.exe"

            with self.assertRaisesRegex(
                ValueError, "retail executable does not exist"
            ):
                extract_resources(missing, root / "generated" / "toy2.res")

    def test_rejects_invalid_resource_metadata(self):
        with self.assertRaisesRegex(ValueError, "fit in 16 bits"):
            encode_resource_file(
                (decomp_binary.ResourceEntry((0x10000, 1, 1033), b"x", 0),)
            )
        with self.assertRaisesRegex(ValueError, "type, name, and language"):
            encode_resource_file(
                (decomp_binary.ResourceEntry((2, 127), b"x", 0),)
            )

    @staticmethod
    def decode_identifier(data, offset):
        marker = struct.unpack_from("<H", data, offset)[0]
        if marker == 0xFFFF:
            return struct.unpack_from("<H", data, offset + 2)[0], offset + 4
        end = offset
        while data[end : end + 2] != b"\0\0":
            end += 2
        return data[offset:end].decode("utf-16-le"), end + 2

    @classmethod
    def decode_records(cls, data):
        records = []
        offset = 0
        while offset < len(data):
            data_size, header_size = struct.unpack_from("<II", data, offset)
            resource_type, cursor = cls.decode_identifier(data, offset + 8)
            resource_name, cursor = cls.decode_identifier(data, cursor)
            cursor += -cursor % 4
            _, _, language, _, _ = struct.unpack_from("<IHHII", data, cursor)
            payload = data[offset + header_size : offset + header_size + data_size]
            records.append((resource_type, resource_name, language, payload))
            offset += header_size + data_size
            offset += -offset % 4
        return records


if __name__ == "__main__":
    unittest.main()
