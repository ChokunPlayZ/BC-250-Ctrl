"""Guard against erasing an unknown flash layout."""

import csv
import io
import os
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from contextlib import redirect_stdout
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from recover import LAYOUTS, detect_layout, main


def fake_partitions(layout):
    return [
        SimpleNamespace(
            name=label, offset=offset, size=size,
            type=0x01 if label in {"nvs", "zb_fct"} else 0x00,
            subtype={"nvs": 0x02, "zb_fct": 0x81}.get(label, 0),
        )
        for label, (offset, size) in layout.items()
    ]


class RecoveryLayoutTests(unittest.TestCase):
    def test_layouts_match_firmware_partition_tables(self):
        root = Path(__file__).resolve().parents[2]
        for name, layout in LAYOUTS.items():
            with self.subTest(name=name):
                size = name.split()[0]
                with (root / f"partitions_{size}mb.csv").open(newline="") as stream:
                    rows = csv.reader(line for line in stream if not line.startswith("#"))
                    from_csv = {
                        row[0].strip(): (int(row[3], 0), int(row[4], 0))
                        for row in rows if row
                    }
                self.assertEqual(layout, from_csv)

    def test_supported_layouts(self):
        for name, layout in LAYOUTS.items():
            with self.subTest(name=name):
                partitions = fake_partitions(layout)
                self.assertEqual(detect_layout(partitions)[0], name)

    def test_changed_or_duplicate_layout_is_rejected(self):
        layout = LAYOUTS["4 MB"]
        partitions = fake_partitions(layout)
        partitions[0].offset += 0x1000
        with self.assertRaises(ValueError):
            detect_layout(partitions)
        partitions[0].offset -= 0x1000
        partitions.append(partitions[0])
        with self.assertRaises(ValueError):
            detect_layout(partitions)
        partitions.pop()
        partitions[0].subtype = 0x03
        with self.assertRaises(ValueError):
            detect_layout(partitions)

    def test_confirmation_precedes_erasure(self):
        layout = LAYOUTS["4 MB"]
        partitions = fake_partitions(layout)
        erased = []

        class FakeTarget:
            def __init__(self, port, esptool_args):
                self.partition_table = partitions
                self.port = port
                self.esptool_args = esptool_args

            def erase_partition(self, partition):
                erased.append(partition.name)

        class FakePartitionName:
            def __init__(self, name):
                self.name = name

        with TemporaryDirectory() as temp:
            parttool_dir = Path(temp) / "components" / "partition_table"
            parttool_dir.mkdir(parents=True)
            (parttool_dir / "parttool.py").touch()
            fake_module = SimpleNamespace(ParttoolTarget=FakeTarget, PartitionName=FakePartitionName)
            old_path = sys.path[:]
            try:
                with patch.dict(os.environ, {"IDF_PATH": temp}), \
                     patch.dict(sys.modules, {"parttool": fake_module}), \
                     patch("recover.no_reset_mode", return_value="no-reset"), \
                     patch("builtins.input", return_value="NO"), \
                     redirect_stdout(io.StringIO()):
                    self.assertEqual(main(["--port", "FAKE"]), 1)
                    self.assertEqual(erased, [])
                with patch.dict(os.environ, {"IDF_PATH": temp}), \
                     patch.dict(sys.modules, {"parttool": fake_module}), \
                     patch("recover.no_reset_mode", return_value="no-reset"), \
                     patch("builtins.input", return_value="ERASE ALL"), \
                     redirect_stdout(io.StringIO()):
                    self.assertEqual(main(["--port", "FAKE"]), 0)
                    self.assertEqual(erased, ["nvs", "zb_fct"])
            finally:
                sys.path[:] = old_path


if __name__ == "__main__":
    unittest.main()
