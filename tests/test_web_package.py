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

    def test_audiobridge_brand_and_local_preview(self):
        page = (ROOT / "web/index.html").read_text(encoding="utf-8")
        tile = (ROOT / "homebrew.js").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("AudioBridge — GUI", page)
        self.assertIn("AudioBridge — GUI", tile)
        self.assertIn("FathiGhanem", readme)
        self.assertIn("FGGstore/FGG-PlayPods", readme)
        self.assertIn('data-language="ru"', page)
        self.assertIn('data-language="en"', page)
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        self.assertIn("audiobridge-language", script)
        self.assertIn("Bluetooth-аудио для PS5", script)
        self.assertIn('href="styles.css"', page)
        self.assertIn('src="app.js"', page)
        self.assertNotIn('href="/styles.css"', page)
        self.assertNotIn('src="/app.js"', page)

    def test_frontend_uses_documented_api(self):
        script = (ROOT / "web/app.js").read_text(encoding="utf-8")
        for endpoint in (
            "/api/status", "/api/devices", "/api/scan",
            "/api/connect", "/api/disconnect", "/api/saved",
            "/api/audio-profile",
        ):
            self.assertIn(endpoint, script)

    def test_websrv_extension_contract(self):
        script = (ROOT / "homebrew.js").read_text(encoding="utf-8")
        self.assertRegex(script, r"async\s+function\s+main\s*\(")
        self.assertIn("ApiClient.launchApp", script)
        self.assertIn("/eboot.elf", script)
        self.assertIn('window.location.hostname || "127.0.0.1"', script)
        self.assertIn('`http://${host}:18195/`', script)

    def test_version_is_a_new_release(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertNotEqual(version, "0.1.5")
        page = (ROOT / "web/index.html").read_text(encoding="utf-8")
        metadata = json.loads((ROOT / "audiobridge-gui.elf.json").read_text(encoding="utf-8"))
        self.assertIn(f'<span class="version">{version}</span>', page)
        self.assertEqual(metadata["version"], version)

    def test_native_tile_uses_unique_local_deeplink(self):
        metadata = json.loads((ROOT / "tile/sce_sys/param.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["titleId"], "ABRG18195")
        self.assertEqual(metadata["applicationCategoryType"], 65536)
        self.assertEqual(metadata["deeplinkUri"], "http://127.0.0.1:18195/")
        self.assertNotIn("webAppUri", metadata)

        installer = (ROOT / "src/tile.c").read_text(encoding="utf-8")
        self.assertIn("audiobridge-owner.txt", installer)
        self.assertIn("refusing to overwrite", installer)
        self.assertIn("sceAppInstUtilAppUnInstall", installer)
        self.assertNotIn("rm -rf", installer)

    def test_latency_profiles_keep_quality_and_expose_metrics(self):
        a2dp = (ROOT / "src/a2dp.c").read_text(encoding="utf-8")
        api = (ROOT / "src/http_server.c").read_text(encoding="utf-8")
        self.assertIn("STABLE_MAX_LAG_MS      250", a2dp)
        self.assertIn("LOW_LATENCY_MAX_LAG_MS 80", a2dp)
        self.assertIn("LOW_LATENCY_SBC_FRAMES 2", a2dp)
        self.assertIn("MAX_BITPOOL     53", a2dp)
        for field in ("packetDurationUs", "queueMs", "trimmedFrames", "captureOverruns"):
            self.assertIn(field, api)

    def test_user_facing_terms_cover_a2dp_audio_devices(self):
        page = (ROOT / "web/app.js").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        instructions = (ROOT / "INSTALL-RU.txt").read_text(encoding="utf-8")
        for text in (page, readme, instructions):
            self.assertIn("A2DP", text)
            self.assertIn("SBC", text)
        self.assertIn("Bluetooth-аудиоустрой", page)
        self.assertIn("Bluetooth-аудиоустрой", readme)

    def test_legacy_metadata_is_valid_json(self):
        # Payload Manager metadata is retained as an optional compatibility asset.
        metadata = json.loads((ROOT / "audiobridge-gui.elf.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["name"], "AudioBridge-GUI")


if __name__ == "__main__":
    unittest.main()
