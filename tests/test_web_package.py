import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WebPackageTests(unittest.TestCase):
    def test_required_web_assets_exist(self):
        for relative in ("web/index.html", "web/styles.css", "web/app.js", "homebrew.js"):
            path = ROOT / relative
            self.assertTrue(path.is_file(), relative)
            self.assertGreater(path.stat().st_size, 100, relative)

    def test_frontend_uses_documented_api(self):
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        for endpoint in (
            "/api/status", "/api/devices", "/api/scan",
            "/api/connect", "/api/disconnect", "/api/saved",
        ):
            self.assertIn(endpoint, script)

    def test_websrv_extension_contract(self):
        script = (ROOT / "homebrew.js").read_text(encoding="utf-8")
        self.assertRegex(script, r"async\s+function\s+main\s*\(")
        self.assertIn("ApiClient.launchApp", script)
        self.assertIn("/eboot.elf", script)
        self.assertIn("127.0.0.1:18195", script)

    def test_version_is_a_new_release(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertNotEqual(version, "0.1.5")

    def test_legacy_metadata_is_valid_json(self):
        # Payload Manager metadata is retained as an optional compatibility asset.
        json.loads((ROOT / "fgg-playpods-gui.elf.json").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
