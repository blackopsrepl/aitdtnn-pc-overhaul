import subprocess
import sys
import unittest
from pathlib import Path


class PublicAssetBuilderCliTests(unittest.TestCase):
    def test_cli_only_exposes_asset_extraction(self) -> None:
        script = Path(__file__).resolve().parents[1] / "tools" / "build_assets.py"
        result = subprocess.run(
            [sys.executable, str(script), "--help"],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("dreamcast_image", result.stdout)
        self.assertIn("output", result.stdout)
        self.assertNotIn("--patch-exe", result.stdout)
        self.assertNotIn("--unpatch-exe", result.stdout)
        self.assertNotIn("--check-exe", result.stdout)


if __name__ == "__main__":
    unittest.main()
