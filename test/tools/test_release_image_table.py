"""Check the release image links and safe updates to release notes."""

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from idf_build import PROFILES
from release_image_table import END, PLACEHOLDER, START, image_table, update_notes


class ReleaseImageTableTests(unittest.TestCase):
    def test_table_links_every_release_image(self):
        with TemporaryDirectory() as directory:
            assets = Path(directory)
            for profile in PROFILES:
                (assets / f"bc250_ctrl-{profile}.bin").touch()
                (assets / f"bc250_ctrl-{profile}-full.bin").touch()

            table = image_table(assets, "owner/repo", "v1.2.3-rc.1")
            self.assertEqual(len(table.splitlines()), len(PROFILES) + 2)
            for profile in PROFILES:
                for suffix in ("", "-full"):
                    name = f"bc250_ctrl-{profile}{suffix}.bin"
                    self.assertIn(
                        f"[{name}](https://github.com/owner/repo/releases/download/"
                        f"v1.2.3-rc.1/{name})",
                        table,
                    )

            (assets / "bc250_ctrl-esp32_4mb-full.bin").unlink()
            with self.assertRaisesRegex(FileNotFoundError, "esp32_4mb-full"):
                image_table(assets, "owner/repo", "v1.2.3")

    def test_notes_keep_existing_text_and_replace_table_on_rerun(self):
        original = "# Release notes\n\nExisting details.\n"
        first = update_notes(original, "old table", "owner/repo", "v1.2.3")
        second = update_notes(first, "new table", "owner/repo", "v1.2.3")
        self.assertTrue(second.endswith(original))
        self.assertEqual(second.count(START), 1)
        self.assertEqual(second.count(END), 1)
        self.assertNotIn("old table", second)
        self.assertIn("new table", second)
        self.assertEqual(update_notes(second, "new table", "owner/repo", "v1.2.3"), second)
        self.assertIn("/blob/v1.2.3/docs/UPGRADE.md", second)
        self.assertIn("wipes configuration and Zigbee pairing", second)

    def test_placeholder_sets_table_position(self):
        original = f"# Release notes\n\nBefore\n\n{PLACEHOLDER}\n\nAfter\n"
        updated = update_notes(original, "table", "owner/repo", "v1.2.3")
        self.assertTrue(updated.startswith("# Release notes\n\nBefore\n\n"))
        self.assertTrue(updated.endswith("\n\nAfter\n"))
        self.assertNotIn(PLACEHOLDER, updated)
        self.assertEqual(update_notes(updated, "table", "owner/repo", "v1.2.3"), updated)

    def test_incomplete_markers_fail_without_changing_notes(self):
        with self.assertRaisesRegex(ValueError, "markers"):
            update_notes(f"Release notes\n{START}", "table", "owner/repo", "v1.2.3")
        with self.assertRaisesRegex(ValueError, "markers"):
            update_notes(f"{PLACEHOLDER}\n{PLACEHOLDER}", "table", "owner/repo", "v1.2.3")


if __name__ == "__main__":
    unittest.main()
