import unittest
from datetime import datetime
from pathlib import Path

import dataset_server


class DatasetServerTests(unittest.TestCase):
    def test_label_allowlist(self):
        self.assertEqual(dataset_server.sanitize_label("翎角"), "翎角")
        self.assertEqual(dataset_server.sanitize_label("麻嚕"), "麻嚕")
        self.assertEqual(dataset_server.sanitize_label("unknown"), "unknown")
        self.assertEqual(dataset_server.sanitize_label("../outside"), "unknown")

    def test_dataset_path_is_script_anchored(self):
        self.assertEqual(dataset_server.DATASET_PATH, Path(dataset_server.__file__).resolve().parent / "dataset")

    def test_filename_is_unique_jpeg_name(self):
        filename = dataset_server.build_filename("翎角", datetime(2026, 1, 2, 3, 4, 5, 678901))
        self.assertRegex(filename, r"^翎角_20260102030405678901_[0-9a-f]{32}\.jpg$")


if __name__ == "__main__":
    unittest.main()
